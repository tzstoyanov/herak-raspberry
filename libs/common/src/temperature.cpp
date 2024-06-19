// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "one_wire.h"

#include "base64.h"
#include "params.h"

#define SENSLOG				"sensor"
#define MAX_SENSORS			1
#define ADC_INTERNAL_TEMP	4
#define ADC_CONVERS			(3.28f / (1 << 12))

/* For each measurements, take 50 samples */
#define ADC_MEASURE_COUNT	50
/* Filter out the 5 biggest and the 5 smallest */
#define ADC_MEASURE_DROP	5

typedef struct {
	rom_address_t	address;
	uint64_t		address_int;
	float 			temperature;
}sensor;

struct {
	uint32_t		samples[ADC_MEASURE_COUNT];
	float 			temp_internal;
	One_wire		*tempSensor;
	int				count;
	int				pin;
	rom_address_t	null_address;
	sensor			all[MAX_SENSORS];
}static sensor_context;

static bool get_sensor_config()
{
	char *config = param_get(ONE_WIRE_DEVICES);
	bool ret = false;

	if (!config || strlen(config) < 1)
		goto out;

	sensor_context.pin = (int)strtol(config, NULL, 10);
	if (sensor_context.pin < 0 || sensor_context.pin >= 0xFFFF)
		goto out;

	ret = true;

out:
	free(config);
	return ret;
}

bool temperature_init()
{
	memset(&sensor_context, 0, sizeof(sensor_context));

	adc_init();
	adc_set_round_robin(0);
	adc_set_temp_sensor_enabled(true);

	if (get_sensor_config()) {
		sensor_context.tempSensor = new One_wire(sensor_context.pin);
		if (sensor_context.tempSensor) {
			sensor_context.tempSensor->init();
			gpio_init(sensor_context.pin);
			gpio_set_dir(sensor_context.pin, GPIO_IN);
			gpio_pull_up(sensor_context.pin);
		}
	}

	return true;
}

static void temperature_detect()
{
	int i;

	if (!sensor_context.tempSensor)
		return;

	if(!gpio_get(sensor_context.pin)) {
		if (sensor_context.count) {
			hlog_info(SENSLOG,"Temperature sensors disconnected from pin %d", sensor_context.pin);
			sensor_context.count = 0;
		}
		return;
	}
	if (sensor_context.count)
		return;
	sensor_context.count = sensor_context.tempSensor->find_and_count_devices_on_bus();
	hlog_info(SENSLOG,"Detected %d sensors on pin %d, supported %d", sensor_context.count, sensor_context.pin, MAX_SENSORS);
	if (sensor_context.count > MAX_SENSORS)
		sensor_context.count = MAX_SENSORS;
	for (i = 0; i < sensor_context.count; i++) {
		sensor_context.all[i].address = sensor_context.tempSensor->get_address(i);
		sensor_context.all[i].address_int = sensor_context.tempSensor->to_uint64(sensor_context.all[i].address);
	}
}

void temperature_measure_onewire()
{
	float temp;
	int i;

	if (!sensor_context.tempSensor)
		return;

	temperature_detect();
	if (!sensor_context.count)
		return;
	sensor_context.tempSensor->convert_temperature(sensor_context.null_address, true, true);
	for (i = 0; i < sensor_context.count; i++) {
		temp = sensor_context.tempSensor->temperature(sensor_context.all[i].address);
		hlog_info(SENSLOG,"External temperature %3.1f*C @ %016llX", temp, sensor_context.all[i].address_int);
		if (sensor_context.all[i].temperature != temp) {
			sensor_context.all[i].temperature = temp;
		}
	}
}

float temperature_internal_get()
{
	return sensor_context.temp_internal;
}

void temperature_measure_internal()
{
	float result_tmp;
	uint32_t av;
	int i;

	adc_select_input(ADC_INTERNAL_TEMP);
	/* read the samples */
	for(i = 0; i<ADC_MEASURE_COUNT; i++)
		sensor_context.samples[i] = adc_read();
	/* filter biggest and smallest */
	av = samples_filter(sensor_context.samples, ADC_MEASURE_COUNT, ADC_MEASURE_DROP);

	result_tmp = av * ADC_CONVERS;
	/* Formula from the Pico  C/C++ SDK Manual */
	sensor_context.temp_internal = 27 - (result_tmp - 0.706) / 0.001721;
}

void temperature_measure()
{
	temperature_measure_internal();
	temperature_measure_onewire();
}
