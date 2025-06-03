// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define COMMONSYSLOG	"system"
#define WATCHDOG_TIMEOUT_MS	30000 /* The maximum is 8388ms, which is approximately 8.3 seconds */

#define LOG_STATUS_HOOKS_COUNT	64

#define PERIODIC_LOG_MS	0

//#define MAIN_WAIT_MS	10
#ifdef MAIN_WAIT_MS
#define	BUSY_WAIT		busy_wait_ms(MAIN_WAIT_MS);
#else
#define	BUSY_WAIT
#endif
#define BLINK_INTERVAL	100

typedef struct {
	log_status_cb_t hook;
	void *user_context;
} log_status_hook_t;

static struct {
	uint32_t periodic_log_ms;
	uint32_t last_loop;
	uint8_t has_time:1;
	uint8_t has_swout:1;
	uint8_t has_temp:1;
	uint8_t force_reboot:1;

	log_status_hook_t log_status[LOG_STATUS_HOOKS_COUNT];
	uint8_t log_status_count;
	int log_status_progress;
	bool reconnect;
} sys_context;

static bool base_init(void)
{
	if (cyw43_arch_init()) {
		hlog_info(COMMONSYSLOG, "failed to initialize");
		return false;
	}
	cyw43_arch_enable_sta_mode();
	busy_wait_ms(2000);
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

	hlog_info(COMMONSYSLOG, "Booting ... %d", watchdog_enable_caused_reboot());
	hlog_info(COMMONSYSLOG, "RAM: %d total / %d free bytes", get_total_heap(), get_free_heap());

	if (!base_init())
		return false;

	wd_update();
	LED_ON;

	sys_context.log_status_count = 0;
	sys_context.log_status_progress = -1;
	sys_context.force_reboot = false;
	sys_context.periodic_log_ms = PERIODIC_LOG_MS;
	sys_context.has_time = ntp_init();
	wd_update();
	sys_context.has_temp = temperature_init();
	wd_update();
	wd_update();
	sys_modules_init();
	wd_update();
	LED_OFF;

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
		BUSY_WAIT;
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
	log_sys_health();
	sys_modules_log();
	sys_context.log_status_progress = 0;
}

static void system_log_run(void)
{
	int idx = sys_context.log_status_progress;
	bool ret = true;

	if (idx < 0 || idx >= sys_context.log_status_count)
		return;

	if (sys_context.log_status[idx].hook)
		ret = sys_context.log_status[idx].hook(sys_context.log_status[idx].user_context);

	if (ret)
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
	if (sys_context.has_temp)
		LOOP_FUNC_RUN("temperature", temperature_measure);
	if (sys_context.has_time)
		LOOP_FUNC_RUN("ntp", ntp_connect);
	LOOP_FUNC_RUN("log WD boot", log_wd_boot);
	if (sys_context.reconnect) {
		do_system_reconnect();
		sys_context.reconnect = false;
	}
	LOOP_FUNC_RUN("slog", system_log_run);
	sys_modules_run();
	if (sys_context.periodic_log_ms > 0) {
		static uint32_t llog;

		if ((sys_context.last_loop - llog) > sys_context.periodic_log_ms) {
			llog = sys_context.last_loop;
			LOOP_FUNC_RUN("syslog status", system_log_status);
		}
	}
}
