// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define THERMOSTAT_MODULE	"thermostat"
#define MQTT_DATA_LEN			256
#define MEASURE_INTERVAL_MS		500
#define MQTT_SEND_INTERVAL_MS	60000
#define MAX_THERMOSTAT_DEVICES	10

#define IS_DEBUG(C)	((C)->debug)

enum {
	THERM_TEMP_ONEWIRE = 0,
	THERM_TEMP_SHT20,
	THERM_TEMP_NTC,
};

enum {
	THERM_MQTT_STATE	= 0,
	THERM_MQTT_VALVE,
	THERM_MQTT_TEMPERATURE,
	THERM_MQTT_TEMP_ON,
	THERM_MQTT_TEMP_OFF,
	THERM_MQTT_MAX
};

struct therm_device_t {
	bool	enable;
	uint8_t ssr_id;
	bool	ssr_state;
	float	off_t;
	float	on_t;
	float	current_t;
	int		temp_provider;
	uint16_t	temp_id;
	mqtt_component_t mqtt_comp[THERM_MQTT_MAX];
};

struct thermostat_context_t {
	sys_module_t mod;
	uint32_t debug;
	uint8_t dev_count;
	uint64_t last_run;
	uint64_t mqtt_last_send;
	struct therm_device_t *devices[MAX_THERMOSTAT_DEVICES];
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int therm_mqtt_dvice_send(struct thermostat_context_t *ctx, int idx)
{
	struct therm_device_t *dev = ctx->devices[idx];
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	bool valve = false;
	int count = 0;

	if (!dev)
		return -1;
	ssr_api_state_get(dev->ssr_id, &valve, NULL, NULL);

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"id\": \"%d\"", idx);
	ADD_MQTT_MSG_VAR(",\"state\": \"%d\"", dev->enable);
	ADD_MQTT_MSG_VAR(",\"valve\": \"%d\"", valve);
	ADD_MQTT_MSG_VAR(",\"temperature\": \"%3.2f\"", dev->current_t);
	ADD_MQTT_MSG_VAR(",\"t_on\": \"%3.2f\"", dev->on_t);
	ADD_MQTT_MSG_VAR(",\"t_off\": \"%3.2f\"", dev->off_t);
	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;

	return mqtt_msg_component_publish(&ctx->devices[idx]->mqtt_comp[THERM_MQTT_STATE], ctx->mqtt_payload);
}

static void therm_mqtt_send(struct thermostat_context_t *ctx)
{
	static int idx;
	uint64_t now;
	int i;

	if (idx >= ctx->dev_count)
		idx = 0;
	for (; idx < ctx->dev_count; idx++) {
		if (!ctx->devices[idx])
			continue;
		if (ctx->devices[idx]->mqtt_comp[THERM_MQTT_STATE].force) {
			therm_mqtt_dvice_send(ctx, idx++);
			return;
		}
	}

	now = time_ms_since_boot();
	if (ctx->mqtt_last_send &&
	    (now - ctx->mqtt_last_send) < MQTT_SEND_INTERVAL_MS)
		return;
	for (i = 0; i < ctx->dev_count; i++) {
		if (!ctx->devices[i])
			continue;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].force = true;
	}
	ctx->mqtt_last_send = now;
}

static int therm_device_read_temperature(struct therm_device_t *dev)
{
	int ret = -1;

	switch (dev->temp_provider) {
	case THERM_TEMP_ONEWIRE:
#ifdef HAVE_ONE_WIRE
		ret = one_wire_get_sensor_data((dev->temp_id >> 8) & 0xFF, dev->temp_id & 0xFF,
										&dev->current_t);
#endif /* HAVE_ONE_WIRE */
		break;
	case THERM_TEMP_SHT20:
#ifdef HAVE_SHT20
		ret = sht20_get_data(dev->temp_id, &dev->current_t, NULL, NULL, NULL);
#endif /* HAVE_SHT20 */
		break;
	case THERM_TEMP_NTC:
#ifdef HAVE_TEMPERATURE
		ret = temperature_get_data(TEMPERATURE_TYPE_NTC, dev->temp_id, &dev->current_t);
#endif /* HAVE_TEMPERATURE */
		break;
	}
	return ret;
}

static int therm_device_run(struct thermostat_context_t *ctx, int idx)
{
	struct therm_device_t *dev = NULL;
	bool state;
	int ret;

	if (idx < MAX_THERMOSTAT_DEVICES)
		dev = ctx->devices[idx];
	if (!dev)
		return -1;
	state = dev->ssr_state;
	if (dev->enable) {
		ret = therm_device_read_temperature(dev);
		if (ret)
			return ret;
		if (dev->current_t >= dev->off_t)
			state = false;
		else if (dev->current_t <= dev->on_t)
			state = true;
	} else {
		state = false;
	}
#ifdef HAVE_SSR
	ret = ssr_api_state_set(dev->ssr_id, state, 0, 0);
#else
	ret = -1;
#endif /* HAVE_SSR */
	if (!ret && state != dev->ssr_state) {
		dev->ssr_state = state;
		dev->mqtt_comp[THERM_MQTT_STATE].force = true;
		if (IS_DEBUG(ctx))
			hlog_info(THERMOSTAT_MODULE, "Switched %s %d, current temperature is %f",
					  state ? "ON" : "OFF", idx, dev->current_t);
	}

	return ret;
}

static void therm_run(void *context)
{
	struct thermostat_context_t *ctx = (struct thermostat_context_t *)context;
	uint64_t now = time_ms_since_boot();
	int i;

	therm_mqtt_send(ctx);
	if ((now - ctx->last_run) < MEASURE_INTERVAL_MS)
		return;

	for (i = 0; i < ctx->dev_count; i++) {
		if (ctx->devices[i])
			therm_device_run(ctx, i);
	}

	ctx->last_run = now;
}

static bool therm_log(void *context)
{
	struct thermostat_context_t *ctx = (struct thermostat_context_t *)context;
	int i;

	for (i = 0; i < ctx->dev_count; i++) {
		hlog_info(THERMOSTAT_MODULE, "Thermostat %d: %s",
				  i, ctx->devices[i]->enable ? "enabled" : "disabled");
		if (!ctx->devices[i]->enable)
			continue;
		hlog_info(THERMOSTAT_MODULE, "\tSensor 0x%04X (%s); Temperatures: On %3.2f, Off %3.2f, currect %3.2f; SSR%d is %s",
				  ctx->devices[i]->temp_id, ctx->devices[i]->temp_provider == THERM_TEMP_ONEWIRE ? "OneWire" :
				  (ctx->devices[i]->temp_provider == THERM_TEMP_SHT20 ? "SHT20" : "NTC"),
				  ctx->devices[i]->on_t, ctx->devices[i]->off_t,
				  ctx->devices[i]->current_t, ctx->devices[i]->ssr_id,
				  ctx->devices[i]->ssr_state ? "On" : "Off");
	}

	return true;
}

static void therm_mqtt_init(struct thermostat_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->dev_count; i++) {
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].module = THERMOSTAT_MODULE;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].platform = "binary_sensor";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].payload_on = "1";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].payload_off = "0";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].value_template = "{{ value_json['state'] }}";
		sys_asprintf(&ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].name, "Thermostat_%d", i);
		mqtt_msg_component_register(&(ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE]));

		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].module = THERMOSTAT_MODULE;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].platform = "binary_sensor";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].payload_on = "1";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].payload_off = "0";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].value_template = "{{ value_json['valve'] }}";
		sys_asprintf(&ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].name, "Valve_%d", i);
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].state_topic =
					ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].state_topic;
		mqtt_msg_component_register(&(ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE]));
		ctx->devices[i]->mqtt_comp[THERM_MQTT_VALVE].force = false;

		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].module = THERMOSTAT_MODULE;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].platform = "sensor";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].dev_class = "temperature";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].unit = "°C";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].value_template = "{{ value_json['temperature'] }}";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].state_topic =
					ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].state_topic;
		sys_asprintf(&ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].name, "Temperature_%d", i);
		mqtt_msg_component_register(&(ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE]));
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMPERATURE].force = false;

		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].module = THERMOSTAT_MODULE;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].platform = "sensor";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].dev_class = "temperature";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].unit = "°C";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].value_template = "{{ value_json['t_on'] }}";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].state_topic =
					ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].state_topic;
		sys_asprintf(&ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].name, "T_On_%d", i);
		mqtt_msg_component_register(&(ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON]));
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_ON].force = false;

		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].module = THERMOSTAT_MODULE;
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].platform = "sensor";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].dev_class = "temperature";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].unit = "°C";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].value_template = "{{ value_json['t_off'] }}";
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].state_topic =
					ctx->devices[i]->mqtt_comp[THERM_MQTT_STATE].state_topic;
		sys_asprintf(&ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].name, "T_Off_%d", i);
		mqtt_msg_component_register(&(ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF]));
		ctx->devices[i]->mqtt_comp[THERM_MQTT_TEMP_OFF].force = false;
	}
}

#define THERM_ON_STR	"on"
static bool therm_config_get(struct thermostat_context_t **ctx)
{
	char *config_defaults = param_get(THERMOSTAT_DEF);
	char *config_therm = param_get(THERMOSTAT);
	char *rest, *tok, *rest1, *tok1, *rest2, *tok2;
	struct therm_device_t dev;
	int p, c;
	float f;

	(*ctx) = NULL;
	if (!config_therm || strlen(config_therm) < 1)
		goto out;

	(*ctx) = (struct thermostat_context_t *)calloc(1, sizeof(struct thermostat_context_t));
	if ((*ctx) == NULL)
		goto out;

	c = 0;
	rest = config_therm;
	while ((tok = strtok_r(rest, ";", &rest))) {
		if (c >= MAX_THERMOSTAT_DEVICES)
			break;
		memset(&dev, 0, sizeof(dev));
		rest1 = tok;
		tok1 = strtok_r(rest1, ":", &rest1);
		if (!tok1 || !rest1)
			continue;
		dev.ssr_id = (int)strtol(tok1, NULL, 10);
#ifndef HAVE_SSR
		hlog_info(THERMOSTAT_MODULE, "SSRs are not enabled.");
#endif /* HAVE_SSR */
		rest2 = rest1;
		tok2 = strtok_r(rest2, "-", &rest2);
		if (!tok2 || !rest2)
			continue;

		p = strlen(tok2);
		if (p == strlen("one_wire") && !strncmp(tok2, "one_wire", p)) {
#ifdef HAVE_ONE_WIRE
			dev.temp_provider = THERM_TEMP_ONEWIRE;
#else
			hlog_info(THERMOSTAT_MODULE, "OneWire sensors are not enabled.");
#endif /* HAVE_ONE_WIRE */
		} else if (p == strlen("sht20") && !strncmp(tok2, "sht20", p)) {
#ifdef HAVE_SHT20
			dev.temp_provider = THERM_TEMP_SHT20;
#else
			hlog_info(THERMOSTAT_MODULE, "SHT20 sensors are not enabled.");
#endif /* HAVE_SHT20 */
		} else if (p == strlen("ntc") && !strncmp(tok2, "ntc", p)) {
#ifdef HAVE_TEMPERATURE
			dev.temp_provider = THERM_TEMP_NTC;
#else
			hlog_info(THERMOSTAT_MODULE, "NTC temperature sensors are not enabled.");
#endif /* HAVE_TEMPERATURE */
		} else {
			hlog_info(THERMOSTAT_MODULE, "Invalid temperature provider [%s]", (p > 1) ? tok2 : "NULL");
			continue;
		}
		dev.temp_id = (int)strtol(rest2, NULL, 0);
		(*ctx)->devices[c] = calloc(1, sizeof(dev));
		if (!(*ctx)->devices[c])
			continue;
		dev.enable = false;
		memcpy((*ctx)->devices[c], &dev, sizeof(dev));
		c++;
	}
	(*ctx)->dev_count = c;

	c = 0;
	rest = config_defaults;
	while ((tok = strtok_r(rest, ";", &rest))) {
		if (c >= (*ctx)->dev_count)
			break;
		rest1 = tok;
		tok1 = strtok_r(rest1, ":", &rest1);
		if (!tok1 || !rest1)
			continue;
		rest2 = rest1;
		tok2 = strtok_r(rest2, "-", &rest2);
		if (!tok2 || !rest2)
			continue;
		if (strlen(tok1) == strlen(THERM_ON_STR) && !strncmp(tok1, THERM_ON_STR, strlen(tok1)))
			(*ctx)->devices[c]->enable = true;
		else
			(*ctx)->devices[c]->enable = false;
		(*ctx)->devices[c]->off_t = strtof(tok2, NULL);
		f = strtof(rest2, NULL);
		if (f <= (*ctx)->devices[c]->off_t)
			(*ctx)->devices[c]->on_t = (*ctx)->devices[c]->off_t - f;
		else
			(*ctx)->devices[c]->enable = false;
		c++;
	}

out:
	free(config_defaults);
	free(config_therm);
	if ((*ctx) && (*ctx)->dev_count < 1) {
		free(*ctx);
		(*ctx) = NULL;
	}

	return ((*ctx) ? (*ctx)->dev_count > 0 : 0);
}

static bool therm_init(struct thermostat_context_t **ctx)
{
	if (!therm_config_get(ctx))
		return false;

	therm_mqtt_init(*ctx);

	hlog_info(THERMOSTAT_MODULE, "Initialise successfully %d  thermostats",
			  (*ctx)->dev_count);
	return true;
}

static void therm_debug_set(uint32_t debug, void *context)
{
	struct thermostat_context_t *ctx = (struct thermostat_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

static int therm_set_temperatures(struct thermostat_context_t *ctx,
								  int idx, float temperature, bool hist)
{
	float f;

	if (idx < 0 || idx >= ctx->dev_count || !ctx->devices[idx]) {
		hlog_err(THERMOSTAT_MODULE, "Invalid thermostat device %d", idx);
		return -1;
	}
	if (hist) {
		if (temperature > ctx->devices[idx]->off_t) {
			hlog_err(THERMOSTAT_MODULE, "Invalid hysteresis temperature %f, ensure it is less than %f",
					 temperature, ctx->devices[idx]->off_t);
			return -1;
		}
		ctx->devices[idx]->on_t = ctx->devices[idx]->off_t - temperature;
		if (IS_DEBUG(ctx))
			hlog_info(THERMOSTAT_MODULE, "Set %d Toff to %f", idx, ctx->devices[idx]->off_t);
	} else {
		f = ctx->devices[idx]->off_t - ctx->devices[idx]->on_t;
		if (f < 0 || temperature < f) {
			hlog_err(THERMOSTAT_MODULE, "Invalid temperature %f, ensure it is less than %f",
					 temperature, f);
			return -1;
		}
		ctx->devices[idx]->on_t = temperature - f;
		ctx->devices[idx]->off_t = temperature;
		if (IS_DEBUG(ctx)) {
			hlog_info(THERMOSTAT_MODULE, "Set %d Toff to %f", idx, ctx->devices[idx]->off_t);
			hlog_info(THERMOSTAT_MODULE, "Set %d Ton to %f", idx, ctx->devices[idx]->on_t);
		}
	}

	return 0;
}

static int therm_set_state(struct thermostat_context_t *ctx, int idx, bool state)
{
	int ret = 0;
	int i;

	if (idx >= 0) {
		if (idx < ctx->dev_count && ctx->devices[idx]) {
			ctx->devices[idx]->enable = state;
			if (IS_DEBUG(ctx))
				hlog_info(THERMOSTAT_MODULE, "%s %d ", state ? "Enable" : "Disable", idx);
		} else {
			hlog_err(THERMOSTAT_MODULE, "Invalid thermostat device %d", idx);
			ret = -1;
		}
	} else {
		for (i = 0; i < ctx->dev_count; i++) {
			if (!ctx->devices[i])
				continue;
			ctx->devices[i]->enable = state;
			if (IS_DEBUG(ctx))
				hlog_info(THERMOSTAT_MODULE, "%s %d ", state ? "Enable" : "Disable", i);
		}
	}

	return ret;
}

static int cmd_therm_disable_all(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);

	if (therm_set_state(wctx, -1, false))
		return -1;

	return 0;
}

static int cmd_therm_enable_all(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);

	if (therm_set_state(wctx, -1, true))
		return -1;

	return 0;
}

static int cmd_therm_on(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;
	int id;

	UNUSED(ctx);
	UNUSED(cmd);

	if (strlen(params) < 2 || params[0] != ':')
		return -1;
	id = (int)strtol(params + 1, NULL, 10);
	if (id < 0)
		return -1;
	return therm_set_state(wctx, id, true);
}

static int cmd_therm_off(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;
	int id;

	UNUSED(ctx);
	UNUSED(cmd);

	if (strlen(params) < 2 || params[0] != ':')
		return -1;
	id = (int)strtol(params + 1, NULL, 10);
	if (id < 0)
		return -1;
	return therm_set_state(wctx, id, false);
}

static int cmd_therm_set_temperature(struct thermostat_context_t *ctx, char *params, bool hist)
{
	char *tok, *rest = params;
	float temp;
	int id;

	tok = strtok_r(rest, ":", &rest);
	if (!tok || !rest)
		return -1;
	id = (int)strtol(tok, NULL, 10);
	if (id < 0)
		return -1;
	temp = strtof(rest, NULL);
	return therm_set_temperatures(ctx, id, temp, hist);
}

static int cmd_therm_temp(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);

	if (strlen(params) < 2 || params[0] != ':')
		return -1;
	return cmd_therm_set_temperature(wctx, params + 1, false);
}

static int cmd_therm_hist(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct thermostat_context_t *wctx = (struct thermostat_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);

	if (strlen(params) < 2 || params[0] != ':')
		return -1;
	return cmd_therm_set_temperature(wctx, params + 1, true);
}

static app_command_t therm_requests[] = {
	{"on", ":<thermostat_id> - enable thermostat", cmd_therm_on},
	{"off", ":<thermostat_id> - disable thermostat", cmd_therm_off},
	{"temperature", ":<thermostat_id>:<temperature in *C> - set desired temperature", cmd_therm_temp},
	{"hysteresis", ":<thermostat_id>:<hysteresis in *C> - set operational hysteresis", cmd_therm_hist},
	{"on_all", " - enable all thermostats", cmd_therm_enable_all},
	{"off_all", " - disable all thermostats", cmd_therm_disable_all}
};

void thermostat_register(void)
{
	struct thermostat_context_t *ctx = NULL;

	if (!therm_init(&ctx))
		return;

	ctx->mod.name = THERMOSTAT_MODULE;
	ctx->mod.run = therm_run;
	ctx->mod.log = therm_log;
	ctx->mod.debug = therm_debug_set;
	ctx->mod.commands.hooks = therm_requests;
	ctx->mod.commands.count = ARRAY_SIZE(therm_requests);
	ctx->mod.commands.description = "Thermostat";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
