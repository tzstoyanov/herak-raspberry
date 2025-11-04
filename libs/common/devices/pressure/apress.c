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
#include "common_lib.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define APRESS_MODULE	"apress"
#define MAX_SENSORS_COUNT 4
#define MQTT_SEND_INTERVAL_MS 10000
#define MEASURE_INTERVAL_MS 5000
#define MQTT_DATA_LEN   128

struct apress_sensor_t {
	struct adc_sensor_t *adc;
	mqtt_component_t mqtt_comp;
};

struct apress_context_t {
	sys_module_t mod;
	uint32_t send_time;
	int sensors_count;
	struct apress_sensor_t sensors[MAX_SENSORS_COUNT];
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
	uint32_t debug;
	uint64_t last_run;
};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int apress_mqtt_sensor_send(struct apress_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int ret;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"id\": \"%d\"", idx);
	ADD_MQTT_MSG_VAR(",\"pressure\": \"%f\"", adc_sensor_get_value(ctx->sensors[idx].adc));
	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->sensors[idx].mqtt_comp, ctx->mqtt_payload);

	if (!ret)
		ctx->mqtt_last_send = now;
	return ret;
}

static void apress_mqtt_send(struct apress_context_t *ctx)
{
	static int idx;
	uint64_t now;
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		if (ctx->sensors[i].mqtt_comp.force) {
			apress_mqtt_sensor_send(ctx, i);
			return;
		}
	}

	now = time_ms_since_boot();
	if (ctx->mqtt_last_send &&
	    (now - ctx->mqtt_last_send) < MQTT_SEND_INTERVAL_MS)
		return;

	if (idx >= ctx->sensors_count)
		idx = 0;
	if (!apress_mqtt_sensor_send(ctx, idx))
		idx++;
}

static void apress_run(void *context)
{
	struct apress_context_t *ctx = (struct apress_context_t *)context;
	uint64_t now = time_ms_since_boot();
	int i;

	if ((now - ctx->last_run) < MEASURE_INTERVAL_MS)
		return;

	for (i = 0; i < ctx->sensors_count; i++) {
		if (adc_sensor_measure(ctx->sensors[i].adc))
			ctx->sensors[i].mqtt_comp.force = true;
	}

	apress_mqtt_send(ctx);
	ctx->last_run = now;
}

static bool apress_log(void *context)
{
	struct apress_context_t *ctx = (struct apress_context_t *)context;
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		hlog_info(APRESS_MODULE, "Sensor %d: pressure %f bars",
				  i, adc_sensor_get_value(ctx->sensors[i].adc));
	}

	return true;
}

static void apress_mqtt_init(struct apress_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		ctx->sensors[i].mqtt_comp.module = APRESS_MODULE;
		ctx->sensors[i].mqtt_comp.platform = "sensor";
		ctx->sensors[i].mqtt_comp.dev_class = "pressure";
		ctx->sensors[i].mqtt_comp.value_template = "{{ value_json.pressure }}";
		sys_asprintf(&ctx->sensors[i].mqtt_comp.name, "Pressure_%d", i);
		mqtt_msg_component_register(&ctx->sensors[i].mqtt_comp);
	}
}

static bool apress_config_get(struct apress_context_t **ctx)
{
	char *config_pins = param_get(APRESS_PIN);
	char *config_f = param_get(APRESS_CORR);
	char *rest, *tok, *rest1, *tok1;
	int pins[MAX_SENSORS_COUNT];
	int a[MAX_SENSORS_COUNT];
	int b[MAX_SENSORS_COUNT];
	int p, c, j;

	(*ctx) = NULL;
	if (!config_pins || strlen(config_pins) < 1)
		goto out;
	if (!config_f || strlen(config_f) < 1)
		goto out;

	(*ctx) = (struct apress_context_t *)calloc(1, sizeof(struct apress_context_t));
	if ((*ctx) == NULL)
		goto out;

	p = 0;
	rest = config_pins;
	while ((tok = strtok_r(rest, ";", &rest))) {
		pins[p++] = (int)strtol(tok, NULL, 0);
		if (p >= MAX_SENSORS_COUNT)
			break;
	}

	c = 0;
	rest = config_f;
	while ((tok = strtok_r(rest, ";", &rest))) {
		if (c >= MAX_SENSORS_COUNT)
			break;
		j = 0;
		rest1 = tok;
		while ((tok1 = strtok_r(rest1, ":", &rest1))) {
			if (j == 0) {
				a[c] = strtof(tok, NULL);
			} else if (j == 1) {
				b[c] = strtof(tok, NULL);
				c++;
				break;
			}
			j++;
		}
	}

	if (p && p == c) {
		for (j = 0; j < p; j++) {
			(*ctx)->sensors[j].adc = adc_sensor_init(pins[j], a[j], b[j]);
			if (!(*ctx)->sensors[j].adc)
				break;
		}

		if (j >= p)
			(*ctx)->sensors_count = p;
	}

out:
	free(config_pins);
	free(config_f);
	if ((*ctx) && (*ctx)->sensors_count < 1) {
		free(*ctx);
		(*ctx) = NULL;
	}

	return ((*ctx) ? (*ctx)->sensors_count > 0 : 0);
}

static bool apress_init(struct apress_context_t **ctx)
{
	if (!apress_config_get(ctx))
		return false;

	apress_mqtt_init(*ctx);

	hlog_info(APRESS_MODULE, "Initialise successfully %d  sensors",
			  (*ctx)->sensors_count);
	return true;
}

static void apress_debug_set(uint32_t debug, void *context)
{
	struct apress_context_t *ctx = (struct apress_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void apress_register(void)
{
	struct apress_context_t *ctx = NULL;

	if (!apress_init(&ctx))
		return;

	ctx->mod.name = APRESS_MODULE;
	ctx->mod.run = apress_run;
	ctx->mod.log = apress_log;
	ctx->mod.debug = apress_debug_set;
	ctx->mod.commands.description = "Pressure measure";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
