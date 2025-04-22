// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "base64.h"
#include "params.h"

extern char __StackLimit, __bss_end__;

#define COMMONSYSLOG	"system"
#define WATCHDOG_TIMEOUT_MS	30000 /* Reboot on 30s inactivity */

#define LOG_STATUS_HOOKS_COUNT	16

#define PERIODIC_LOG_MS	0

#define MAIN_WAIT_MS	100
#define BLINK_INTERVAL	3

typedef struct {
	log_status_cb_t hook;
	void *user_context;
} log_status_hook_t;

static struct {
	int sw_out_pin;
	uint32_t periodic_log_ms;
	uint32_t last_loop;
	uint8_t has_lcd:1;
	uint8_t has_wifi:1;
	uint8_t has_bt:1;
	uint8_t has_mqtt:1;
	uint8_t has_time:1;
	uint8_t has_swout:1;
	uint8_t has_temp:1;
	uint8_t has_usb:1;
	uint8_t has_wh:1;
	uint8_t has_websrv:1;
	uint8_t force_reboot:1;

	log_status_hook_t log_status[LOG_STATUS_HOOKS_COUNT];
	uint8_t log_status_count;
	int log_status_progress;
	bool reconnect;
} sys_context;

uint32_t samples_filter(uint32_t *samples, int total_count, int filter_count)
{
	uint32_t all;
	uint16_t sw;
	int i, j;

	/* bubble sort */
	for (i = 0 ; i < total_count - 1; i++) {
		for (j = 0 ; j < total_count - i - 1; j++) {
			if (samples[j] > samples[j+1]) {
				sw  = samples[j];
				samples[j] = samples[j+1];
				samples[j+1] = sw;
			}
		}
	}
	/* filter biggest and smallest */
	all = 0;
	for (i = filter_count ; i < total_count - filter_count; i++)
		all += samples[i];
	all /= total_count-(2*filter_count);

	return all;
}

uint32_t get_total_heap(void)
{
	static uint32_t mem_total;

	if (!mem_total)
		mem_total = &__StackLimit  - &__bss_end__;

	return mem_total;
}

uint32_t get_free_heap(void)
{
	struct mallinfo m = mallinfo();

//	return m.fordblks;
	return get_total_heap() - m.uordblks;

}

#define PRINT_BUF_LEN	32
static void dump_raw_data(char *topic, char *format, const uint8_t *data, int len)
{
	char print_buff[PRINT_BUF_LEN], buf[4];
	int i = 0, j = 0;

	print_buff[0] = 0;
	while (i < len) {
		snprintf(buf, 4, format, data[i++]);
		if ((j + strlen(buf)) >= PRINT_BUF_LEN) {
			j = 0;
			hlog_info(topic, "\t %s", print_buff);
			print_buff[0] = 0;
		}
		strcat(print_buff, buf);
		j += strlen(buf);
	}
	if (j)
		hlog_info(topic, "\t %s", print_buff);
}

void dump_hex_data(char *topic, const uint8_t *data, int len)
{
	dump_raw_data(topic, "%0.2X ", data, len);
}

void dump_char_data(char *topic, const uint8_t *data, int len)
{
	dump_raw_data(topic, "%c", data, len);
}

bool sw_out_init(void)
{
	char *config = param_get(SW_OUT_PIN);
	bool ret = false;
	int pin;

	sys_context.sw_out_pin  = -1;
	if (!config || strlen(config) < 1)
		goto out;

	pin = (int)strtol(config, NULL, 10);
	if (pin < 0 || pin >= 0xFFFF)
		goto out;

	sys_context.sw_out_pin = pin;
	gpio_init(sys_context.sw_out_pin);
	gpio_set_dir(sys_context.sw_out_pin, GPIO_OUT);
	sw_out_set(false);
	ret = true;

out:
	free(config);
	return ret;
}

void sw_out_set(bool state)
{
	if (state)
		gpio_put(sys_context.sw_out_pin, 1);
	else
		gpio_put(sys_context.sw_out_pin, 0);
}

static bool base_init(void)
{
	if (cyw43_arch_init()) {
		hlog_info(COMMONSYSLOG, "failed to initialize");
		return false;
	}
	cyw43_arch_enable_sta_mode();
	gpio_init(CYW43_WL_GPIO_LED_PIN);
	gpio_set_dir(CYW43_WL_GPIO_LED_PIN, GPIO_OUT);

	return true;
}

bool system_common_init(void)
{
	// Initialize chosen serial port, default 38400 baud
	set_sys_clock_khz(120000, true);
	stdio_init_all();
	srand(to_us_since_boot(get_absolute_time()));
	busy_wait_ms(2000);

	watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

	hlog_init(HLOG_INFO);
	hlog_info(COMMONSYSLOG, "Booting ... %d", watchdog_enable_caused_reboot());
	hlog_info(COMMONSYSLOG, "RAM: %d total / %d free bytes", get_total_heap(), get_free_heap());

	if (!base_init())
		return false;
	LED_ON;
	sys_context.log_status_count = 0;
	sys_context.log_status_progress = -1;

	sys_context.force_reboot = false;
	sys_context.periodic_log_ms = PERIODIC_LOG_MS;
	sys_context.has_wifi = wifi_init();
	sys_context.has_lcd = lcd_init();
	if (sys_context.has_lcd)
		hlog_info(COMMONSYSLOG, "LCD initialized");
	else
		hlog_info(COMMONSYSLOG, "no LCD attached");
	sys_context.has_bt = bt_init();
	sys_context.has_usb = usb_init();
	sys_context.has_mqtt = mqtt_init();
	sys_context.has_time = ntp_init();
	sys_context.has_temp = temperature_init();
	sys_context.has_swout = sw_out_init();
	sys_context.has_wh = webhook_init();
	sys_context.has_websrv = webserv_init();
	webdebug_init();
	sys_modules_init();
	LED_OFF;
	watchdog_update();

	return true;
}

void system_common_main(void)
{
	int blinik_count = 0;

	if (!system_common_init()) {
		printf("\r\nFailed to initialize the system\r\n");
		exit(1);
	}

	while (true) {
		if (blinik_count++ % BLINK_INTERVAL == 0)
			LED_ON;
		system_common_run();
		LED_OFF;
		busy_wait_ms(MAIN_WAIT_MS);
	}
}

bool system_log_in_progress(void)
{
	if (sys_context.log_status_progress >= 0 &&
		sys_context.log_status_progress < sys_context.log_status_count)
		return true;

	return false;
}

void system_log_status(void)
{
	if (system_log_in_progress())
		return;

	hlog_info(COMMONSYSLOG, "----------- Status -----------");
	hlog_info(COMMONSYSLOG, "Uptime: %s; free RAM: %d bytes; chip temperature: %3.2f *C",
			  get_uptime(), get_free_heap(), temperature_internal_get());
	sys_context.log_status_progress = 0;
	sys_modules_log();
}

static void system_log_run(void)
{
	int idx = sys_context.log_status_progress;

	if (idx < 0 || idx >= sys_context.log_status_count)
		return;

	if (sys_context.log_status[idx].hook)
		sys_context.log_status[idx].hook(sys_context.log_status[idx].user_context);

	sys_context.log_status_progress++;
	if (sys_context.log_status_progress >= sys_context.log_status_count) {
		hlog_info(COMMONSYSLOG, "----------- Status end--------");
		sys_context.log_status_progress = -1;
	}
}

int add_status_callback(log_status_cb_t cb, void *user_context)
{
	int idx = sys_context.log_status_count;

	if (sys_context.log_status_count >= LOG_STATUS_HOOKS_COUNT)
		return -1;

	sys_context.log_status[idx].hook = cb;
	sys_context.log_status[idx].user_context = user_context;
	sys_context.log_status_count++;

	return idx;
}

static void log_wd_boot(void)
{
	static bool one_time;

	if (one_time || !hlog_remoute())
		return;

	if (watchdog_enable_caused_reboot())
		hlog_warning(COMMONSYSLOG, "The device recovered from a watchdog reboot");
	else
		hlog_info(COMMONSYSLOG, "Normal power-on boot");

	one_time = true;
}

static void do_system_reconnect(void)
{
	hlog_info(COMMONSYSLOG, "Reconnecting ...");

	if (sys_context.has_mqtt)
		mqtt_reconnect();
	if (sys_context.has_wh)
		webhook_reconnect();
	if (sys_context.has_websrv)
		webserv_reconnect();
	hlog_reconnect();
	sys_modules_reconnect();
}

void system_reconnect(void)
{
	sys_context.reconnect = true;
}

void system_force_reboot(int delay_ms)
{
	hlog_info(COMMONSYSLOG, "System is rebooting in %dms ...", delay_ms);
	watchdog_enable(delay_ms, true);
	sys_context.force_reboot = true;
}

void system_set_periodic_log_ms(uint32_t ms)
{
	sys_context.periodic_log_ms = ms;
}

void wd_update(void)
{
	if (!sys_context.force_reboot)
		watchdog_update();
}

void system_common_run(void)
{
	sys_context.last_loop = to_ms_since_boot(get_absolute_time());
	wd_update();
	if (sys_context.has_lcd)
		lcd_refresh();
	hlog_connect();
	if (sys_context.has_temp)
		temperature_measure();
	wd_update();
	if (sys_context.has_wifi)
		wifi_connect();
	if (sys_context.has_bt)
		bt_run();
	wd_update();
	if (sys_context.has_mqtt)
		mqtt_run();
	if (sys_context.has_time)
		ntp_connect();
	wd_update();
	if (sys_context.has_usb)
		usb_run();
	wd_update();
	if (sys_context.has_wh)
		webhook_run();
	log_wd_boot();
	if (sys_context.has_websrv)
		webserv_run();
	log_wd_boot();
	if (sys_context.reconnect) {
		do_system_reconnect();
		sys_context.reconnect = false;
	}
	wd_update();
	webdebug_run();
	system_log_run();
	wd_update();
	sys_modules_run();
	if (sys_context.periodic_log_ms > 0) {
		static uint32_t llog;

		if ((sys_context.last_loop - llog) > sys_context.periodic_log_ms) {
			llog = sys_context.last_loop;
			system_log_status();
			wd_update();
		}
	}
}
