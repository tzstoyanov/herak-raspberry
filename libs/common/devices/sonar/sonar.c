// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define SONAR_MODULE		"sonar"
#define STARTUP_TIME_MSEC	3
#define TRIGGER_TIME_USEC	15
#define MAX_TIME_USEC		50000
#define MEASURE_TIME_MS		500
#define MQTT_DATA_LEN		64
#define MQTT_DELAY_MS		5000

/* For each measurements, take 30 samples */
#define SONAR_MEASURE_COUNT 30
/* Filter out the 5 biggest and the 5 smallest */
#define SONAR_MEASURE_DROP	5

struct sonar_context_t {
	sys_module_t mod;
	bool force;
	uint64_t mqtt_last_send;
	mqtt_component_t mqtt_comp;
	int echo_pin;
	int trigger_pin;
	uint32_t send_time;
	uint32_t last_distance;
	uint32_t samples[SONAR_MEASURE_COUNT];
	uint64_t last_measure;
	uint32_t debug;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int sonar_mqtt_data_send(struct sonar_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int ret = -1;

	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"distance\": \"%3.2f\"", (float)(ctx->last_distance/10));
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->mqtt_comp, ctx->mqtt_payload);

	if (!ret)
		ctx->mqtt_last_send = now;

	return ret;
}

static void sonar_mqtt_send(struct sonar_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();

	if (ctx->force) {
		ctx->mqtt_comp.force = true;
		ctx->force = false;
	}

	if (ctx->mqtt_comp.force) {
		sonar_mqtt_data_send(ctx);
		return;
	}

	if ((now - ctx->mqtt_last_send) < MQTT_DELAY_MS)
		return;

	sonar_mqtt_data_send(ctx);
}

static uint32_t sonar_read(struct sonar_context_t *ctx)
{
	absolute_time_t start, end, timeout;
	uint32_t duration_us, distance;

	gpio_put(ctx->trigger_pin, 0);
	busy_wait_ms(STARTUP_TIME_MSEC);
	gpio_put(ctx->trigger_pin, 1);
	busy_wait_us(TRIGGER_TIME_USEC);
	gpio_put(ctx->trigger_pin, 0);

	timeout = get_absolute_time();
	start = get_absolute_time();
	while (!gpio_get(ctx->echo_pin)) {
		start = get_absolute_time();
		if (absolute_time_diff_us(timeout, start) > MAX_TIME_USEC)
			return 0;
	}

	timeout = get_absolute_time();
	end = get_absolute_time();
	while (gpio_get(ctx->echo_pin)) {
		end = get_absolute_time();
		if (absolute_time_diff_us(timeout, end) > MAX_TIME_USEC)
			return 0;
	}

	duration_us = absolute_time_diff_us(start, end);
	distance = (duration_us * 17)/100;

	return distance;
}

static void sonar_measure(struct sonar_context_t *ctx)
{
	uint32_t av;
	int i;

	/* read the samples */
	for (i = 0; i < SONAR_MEASURE_COUNT; i++)
		ctx->samples[i] = sonar_read(ctx);
	/* filter biggest and smallest */
	av = samples_filter(ctx->samples, SONAR_MEASURE_COUNT, SONAR_MEASURE_DROP);

	if (av != ctx->last_distance) {
		ctx->force = true;
		ctx->last_distance = av;
	}

	ctx->last_measure = time_ms_since_boot();
}

static void sonar_run(void *context)
{
	struct sonar_context_t *ctx = (struct sonar_context_t *)context;
	uint64_t now = time_ms_since_boot();

	if ((now - ctx->last_measure) >= MEASURE_TIME_MS)
		sonar_measure(ctx);
	sonar_mqtt_send(ctx);
}

static void sonar_mqtt_init(struct sonar_context_t *ctx)
{
	ctx->mqtt_comp.module = SONAR_MODULE;
	ctx->mqtt_comp.platform = "sensor";
	ctx->mqtt_comp.dev_class = "distance";
	ctx->mqtt_comp.unit = "cm";
	ctx->mqtt_comp.value_template = "{{ value_json.distance }}";
	ctx->mqtt_comp.name = "sonar_sensor";
	mqtt_msg_component_register(&ctx->mqtt_comp);
}

static bool sonar_init(struct sonar_context_t **ctx)
{
	char *config = param_get(SONAR_CONFIG);
	char *config_tokens[2];
	char *rest, *tok;
	int i;

	if (!config || strlen(config) < 1)
		goto out_error;

	i = 0;
	rest = config;
	while (i < 2 && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i != 2)
		goto out_error;

	(*ctx) = (struct sonar_context_t *)calloc(1, sizeof(struct sonar_context_t));
	if (!(*ctx))
		goto out_error;

	(*ctx)->echo_pin = (int)strtol(config_tokens[0], NULL, 10);
	(*ctx)->trigger_pin = (int)strtol(config_tokens[1], NULL, 10);

	if ((*ctx)->echo_pin < 0 || (*ctx)->echo_pin > 40)
		goto out_error;
	if ((*ctx)->trigger_pin < 0 || (*ctx)->trigger_pin > 40)
		goto out_error;

	free(config);

	gpio_init((*ctx)->echo_pin);
	gpio_set_dir((*ctx)->echo_pin, GPIO_IN);
	gpio_put((*ctx)->echo_pin, 0);

	gpio_init((*ctx)->trigger_pin);
	gpio_set_dir((*ctx)->trigger_pin, GPIO_OUT);
	gpio_put((*ctx)->trigger_pin, 0);

	sonar_mqtt_init(*ctx);

	return true;

out_error:
	free(config);
	return false;
}

static bool sonar_log(void *context)
{
	struct sonar_context_t *ctx = (struct sonar_context_t *)context;

	hlog_info(SONAR_MODULE, "Last detected distance: %3.2fcm", ctx->last_distance/10);

	return true;
}

static void sonar_debug_set(uint32_t debug, void *context)
{
	struct sonar_context_t *ctx = (struct sonar_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void sonar_register(void)
{
	struct sonar_context_t *ctx = NULL;

	if (!sonar_init(&ctx))
		return;

	ctx->mod.name = SONAR_MODULE;
	ctx->mod.run = sonar_run;
	ctx->mod.log = sonar_log;
	ctx->mod.debug = sonar_debug_set;
	ctx->mod.commands.description = "Sonar AJ-SR04M";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
