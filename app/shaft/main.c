// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "shaft.h"

#define MAINLOG	"main"
#define BLINK_INERVAL	3

#define TEMP_TXT_ROW	0
#define TEMP_NUM_ROW	1

static void internal_temp_query()
{
	static float int_temp;
	float temp;

	temp = temperature_internal_get();

	if (int_temp != temp) {
		int_temp = temp;
		lcd_set_double(3, TEMP_NUM_ROW, 9, (double)int_temp);
	}
	mqtt_data_internal_temp(int_temp);
}

static void internal_temp_init()
{
	lcd_set_text(2, TEMP_TXT_ROW, 11, (char*)"*C");
	lcd_set_text(3, TEMP_NUM_ROW, 11, (char*)"--");
}

int main()
{
	int blinik_count = 0;
	bool has_sonar;

	if (!system_common_init()) {
		printf("\n\rFailed to initialize the system\n\r");
		exit(1);
	}
	internal_temp_init();
	has_sonar = sonar_init();

	while(true){
		if (blinik_count++ % BLINK_INERVAL == 0) {
			LED_ON;
		}
		system_common_run();
		if (has_sonar)
			sonar_measure();
		internal_temp_query();
		LED_OFF;
		busy_wait_ms(100);
	}
}
