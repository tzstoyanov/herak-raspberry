// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "solar.h"

#define MAINLOG	"main"
#define BLINK_INERVAL	300

static void internal_temp_query(void)
{
	mqtt_data_internal_temp(temperature_internal_get());
}

int main(void)
{
	int blinik_count = 0;
	bool has_solar = false;
	bool has_bms = false;

	if (!system_common_init()) {
		printf("\n\rFailed to initialize the system\n\r");
		exit(1);
	}

	has_solar = mppt_solar_init();
	has_bms = bms_solar_init();
	mqtt_solar_init();

	while (true) {
		if (blinik_count++ % BLINK_INERVAL == 0)
			LED_ON;

		system_common_run();
		internal_temp_query();
		if (has_solar)
			mppt_solar_query();
		if (has_bms)
			bms_solar_query();
		wh_notify_send();
		LED_OFF;
	}
}
