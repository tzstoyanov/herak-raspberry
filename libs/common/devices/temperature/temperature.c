// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "base64.h"
#include "params.h"

#define ADC_INTERNAL_TEMP	4
#define ADC_CONVERS			(3.28f / (1 << 12))

/* For each measurements, take 50 samples */
#define ADC_MEASURE_COUNT	50
/* Filter out the 5 biggest and the 5 smallest */
#define ADC_MEASURE_DROP	5

static struct {
	uint32_t		samples[ADC_MEASURE_COUNT];
	float 			temp_internal;
	int				count;
	int				pin;
} sensor_context;

bool temperature_init()
{
	memset(&sensor_context, 0, sizeof(sensor_context));

	adc_init();
	adc_set_round_robin(0);
	adc_set_temp_sensor_enabled(true);

	return true;
}

float temperature_internal_get()
{
	return sensor_context.temp_internal;
}

void temperature_measure()
{
	float result_tmp;
	uint32_t av;
	int i;

	adc_select_input(ADC_INTERNAL_TEMP);
	/* read the samples */
	for(i = 0; i < ADC_MEASURE_COUNT; i++)
		sensor_context.samples[i] = adc_read();
	/* filter biggest and smallest */
	av = samples_filter(sensor_context.samples, ADC_MEASURE_COUNT, ADC_MEASURE_DROP);

	result_tmp = av * ADC_CONVERS;
	/* Formula from the Pico  C/C++ SDK Manual */
	sensor_context.temp_internal = 27 - (result_tmp - 0.706) / 0.001721;
}
