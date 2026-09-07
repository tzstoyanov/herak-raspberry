// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#include "dev_uart.h"

#define SONICA20_READ_INTERVAL_MS		100
#define SONICA20_MODULE		"sonica20"
#define SONICA20_SENORS_MAX	6
#define SONICA20_PROBES		10
#define MQTT_DATA_LEN	128
#define MQTT_REFRESH_MS	10000	// 10s
#define SONICA20_UART_DEBUG		0x2
#define SONICA20_RESPONCE_WORD		4
#define SONICA20_MEASSURE_DELAY_MS	50

#define SONICA20_MIN	0
#define SONICA20_MAX	4500
#define VALID_DATA(D) (((D) >= SONICA20_MIN) && ((D) <= SONICA20_MAX))

#define UART_BAUD	  9600

#define IS_DEBUG(C)	((C)->debug)

struct sonica20_context;

struct sonica20_sensor {
	int rx_pin;
	int tx_pin;
	struct dev_uart dev;
	uint16_t distance;
	int id;
	int idx;
	uint32_t probes[SONICA20_PROBES];
	mqtt_component_t mqtt_comp;
	uint64_t mqtt_last_send;
	struct sonica20_context *ctx;
	uint64_t ok_stat;
	uint64_t err_stat;
};

struct sonica20_context {
	sys_module_t mod;
	uint8_t count;
	struct sonica20_sensor *sensors[SONICA20_SENORS_MAX];
	uint64_t last_read;
	uint32_t debug;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static int sonica20_sensor_read(struct sonica20_sensor *sensor)
{
	uint8_t buff[SONICA20_RESPONCE_WORD];
	uint8_t checksum;
	int ret;

	buff[0] = 0xFF;
	if (dev_uart_send(&(sensor->dev), buff, 1)) {
		if (IS_DEBUG(sensor->ctx))
			hlog_warning(SONICA20_MODULE, "[%d] failed to request measurement",
						 sensor->id);
		return -1;
	}

	busy_wait_ms(SONICA20_MEASSURE_DELAY_MS);
	memset(buff, 0, SONICA20_RESPONCE_WORD);

	ret = dev_uart_read(&(sensor->dev), buff, ARRAY_SIZE(buff));
	if (ret != ARRAY_SIZE(buff)) {
		if (IS_DEBUG(sensor->ctx))
			hlog_warning(SONICA20_MODULE, "[%d] broken reading, not enough bytes: %d",
						 sensor->id, ret);
		return -1;
	}
	if (buff[0] != 0xFF) {
		if (IS_DEBUG(sensor->ctx))
			hlog_warning(SONICA20_MODULE, "[%d] broken reading, start byte missing: 0x%X",
						 sensor->id, buff[0]);
		return -1;
	}

	checksum = (uint8_t)((buff[0] + buff[1] + buff[2]) & 0xFF);
	if (checksum != buff[3]) {
		if (IS_DEBUG(sensor->ctx))
			hlog_warning(SONICA20_MODULE, "[%d] broken reading, invalid checksum: 0x%X != 0x%X",
						 sensor->id, checksum, buff[3]);
		return -1;
	}

	ret = (uint16_t)((buff[1] << 8) | buff[2]);
	if (IS_DEBUG(sensor->ctx)) {
		if (VALID_DATA(ret))
			hlog_info(SONICA20_MODULE, "[%d] correct reading: %d", sensor->id, ret);
		else
			hlog_warning(SONICA20_MODULE, "[%d] invalid data reading: %d",
						 sensor->id, ret);
	}
	return ret;
}

static bool sonica20_config_get(struct sonica20_context **ctx)
{
	char *config = param_get(SONIC_A20);
	struct sonica20_sensor sensor;
	char *rest, *tok, *tokrx, *toktx;

	(*ctx) = NULL;
	if (!config || strlen(config) < 1)
		goto out;

	(*ctx) = calloc(1, sizeof(struct sonica20_context));
	if ((*ctx) == NULL)
		goto out;

	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		toktx = strtok_r(tok, ",", &tokrx);
		if (!toktx || !tokrx)
			continue;
		memset(&sensor, 0, sizeof(sensor));
		sensor.tx_pin = (int)strtol(toktx, NULL, 0);
		sensor.rx_pin = (int)strtol(tokrx, NULL, 0);
		if (!GPIO_IS_VALID(sensor.rx_pin) || !GPIO_IS_VALID(sensor.tx_pin))
			continue;
		(*ctx)->sensors[(*ctx)->count] = calloc(1, sizeof(struct sonica20_sensor));
		if (!(*ctx)->sensors[(*ctx)->count])
			continue;
		sensor.ctx = *ctx;
		sensor.id = (*ctx)->count;
		memcpy((*ctx)->sensors[(*ctx)->count], &sensor, sizeof(sensor));
		(*ctx)->count++;
		if ((*ctx)->count >= SONICA20_SENORS_MAX)
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

static bool sonica20_log(void *context)
{
	struct sonica20_context *ctx = (struct sonica20_context *)context;
	int q, i;

	if (!ctx)
		return true;
	hlog_info(SONICA20_MODULE, "Reading %d sensors:", ctx->count);
	for (i = 0; i < ctx->count; i++) {
		q = ((ctx->sensors[i]->ok_stat * 100) / (ctx->sensors[i]->ok_stat + ctx->sensors[i]->err_stat));
		hlog_info(SONICA20_MODULE, "\t[%d]: %d mm, accuracy %d%%",
				  i, ctx->sensors[i]->distance, q);
	}

	return true;
}

static void sonica20_debug_set(uint32_t debug, void *context)
{
	struct sonica20_context *ctx = (struct sonica20_context *)context;
	int i;

	if (ctx)
		ctx->debug = debug;

	for (i = 0; i < ctx->count; i++)
		dev_uart_debug_set(&(ctx->sensors[i]->dev), debug & SONICA20_UART_DEBUG);
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int sonica20_mqtt_data_send(struct sonica20_context *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int ret = -1;

	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"distance\": \"%d\"", ctx->sensors[idx]->distance / 10); // mm -> cm
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->sensors[idx]->mqtt_comp, ctx->mqtt_payload);
	if (!ret)
		ctx->sensors[idx]->mqtt_last_send = now;

	return ret;
}

static void sonica20_mqtt_send(struct sonica20_context *ctx)
{
	uint64_t now = time_ms_since_boot();
	static uint8_t idx;
	int i;

	for (i = 0; i < ctx->count; i++) {
		if ((now - ctx->sensors[i]->mqtt_last_send) >= MQTT_REFRESH_MS)
			ctx->sensors[i]->mqtt_comp.force = true;
	}
	if (idx >= ctx->count)
		idx = 0;
	for (i = idx; i < ctx->count; i++) {
		if (ctx->sensors[i]->mqtt_comp.force) {
			sonica20_mqtt_data_send(ctx, i);
			idx++;
			return;
		}
	}
}

static void sonica20_sensor_calc(struct sonica20_sensor *sensor)
{
	uint32_t res;

	res = samples_filter(sensor->probes, SONICA20_PROBES, 2);
	if (sensor->distance != res)
		sensor->mqtt_comp.force = true;
	sensor->distance = res;
	memset(sensor->probes, 0, SONICA20_PROBES * sizeof(sensor->probes[0]));
	sensor->idx = 0;
}

static void sonica20_run(void *context)
{
	struct sonica20_context *ctx = (struct sonica20_context *)context;
	uint64_t now = time_ms_since_boot();
	int i, ret;

	if (((now - ctx->last_read) < SONICA20_READ_INTERVAL_MS))
		return;

	for (i = 0; i < ctx->count; i++) {
		ret = sonica20_sensor_read(ctx->sensors[i]);
		if (VALID_DATA(ret)) {
			ctx->sensors[i]->ok_stat++;
			ctx->sensors[i]->probes[ctx->sensors[i]->idx++] = ret;
			if (ctx->sensors[i]->idx >= SONICA20_PROBES) {
				sonica20_sensor_calc(ctx->sensors[i]);
				if (IS_DEBUG(ctx))
					hlog_info(SONICA20_MODULE, "Got from sensor %d: %d mm",
							  i, ctx->sensors[i]->distance);
			}
		} else {
			ctx->sensors[i]->err_stat++;
		}
	}
	sonica20_mqtt_send(ctx);
	ctx->last_read = now;
}

static void sonica20_mqtt_components_add(struct sonica20_context *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->sensors[i]->mqtt_comp.module = SONICA20_MODULE;
		ctx->sensors[i]->mqtt_comp.platform = "sensor";
		ctx->sensors[i]->mqtt_comp.dev_class = "distance";
		ctx->sensors[i]->mqtt_comp.unit = "cm";
		ctx->sensors[i]->mqtt_comp.value_template = "{{ value_json['distance'] }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp.name, "Distance_%d", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp));
	}
}

static bool sonica20_init(struct sonica20_context **ctx)
{
	int i;

	if (!sonica20_config_get(ctx))
		return false;
	for (i = 0; i < (*ctx)->count && (*ctx)->sensors[i]; i++) {
		if (dev_uart_init(&((*ctx)->sensors[i]->dev),
					   (*ctx)->sensors[i]->rx_pin, (*ctx)->sensors[i]->tx_pin, UART_BAUD))
			return false;
		if (dev_uart_start(&((*ctx)->sensors[i]->dev)))
			return false;
	}
	sonica20_mqtt_components_add(*ctx);
	hlog_info(SONICA20_MODULE, "Initialise successfully %d sensors", (*ctx)->count);
	for (i = 0; i < (*ctx)->count; i++)
		hlog_info(SONICA20_MODULE, "\tSensor %d attached to tx %d;rx %d, running at %d baud",
				   i, (*ctx)->sensors[i]->tx_pin, (*ctx)->sensors[i]->rx_pin, UART_BAUD);

	return true;
}

void sonica20_register(void)
{
	struct sonica20_context *ctx = NULL;

	if (!sonica20_init(&ctx))
		return;

	ctx->mod.name = SONICA20_MODULE;
	ctx->mod.run = sonica20_run;
	ctx->mod.log = sonica20_log;
	ctx->mod.debug = sonica20_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
