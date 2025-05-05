// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"

#include "common_lib.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define SSR_MODULE	"ssr"
#define MAX_SSR_COUNT GPIO_PIN_MAX+1
#define MQTT_DELAY_MS 1000

#define MQTT_DATA_LEN   128

enum {
	SSR_MQTT_SENSOR_STATE = 0,
	SSR_MQTT_SENSOR_TIME,
	SSR_MQTT_SENSOR_DELAY,
	SSR_MQTT_SENSOR_MAX
};

struct ssr_t {
	int gpio_pin;
	bool state_desired;
	bool state_actual;
	uint64_t last_switch;
	uint32_t time_ms;
	int time_remain_ms;
	uint32_t delay_ms;
	int drelay_remain_ms;
	mqtt_component_t mqtt_comp[SSR_MQTT_SENSOR_MAX];
};

struct ssr_context_t {
	sys_module_t mod;
	int count;
	uint8_t on_state;
	uint32_t state;
	struct ssr_t *relays[MAX_SSR_COUNT];
	uint32_t debug;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int ssr_mqtt_data_send(struct ssr_context_t *ctx, int idx, int sens)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	struct ssr_t *relay;
	int count = 0;

	if (idx < 0 || idx >= MAX_SSR_COUNT || !(ctx->relays[idx]))
		return -1;
	if (sens < 0 || sens >= SSR_MQTT_SENSOR_MAX)
		return -1;

	relay = ctx->relays[idx];
	if (!relay->mqtt_comp[sens].force && (relay->mqtt_comp[sens].last_send && ((now - relay->mqtt_comp[sens].last_send) < MQTT_DELAY_MS)))
		return -1;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR))
	ADD_MQTT_MSG_VAR(",\"ssr_id\": \"%d\"", idx);
	ADD_MQTT_MSG_VAR(",\"ssr_state\": \"%d\"", relay->state_actual);
	ADD_MQTT_MSG_VAR(",\"run_time\": \"%d\"", relay->time_remain_ms/1000);
	ADD_MQTT_MSG_VAR(",\"delay\": \"%d\"", relay->drelay_remain_ms/1000);
	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	return mqtt_msg_component_publish(&relay->mqtt_comp[sens], ctx->mqtt_payload);
}

static void ssr_mqtt_send(struct ssr_context_t *ctx)
{
	static int midx;

	if (!mqtt_discovery_sent())
		return;

	while (true) {
		if (midx >= MAX_SSR_COUNT)
			midx = 0;
		if (ctx->relays[midx]) {
			ssr_mqtt_data_send(ctx, midx, SSR_MQTT_SENSOR_STATE);
			if (!ctx->relays[midx]->mqtt_comp[SSR_MQTT_SENSOR_STATE].force)
				midx++;
			return;
		}
		midx++;
	}
}

static void ssr_reset_all(struct ssr_context_t *ssr_ctx)
{
	int off = !ssr_ctx->on_state;
	int i;

	hlog_info(SSR_MODULE, "Going to execute command state reset");

	for (i = 0; i < MAX_SSR_COUNT; i++) {
		if (!(ssr_ctx->relays[i]))
			continue;
		gpio_put(ssr_ctx->relays[i]->gpio_pin, off);
		ssr_ctx->relays[i]->state_actual = off;
		ssr_ctx->relays[i]->state_desired = off;
		ssr_ctx->relays[i]->time_ms = 0;
		ssr_ctx->relays[i]->delay_ms = 0;
		ssr_ctx->relays[i]->last_switch = time_ms_since_boot();
	}
}

static int ssr_state_set(struct ssr_context_t *context, uint8_t id, bool state, uint32_t time, uint32_t delay)
{
	int value = state ? context->on_state : !context->on_state;

	if (id >= MAX_SSR_COUNT || !(context->relays[id]))
		return -1;
	if (!delay) {
		gpio_put(context->relays[id]->gpio_pin, value);
		if (context->relays[id]->state_actual != value)
			context->relays[id]->mqtt_comp[SSR_MQTT_SENSOR_STATE].force = true;
		context->relays[id]->state_actual = value;
	}
	if (context->relays[id]->state_desired != value)
		context->relays[id]->mqtt_comp[SSR_MQTT_SENSOR_STATE].force = true;
	context->relays[id]->state_desired = value;
	context->relays[id]->time_ms = time;
	context->relays[id]->delay_ms = delay;
	context->relays[id]->last_switch = time_ms_since_boot();

	return 0;
}

static void ssr_state_remain_times(struct ssr_context_t *context, uint8_t id, int time, int delay)
{
	if (id >= MAX_SSR_COUNT || !(context->relays[id]))
		return;
	if (time < 0)
		time = 0;
	if (delay < 0)
		delay = 0;
	if (context->relays[id]->time_remain_ms != time)
		context->relays[id]->mqtt_comp[SSR_MQTT_SENSOR_STATE].force = true;
	if (context->relays[id]->drelay_remain_ms != delay)
		context->relays[id]->mqtt_comp[SSR_MQTT_SENSOR_STATE].force = true;

	context->relays[id]->time_remain_ms = time;
	context->relays[id]->drelay_remain_ms = delay;
}

static bool ssr_log(void *context)
{
	struct ssr_context_t *ctx = (struct ssr_context_t *)context;
	int i;

	UNUSED(context);

	for (i = 0; i < MAX_SSR_COUNT; i++) {
		if (!(ctx->relays[i]))
			continue;
		hlog_info(SSR_MODULE, "Relay %d: gpio %d [%s/%s]; delay: %lu/%lu sec, time %lu/%lu sec",
				  i, ctx->relays[i]->gpio_pin, (ctx->relays[i]->state_desired)?"ON":"OFF",
				  (ctx->relays[i]->state_actual)?"ON":"OFF",
				  ctx->relays[i]->drelay_remain_ms/1000, ctx->relays[i]->delay_ms/1000,
				  ctx->relays[i]->time_remain_ms/1000, ctx->relays[i]->time_ms/1000);
	}

	return true;
}

static void ssr_run(void *context)
{
	struct ssr_context_t *ctx = (struct ssr_context_t *)context;
	int delta_t, delta_d;
	uint64_t now;
	int i;

	now = time_ms_since_boot();
	for (i = 0; i < MAX_SSR_COUNT; i++) {
		if (!(ctx->relays[i]))
			continue;
		delta_t = ctx->relays[i]->time_ms;
		delta_d = ctx->relays[i]->delay_ms;
		if (ctx->relays[i]->delay_ms > 0) {
			delta_d = now - ctx->relays[i]->last_switch;
			delta_d = ctx->relays[i]->delay_ms - delta_d;
			if (delta_d <= 0)
				ssr_state_set(ctx, i, ctx->relays[i]->state_desired, ctx->relays[i]->time_ms, 0);
		} else if (ctx->relays[i]->time_ms > 0) {
			delta_t = now - ctx->relays[i]->last_switch;
			delta_t = ctx->relays[i]->time_ms - delta_t;
			if (delta_t <= 0)
				ssr_state_set(ctx, i, !ctx->relays[i]->state_desired, 0, 0);
		}
		ssr_state_remain_times(ctx, i, delta_t, delta_d);
	}

	ssr_mqtt_send(ctx);
}

static int ssr_mqtt_components_add(struct ssr_context_t *ctx)
{
	int i;

	for (i = 0; i < MAX_SSR_COUNT; i++) {
		if (!(ctx->relays[i]))
			continue;
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].module = SSR_MODULE;
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].platform = "binary_sensor";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].payload_on = "1";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].payload_off = "0";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].value_template = "{{ value_json.ssr_state }}";
		sys_asprintf(&ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].name, "Relay_%d", i);
		mqtt_msg_component_register(&(ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE]));

		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].module = SSR_MODULE;
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].platform = "sensor";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].dev_class = "duration";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].unit= "s";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].value_template = "{{ value_json.run_time }}";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].state_topic =
					ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].state_topic;
		sys_asprintf(&ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].name, "Relay_%d_run_time", i);
		mqtt_msg_component_register(&(ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME]));
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_TIME].force = false;

		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].module = SSR_MODULE;
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].platform = "sensor";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].dev_class = "duration";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].unit= "s";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].value_template = "{{ value_json.delay }}";
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].state_topic =
					ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_STATE].state_topic;
		sys_asprintf(&ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].name, "Relay_%d_delay", i);
		mqtt_msg_component_register(&(ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY]));
		ctx->relays[i]->mqtt_comp[SSR_MQTT_SENSOR_DELAY].force = false;
	}

	return 0;
}

static bool ssr_config_get(struct ssr_context_t **ctx)
{
	char *config = param_get(SSR);
	char *rest1, *tok1;
	char *tok_map[2];
	char *rest, *tok;
	int id;
	int i;

	(*ctx) = NULL;
	if (!config || strlen(config) < 1)
		goto out_error;

	(*ctx) = calloc(1, sizeof(struct ssr_context_t));
	if ((*ctx) == NULL)
		goto out_error;
	for (i = 0 ; i < MAX_SSR_COUNT; i++)
		(*ctx)->relays[i]->gpio_pin = -1;
	tok = param_get(SSR_TRIGGER);
	if (tok && strlen(tok) >= 1)
		(*ctx)->on_state = (int)strtol(tok, NULL, 10);
	free(tok);

	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		i = 0;
		rest1 = tok;
		while (i < 2 && (tok1 = strtok_r(rest1, ":", &rest1)))
			tok_map[i++] = tok;
		if (i < 2)
			continue;
		id = (int)strtol(tok_map[0], NULL, 10);
		if (id < 0 || id >= MAX_SSR_COUNT)
			continue;
		if (!(*ctx)->relays[id])
			(*ctx)->relays[id] = calloc(1, sizeof(struct ssr_t));
		if (!(*ctx)->relays[id])
			goto out_error;
		(*ctx)->relays[id]->gpio_pin = (int)strtol(tok_map[1], NULL, 0);
		(*ctx)->count++;
	}
	if ((*ctx)->count < 1)
		goto out_error;

	free(config);
	return true;

out_error:
	free(config);
	free((*ctx));
	(*ctx) = NULL;
	return false;
}

static bool ssr_init(struct ssr_context_t **ctx)
{
	int i;

	if (!ssr_config_get(ctx))
		return false;

	for (i = 0; i < MAX_SSR_COUNT; i++) {
		if (!((*ctx)->relays[i]))
			continue;
		gpio_init((*ctx)->relays[i]->gpio_pin);
		gpio_set_dir((*ctx)->relays[i]->gpio_pin, GPIO_OUT);
		gpio_put((*ctx)->relays[i]->gpio_pin, !(*ctx)->on_state);
	}
	ssr_mqtt_components_add((*ctx));
	hlog_info(SSR_MODULE, "Initialise successfully %d relays", (*ctx)->count);

	return true;
}

static int cmd_ssr_set_state(char *cmd, char *params, struct ssr_context_t *context)
{
	int id, state, time, delay;
	char *tok, *rest = params;

	if (!params)
		return -1;

	hlog_info(SSR_MODULE, "Going to execute command [%s] with params [%s]", cmd, params);

	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		return -1;
	id = (int)strtol(tok, NULL, 10);
	if (id < 0)
		return -1;

	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		return -1;
	state = (int)strtol(tok, NULL, 10);

	/* Get state time */
	time = 0;
	delay = 0;
	tok = strtok_r(rest, ":", &rest);
	if (tok) {
		time = (int)strtol(tok, NULL, 10);
		if (time < 0)
			time = 0;
		/* sec -> ms */
		time *= 1000;

		/* Get delay */
		tok = strtok_r(rest, ":", &rest);
		if (tok) {
			delay = (int)strtol(tok, NULL, 10);
			if (delay < 0)
				delay = 0;
			/* sec -> ms */
			delay *= 1000;
		}
	}

	return ssr_state_set(context, id, state, time, delay);
}

#define SET_OK_STR "\tSSR switched.\r\n"
#define SET_ERR_STR "\tInvalid parameters.\r\n"
static int cmd_ssr_set(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ssr_context_t *ssr_ctx = (struct ssr_context_t *)user_data;

	if (strlen(params) < 2 || params[0] != ':')
		goto out_err;

	if (cmd_ssr_set_state(cmd, params, ssr_ctx))
		goto out_err;

	WEB_CLIENT_REPLY(ctx, SET_OK_STR);
	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, SET_ERR_STR);
	return -1;
}

static int cmd_ssr_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ssr_context_t *ssr_ctx = (struct ssr_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);

	ssr_reset_all(ssr_ctx);

	return 0;
}

static app_command_t ssr_requests[] = {
	{"set", ":<ssr_id>:<state_0_1>:<state_time_sec>:<delay_sec>", cmd_ssr_set},
	{"reset", NULL, cmd_ssr_reset}
};

static void ssr_debug_set(uint32_t debug, void *context)
{
	struct ssr_context_t *ctx = (struct ssr_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void ssr_register(void)
{
	struct ssr_context_t *ctx = NULL;

	if (!ssr_init(&ctx))
		return;

	ctx->mod.name = SSR_MODULE;
	ctx->mod.run = ssr_run;
	ctx->mod.log = ssr_log;
	ctx->mod.debug = ssr_debug_set;
	ctx->mod.commands.hooks = ssr_requests;
	ctx->mod.commands.count = ARRAY_SIZE(ssr_requests);
	ctx->mod.commands.description = "SSR control";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
