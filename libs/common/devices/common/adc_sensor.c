// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"

#include "herak_sys.h"
#include "common_internal.h"

#define MAX_ANALOG_VALUE	4095

#define ADC_REF_VOLT	3.3f
#define ADC_MAX			(1 << 12)
#define ADC_CONVERS(V)		(((float)(V))*(ADC_REF_VOLT / ((float)ADC_MAX)))

/* For each measurements, take 30 samples */
#define MEASURE_COUNT 30
/* Filter out the 5 biggest and the 5 smallest */
#define MEASURE_DROP	5

static struct {
	int gp_id;
	int adc_id;
} adc_mapping[] = {
		{26, 0},
		{27, 1},
		{28, 2},
		{29, 3},
		{-1, 4},	// Input 4 is the onboard temperature sensor.
};

struct adc_sensor_t {
	int pin;
	unsigned int adc_id;
	double a;
	double b;
	uint32_t samples[MEASURE_COUNT];
	float value;
	float volt;
	float percent;
	uint32_t raw;
};

static void adc_sys_init(void)
{
	static bool init;

	if (init)
		return;
	adc_init();
	adc_set_round_robin(0);
	adc_irq_set_enabled(false);
	adc_run(false);
	adc_fifo_drain();

	init = true;
}

struct adc_sensor_t *adc_sensor_init(int pin, double a, double b)
{
	struct adc_sensor_t sensor, *s;
	unsigned int i;

	memset(&sensor, 0, sizeof(struct adc_sensor_t));
	sensor.pin = pin;
	sensor.a = a;
	sensor.b = b;

	for (i = 0; i < ARRAY_SIZE(adc_mapping); i++) {
		if (adc_mapping[i].gp_id == pin) {
			sensor.pin = pin;
			sensor.adc_id = adc_mapping[i].adc_id;
			break;
		}
	}

	if (i >= ARRAY_SIZE(adc_mapping))
		return NULL;
	s = calloc(1, sizeof(struct adc_sensor_t));
	if (!s)
		return NULL;
	memcpy(s, &sensor, sizeof(struct adc_sensor_t));
	adc_sys_init();
	adc_gpio_init(pin);
	return s;
}

bool adc_sensor_measure(struct adc_sensor_t *sensor)
{
	bool ret = false;
	uint32_t av;
	double val;
	int p, i;

	if (!sensor)
		return false;

	adc_select_input(sensor->adc_id);
	if (adc_get_selected_input() != sensor->adc_id)
		return false;
	adc_fifo_drain();
	sleep_us(20);
	adc_read();
	sleep_us(100);

	/* read the samples */
	for (i = 0; i < MEASURE_COUNT; i++) {
		sensor->samples[i] = adc_read();
		sleep_us(20);
	}

	/* filter biggest and smallest */
	av = samples_filter(sensor->samples, MEASURE_COUNT, MEASURE_DROP);

	if (sensor->raw != av)
		sensor->raw = av;
	val = ADC_CONVERS(av);
	if (sensor->volt != val) {
		sensor->volt = val;
		ret = true;
	}

	val = sensor->a + (av * sensor->b);
	if (sensor->value != val) {
		sensor->value = val;
		ret = true;
	}

	p = sys_value_to_percent(0, MAX_ANALOG_VALUE, av);
	if (sensor->percent != p) {
		sensor->percent = p;
		ret = true;
	}

	return ret;
}

uint32_t adc_sensor_get_raw(struct adc_sensor_t *sensor)
{
	if (sensor)
		return sensor->raw;
	return 0;
}

float adc_sensor_get_value(struct adc_sensor_t *sensor)
{
	if (sensor)
		return sensor->value;
	return 0;
}

float adc_sensor_get_volt(struct adc_sensor_t *sensor)
{
	if (sensor)
		return sensor->volt;
	return 0;
}

int adc_sensor_get_percent(struct adc_sensor_t *sensor)
{
	if (sensor)
		return sensor->percent;
	return -1;
}
