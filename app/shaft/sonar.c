// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "shaft.h"

#define AJLOG	"sonar"
#define STARTUP_TIME_MSEC	3
#define TRIGGER_TIME_USEC	15
#define MAX_TIME_USEC		50000

/* For each measurements, take 30 samples */
#define SONAR_MEASURE_COUNT 30
/* Filter out the 5 biggest and the 5 smallest */
#define SONAR_MEASURE_DROP	5

#define SONAR_TXT_ROW	0
#define SONAR_NUM_ROW	1

struct {
	int echo_pin;
	int trigger_pin;
	uint32_t send_time;
	uint32_t last_distance;
	uint32_t samples[SONAR_MEASURE_COUNT];
}static sonar_context;

static uint32_t sonar_read()
{
	absolute_time_t start, end, timeout;
	uint32_t duration_us, distance;

	gpio_put(sonar_context.trigger_pin, 0);
	busy_wait_ms(STARTUP_TIME_MSEC);
	gpio_put(sonar_context.trigger_pin, 1);
	busy_wait_us(TRIGGER_TIME_USEC);
	gpio_put(sonar_context.trigger_pin, 0);

	timeout = get_absolute_time();
	while(!gpio_get(sonar_context.echo_pin)) {
		start = get_absolute_time();
		if (absolute_time_diff_us(timeout, start) > MAX_TIME_USEC)
			return 0;
	}

	timeout = get_absolute_time();
	while(gpio_get(sonar_context.echo_pin)) {
		end = get_absolute_time();
		if (absolute_time_diff_us(timeout, end) > MAX_TIME_USEC)
			return 0;
	}

	duration_us = absolute_time_diff_us(start, end);
	distance = (duration_us * 17)/100;

	return distance;
}

void sonar_measure(void)
{
	uint32_t av;
	int i;

	/* read the samples */
	for (i = 0; i < SONAR_MEASURE_COUNT; i++)
		sonar_context.samples[i] = sonar_read();
	/* filter biggest and smallest */
	av = samples_filter(sonar_context.samples, SONAR_MEASURE_COUNT, SONAR_MEASURE_DROP);

	if (av != sonar_context.last_distance) {
		sonar_context.last_distance = av;
		lcd_set_double(1, SONAR_NUM_ROW, 1, (double)av/10);
	}
	mqtt_data_sonar((float)sonar_context.last_distance/10);

	return;

err:
	lcd_set_text(1, SONAR_TXT_ROW, 3, "--");
	mqtt_data_sonar(-1);
}

bool sonar_init(void)
{
	char *config = param_get(SONAR_CONFIG);
	char *config_tokens[2];
	char *rest, *tok;
	int i;

	memset(&sonar_context, 0, sizeof(sonar_context));
	if (strlen(config) < 1)
		goto out_error;

	i = 0;
	rest = config;
	while (i < 2 && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i != 2)
		goto out_error;

	sonar_context.echo_pin = (int)strtol(config_tokens[0], NULL, 10);
	sonar_context.trigger_pin = (int)strtol(config_tokens[1], NULL, 10);

	if (sonar_context.echo_pin < 0 || sonar_context.echo_pin > 40)
		goto out_error;
	if (sonar_context.trigger_pin < 0 || sonar_context.trigger_pin > 40)
		goto out_error;

	free(config);

	gpio_init(sonar_context.echo_pin);
	gpio_set_dir(sonar_context.echo_pin, GPIO_IN);
	gpio_put(sonar_context.echo_pin, 0);

	gpio_init(sonar_context.trigger_pin);
	gpio_set_dir(sonar_context.trigger_pin, GPIO_OUT);
	gpio_put(sonar_context.trigger_pin, 0);

	lcd_set_text(0, SONAR_TXT_ROW, 3, "cm");
	lcd_set_text(1, SONAR_NUM_ROW, 3, "--");

	hlog_info(AJLOG,"Sensor AJ-SR04M initialized");

	return true;

out_error:
	free(config);
	sonar_context.echo_pin = -1;
	sonar_context.trigger_pin = -1;
	hlog_info(AJLOG,"No valid configuration for sensor AJ-SR04M");
	return false;
}
