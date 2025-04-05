// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "irrig.h"

#define MAINLOG	"main"
#define BLINK_INERVAL	3

static void internal_temp_query(void)
{
	float temp;

	temp = temperature_internal_get();
	mqtt_data_internal_temp(temp);
}

int main(void)
{
	int blinik_count = 0;
	int soil_count;
	int ssr_count;

	if (!system_common_init()) {
		printf("\r\nFailed to initialize the system\r\n");
		exit(1);
	}
	soil_count = soil_init();
	ssr_count = ssr_init();
	mqtt_irrig_init(soil_count, ssr_count);
	cmd_irrig_init();

	while (true) {
		if (blinik_count++ % BLINK_INERVAL == 0)
			LED_ON;
		system_common_run();
		if (soil_count)
			soil_measure();
		if (ssr_count)
			ssr_run();
		mqtt_irrig_send();
		internal_temp_query();
		LED_OFF;
		busy_wait_ms(100);
	}
}
