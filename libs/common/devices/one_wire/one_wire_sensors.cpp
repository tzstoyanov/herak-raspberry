// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "one_wire.h"

#include "base64.h"
#include "params.h"

#define ONEWIRE_MODULE "one_wire"
#define ONEWIRE_SENORS_MAX	3
#define ONEWIRE_LINES_MAX	10
#define MQTT_DATA_LEN		128
#define MQTT_DELAY_MS		5000
#define READ_INTERVAL_MS	1000

#define CONST_STR(x) const_cast < char * > (x)

struct one_wire_sensor {
	rom_address_t	rom_addr;
	uint64_t		address;
	float			temperature;
	uint64_t		mqtt_last_send;
	mqtt_component_t mqtt_comp;
	uint64_t		ok_stat;
	uint64_t		err_stat;
};

struct one_wire_line {
	int			pin;
	One_wire	*tempSensor;
	uint8_t		count;
	uint64_t meassure_now;
	uint64_t meassure_last;
	struct one_wire_sensor sensors[ONEWIRE_SENORS_MAX];
};

struct one_wire_context_t {
	sys_module_t mod;
	uint8_t count;
	struct one_wire_line *lines[ONEWIRE_LINES_MAX];
	uint32_t debug;
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static void one_wire_debug_set(uint32_t debug, void *context)
{
	struct one_wire_context_t *ctx = (struct one_wire_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int one_wire_mqtt_data_send(struct one_wire_context_t *ctx, int lidx, int sidx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	mqtt_component_t *ms;
	int count = 0;
	int ret = -1;

	ms = &ctx->lines[lidx]->sensors[sidx].mqtt_comp;
	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"id\": \"%llX\"", ctx->lines[lidx]->sensors[sidx].address);
		ADD_MQTT_MSG_VAR(",\"temperature\": \"%3.2f\"", ctx->lines[lidx]->sensors[sidx].temperature);
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ms, ctx->mqtt_payload);

	if (!ret)
		ctx->mqtt_last_send = now;

	return ret;
}

static void one_wire_mqtt_send(struct one_wire_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	static int lidx, sidx;
	int i, j;

	for (i = 0; i < ctx->count; i++)
		for (j = 0; j < ctx->lines[i]->count; j++) {
			if (ctx->lines[i]->sensors[j].mqtt_comp.force) {
				one_wire_mqtt_data_send(ctx, i, j);
				return;
			}
		}

	if ((now - ctx->mqtt_last_send) < MQTT_DELAY_MS)
		return;
	if (sidx >= ctx->lines[lidx]->count) {
		sidx = 0;
		lidx++;
	}
	if (lidx >= ctx->count) {
		lidx = 0;
		sidx = 0;
	}
	one_wire_mqtt_data_send(ctx, lidx, sidx);
	lidx++;
	sidx++;
}

static bool one_wire_log(void *context)
{
	struct one_wire_context_t *ctx = (struct one_wire_context_t *)context;
	uint32_t q;
	int i, j;

	hlog_info(ONEWIRE_MODULE, "Detected One-Wire sensors:");
	for (i = 0; i < ctx->count; i++)
		for (j = 0; j < ctx->lines[i]->count; j++) {
			q = ((ctx->lines[i]->sensors[j].ok_stat * 100) / (ctx->lines[i]->sensors[j].ok_stat + ctx->lines[i]->sensors[j].err_stat));
			hlog_info(ONEWIRE_MODULE, "\tId[0x%llX] on GPIO %d: %3.2f°C, connection %d%%",
					  ctx->lines[i]->sensors[j].address, ctx->lines[i]->pin,
					  ctx->lines[i]->sensors[j].temperature, q);
			}

	return true;
}

static void one_wire_start_measure(struct one_wire_line *line)
{
	line->meassure_now =
			line->tempSensor->convert_temperature(line->sensors[0].rom_addr, false, true);
	line->meassure_now += time_ms_since_boot();
}

static void one_wire_read_measure(struct one_wire_context_t *ctx, uint8_t idx)
{
	struct one_wire_line *line = ctx->lines[idx];
	float val;
	int i;

	for (i = 0; i < line->count; i++) {
		val = line->tempSensor->temperature(line->sensors[i].rom_addr);
		if (val == One_wire::invalid_conversion) {
			if (ctx->debug)
				hlog_info(ONEWIRE_MODULE, "CRC error reading sensor 0x%llX on GPIO %d",
						  line->sensors[i].address, line->pin);
			line->sensors[i].err_stat++;
		} else {
			if (ctx->debug)
				hlog_info(ONEWIRE_MODULE, "Got %3.2f°C from sensor 0x%llX on GPIO %d",
						  val, line->sensors[i].address, line->pin);
			if (line->sensors[i].temperature != val) {
				line->sensors[i].mqtt_comp.force = true;
				line->sensors[i].temperature = val;
			}
			line->sensors[i].ok_stat++;
		}
	}
}

static void one_wire_mqtt_init(struct one_wire_context_t *ctx, int line)
{
	int i;

	for (i = 0; i < ctx->lines[line]->count; i++) {
		ctx->lines[line]->sensors[i].mqtt_comp.module = CONST_STR(ONEWIRE_MODULE);
		ctx->lines[line]->sensors[i].mqtt_comp.platform = CONST_STR("sensor");
		ctx->lines[line]->sensors[i].mqtt_comp.dev_class = CONST_STR("temperature");
		ctx->lines[line]->sensors[i].mqtt_comp.unit = CONST_STR("°C");
		ctx->lines[line]->sensors[i].mqtt_comp.value_template = CONST_STR("{{ value_json['temperature'] }}");
		sys_asprintf(&ctx->lines[line]->sensors[i].mqtt_comp.name, "Temperature_0x%llX",
					 ctx->lines[line]->sensors[i].address);
		mqtt_msg_component_register(&ctx->lines[line]->sensors[i].mqtt_comp);
	}
}

static int one_wire_sensors_detect(struct one_wire_context_t *ctx, int line)
{
	int i;

	if (!gpio_get(ctx->lines[line]->pin)) {
		if (ctx->lines[line]->count) {
			if (ctx->debug)
				hlog_info(ONEWIRE_MODULE, "Temperature sensors disconnected from pin %d",
						  ctx->lines[line]->pin);
			ctx->lines[line]->count = 0;
		}
		return 0;
	}
	if (ctx->lines[line]->count)
		return ctx->lines[line]->count;
	ctx->lines[line]->count = ctx->lines[line]->tempSensor->find_and_count_devices_on_bus();
	if (ctx->lines[line]->count > ONEWIRE_SENORS_MAX)
		ctx->lines[line]->count = ONEWIRE_SENORS_MAX;
	memset(ctx->lines[line]->sensors, 0, ONEWIRE_SENORS_MAX * sizeof(struct one_wire_sensor));
	for (i = 0; i < ctx->lines[line]->count; i++) {
		ctx->lines[line]->sensors[i].rom_addr = ctx->lines[line]->tempSensor->get_address(i);
		ctx->lines[line]->sensors[i].address =
				ctx->lines[line]->tempSensor->to_uint64(ctx->lines[line]->sensors[i].rom_addr);
	if (ctx->debug)
		hlog_info(ONEWIRE_MODULE, "Detected sensor 0x%X on pin %d",
				  ctx->lines[line]->sensors[i].address, ctx->lines[line]->pin);
	}
	one_wire_mqtt_init(ctx, line);

	return ctx->lines[line]->count;
}

static void one_wire_run(void *context)
{
	struct one_wire_context_t *ctx = (struct one_wire_context_t *)context;
	uint64_t now = time_ms_since_boot();
	struct one_wire_line *line;
	static uint8_t line_idx;

	if (line_idx >= ctx->count)
		line_idx = 0;
	line = ctx->lines[line_idx];

	if (!line->count) {
		one_wire_sensors_detect(ctx, line_idx);
		goto out;
	}

	if (line->meassure_now) {
		if (line->meassure_now > now)
			goto out;
		one_wire_read_measure(ctx, line_idx);
		line->meassure_now = 0;
		line->meassure_last = now;
		goto out;
	}
	if ((now - line->meassure_last) >= READ_INTERVAL_MS)
		one_wire_start_measure(line);

out:
	one_wire_mqtt_send(ctx);
	line_idx++;
}

static bool one_wire_config_get(struct one_wire_context_t **ctx)
{
	char *config = param_get(ONE_WIRE_DEVICES);
	struct one_wire_line line;
	char *rest, *tok;

	(*ctx) = NULL;
	if (!config || strlen(config) < 1)
		goto out;

	(*ctx) = (struct one_wire_context_t *)calloc(1, sizeof(struct one_wire_context_t));
	if ((*ctx) == NULL)
		goto out;

	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		memset(&line, 0, sizeof(line));
		line.pin = (int)strtol(tok, NULL, 0);
		(*ctx)->lines[(*ctx)->count] = (struct one_wire_line *)calloc(1, sizeof(struct one_wire_line));
		if (!(*ctx)->lines[(*ctx)->count])
			continue;
		memcpy((*ctx)->lines[(*ctx)->count], &line, sizeof(line));
		(*ctx)->count++;
		if ((*ctx)->count >= ONEWIRE_LINES_MAX)
			break;
	}

out:
	free(config);
	if ((*ctx) && (*ctx)->count < 1) {
		free(*ctx);
		(*ctx) = NULL;
	}

	return ((*ctx) ? (*ctx)->count > 0 : 0);
}

static int one_wire_sensors_init(struct one_wire_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->lines[i]->tempSensor = new One_wire(ctx->lines[i]->pin, One_wire::not_controllable, false);
		if (ctx->lines[i]->tempSensor)
			ctx->lines[i]->tempSensor->init();
		else
			break;
	}

	if (i < ctx->count)
		return -1;

	return 0;
}

static bool one_wire_init(struct one_wire_context_t **ctx)
{
	int scount = 0;
	int i;

	if (!one_wire_config_get(ctx))
		return false;
	if (one_wire_sensors_init(*ctx))
		goto out_err;


	for (i = 0; i < (*ctx)->count; i++)
		scount += one_wire_sensors_detect(*ctx, i);

	hlog_info(ONEWIRE_MODULE, "Initialise successfully %d lines with %d attached sensors",
			  (*ctx)->count, scount);
	return true;

out_err:
	if (*ctx) {
		for (i = 0; i < (*ctx)->count; i++) {
			if ((*ctx)->lines[i]->tempSensor) {
				delete(*ctx)->lines[i]->tempSensor;
				(*ctx)->lines[i]->tempSensor = NULL;
			}
		}
		free((*ctx));
		(*ctx) = NULL;
	}
	return false;
}

extern "C" void one_wire_register(void)
{
	struct one_wire_context_t *ctx = NULL;

	if (!one_wire_init(&ctx))
		return;

	ctx->mod.name = CONST_STR(ONEWIRE_MODULE);
	ctx->mod.run = one_wire_run;
	ctx->mod.log = one_wire_log;
	ctx->mod.debug = one_wire_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
