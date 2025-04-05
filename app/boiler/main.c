// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdbool.h>
#include "boiler.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#define MAINLOG	"main"
#define BLINK_INERVAL	3

static void internal_temp_query(void)
{
	float temp;

	temp = temperature_internal_get();
	mqtt_data_internal_temp(temp);
}

static void boiler_data_init(opentherm_data_t *boiler)
{
	memset(boiler, 0, sizeof(opentherm_data_t));
	boiler->ch_enabled = false;
	boiler->dhw_enabled = false;
	boiler->ch2_enabled = false;
	boiler->cooling_enabled = false;
	boiler->otc_active = false;
	boiler->param_desired.ch_temperature_setpoint = 25.0;
	boiler->param_desired.dhw_temperature_setpoint = 40.0;
}

void opentherm_status_log(void *context)
{
	opentherm_context_t *boiler = (opentherm_context_t *)context;

	opentherm_pio_log(boiler);
	opentherm_cmd_log(boiler);
}


int main(void)
{
	opentherm_context_t boiler;
	int blinik_count = 0;
	bool has_boiler = false;

	if (!system_common_init()) {
		printf("\r\nFailed to initialize the system\r\n");
		exit(1);
	}

	boiler_data_init(&boiler.data);
	boiler_cmd_init(&boiler);
	opentherm_cmd_init(&boiler);
	if (!opentherm_pio_init(&boiler))
		has_boiler = true;
	mqtt_boiler_init(&boiler);
	add_status_callback(opentherm_status_log, &boiler);

	while (true) {
		if (blinik_count++ % BLINK_INERVAL == 0)
			LED_ON;
		system_common_run();
		if (has_boiler)
			opentherm_cmd_run(&boiler);
		wd_update();
		mqtt_boiler_send(&boiler);
		wd_update();
		internal_temp_query();
		LED_OFF;
		busy_wait_ms(100);
	}
}
