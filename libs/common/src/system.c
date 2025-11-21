// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/aon_timer.h"
#include "pico/binary_info.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define COMMONSYSLOG	"system"
#define WATCHDOG_TIMEOUT_MS	8300 /* The maximum is 8388ms, which is approximately 8.3 seconds */

bi_decl(bi_program_version_string(SYS_VERSION_STR));
bi_decl(bi_program_build_date_string(SYS_BUILD_DATE));
bi_decl(bi_program_name(CYW43_HOST_NAME));
bi_decl(bi_program_description("Tzvetomir Stoyanov"));
bi_decl(bi_program_url("github.com/tzstoyanov/herak-raspberry"));

//#define MAIN_WAIT_MS	10
#ifdef MAIN_WAIT_MS
#define	BUSY_WAIT		busy_wait_ms(MAIN_WAIT_MS);
#else
#define	BUSY_WAIT
#endif
#define BLINK_INTERVAL	100
static struct {
	uint64_t reboot_time;
	bool reconnect;
	char *host_name;
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
	aon_timer_start_with_timeofday();

	return true;
}

bool system_common_init(void)
{
	// Initialize the serial port, default 38400 baud
	set_sys_clock_khz(120000, true);
	stdio_init_all();
	srand(to_us_since_boot(get_absolute_time()));
	busy_wait_ms(2000);

	watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

	hlog_info(COMMONSYSLOG, "Booting ... %d", watchdog_enable_caused_reboot());
	sys_state_log_version();
	hlog_info(COMMONSYSLOG, "[%s] RAM: %d total / %d free bytes",
			  PICO_PLATFORM_STR, get_total_heap(), get_free_heap());
	if (!base_init())
		return false;

	wd_update();
	LED_ON;

	sys_context.reboot_time = 0;

	wd_update();
	sys_modules_init();
	wd_update();
	sys_irq_init();
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
	sys_context.reboot_time = time_ms_since_boot();
	if (delay_ms > WATCHDOG_TIMEOUT_MS)
		sys_context.reboot_time += (delay_ms - WATCHDOG_TIMEOUT_MS);

	hlog_info(COMMONSYSLOG, "System is rebooting in %dms ...",
			  delay_ms > WATCHDOG_TIMEOUT_MS ? delay_ms : WATCHDOG_TIMEOUT_MS);
}

void wd_update(void)
{
	if (sys_context.reboot_time < 1)
		watchdog_update();

	if (sys_context.reboot_time > time_ms_since_boot())
		watchdog_update();
}

void system_common_run(void)
{
	LOOP_FUNC_RUN("log WD boot", log_wd_boot);
	if (sys_context.reconnect) {
		do_system_reconnect();
		sys_context.reconnect = false;
	}
	sys_modules_run();
}

char *system_get_hostname(void)
{
	if (sys_context.host_name)
		return sys_context.host_name;

	sys_context.host_name = USER_PRAM_GET(DEV_HOSTNAME);
	if (sys_context.host_name)
		return sys_context.host_name;

	return "pico";
}
