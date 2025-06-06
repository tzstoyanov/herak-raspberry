// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "lwip/inet.h"
#include "lwip/altcp.h"

#include "herak_sys.h"
#include "common_lib.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define SOIL_MODULE	"soil"
#define MAX_SOIL_SENSORS_COUNT 5
#define MQTT_SEND_INTERVAL_MS 10000
#define MAX_ANALOG_VALUE	4095

/* For each measurements, take 30 samples */
#define SOIL_MEASURE_COUNT 30
/* Filter out the 5 biggest and the 5 smallest */
#define SOIL_MEASURE_DROP	5

#define WH_DEFAULT_PORT	80
#define WH_HTTP_CMD		"POST"
#define WH_HTTP_TYPE	"application/json"
#define HTTP_OK	200
#define	WH_PAYLOAD_MAX_SIZE	128
#define	WH_PAYLOAD_TEMPLATE "{ \"Soil sensor\": %s-%d, \"status\": \"%s\", \"value\": %d }"
#define WH_SEND_DELAY_MS	5000
#define MQTT_DATA_LEN   128

static struct {
	int gp_id;
	int adc_id;
} adc_mapping[] = {
		{26, 0},
		{27, 1},
		{28, 2},
};

struct soil_sensor_analog_t {
	int adc_id;
	uint32_t samples[SOIL_MEASURE_COUNT];
	uint32_t last_analog;
	uint8_t percent;
	mqtt_component_t mqtt_comp;
};

struct soil_sensor_t {
	int analog_pin;
	struct soil_sensor_analog_t *analog;
	int digital_pin;
	uint8_t last_digital;
	bool wh_send;
	uint64_t wh_last_send;
	bool updated;
	mqtt_component_t mqtt_comp;
};

struct soil_context_t {
	sys_module_t mod;
	char *dev_name;
	uint32_t send_time;
	int sensors_count;
	struct soil_sensor_t sensors[MAX_SOIL_SENSORS_COUNT];
	int wh_idx;
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
	uint32_t debug;
};

static int wh_notify(struct soil_context_t *ctx, int id, int trigger, int data)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
			 ctx->dev_name ? ctx->dev_name : "Uknown", id, trigger?"dry":"wet", data);
	return webhook_send(ctx->wh_idx, notify_buff, strlen(notify_buff));
}

static void wh_notify_send(struct soil_context_t *ctx, int id)
{
	uint64_t now;

	if (!ctx->sensors[id].wh_send)
		return;

	now =  time_ms_since_boot();
	if ((now - ctx->sensors[id].wh_last_send) > WH_SEND_DELAY_MS) {
		if (!wh_notify(ctx, id, ctx->sensors[id].last_digital,
					   ctx->sensors[id].analog ? ctx->sensors[id].analog->percent : 0)) {
			ctx->sensors[id].wh_send = false;
		}
		ctx->sensors[id].wh_last_send = now;
	}
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int soil_mqtt_sensor_send(struct soil_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int ret;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR))
	ADD_MQTT_MSG_VAR(",\"id\": \"%d\"", idx);
	ADD_MQTT_MSG_VAR(",\"value_d\": \"%d\"", ctx->sensors[idx].last_digital);
	ADD_MQTT_MSG_VAR(",\"value_a\": \"%d\"",
					 ctx->sensors[idx].analog ? ctx->sensors[idx].analog->percent : 0);
	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->sensors[idx].mqtt_comp, ctx->mqtt_payload);
	ctx->sensors[idx].updated = false;

	if (!ret)
		ctx->mqtt_last_send = now;
	return ret;
}

static void soil_mqtt_send(struct soil_context_t *ctx)
{
	static int idx;
	uint64_t now;
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		if (ctx->sensors[i].updated)
			ctx->sensors[i].mqtt_comp.force = true;
	}

	for (i = 0; i < ctx->sensors_count; i++) {
		if (ctx->sensors[i].mqtt_comp.force) {
			soil_mqtt_sensor_send(ctx, i);
			return;
		}
	}

	now = time_ms_since_boot();
	if (ctx->mqtt_last_send &&
	    (now - ctx->mqtt_last_send) < MQTT_SEND_INTERVAL_MS)
		return;

	if (idx >= ctx->sensors_count)
		idx = 0;
	if (!soil_mqtt_sensor_send(ctx, idx))
		idx++;
}

static void measure_analog(struct soil_context_t *ctx, int id)
{
	uint32_t av;
	int i, pcnt;

	adc_select_input(ctx->sensors[id].analog->adc_id);

	/* read the samples */
	for (i = 0; i < SOIL_MEASURE_COUNT; i++)
		ctx->sensors[id].analog->samples[i] = adc_read();

	/* filter biggest and smallest */
	av = samples_filter(ctx->sensors[id].analog->samples, SOIL_MEASURE_COUNT, SOIL_MEASURE_DROP);
	if (ctx->sensors[id].analog->last_analog != av) {
		ctx->sensors[id].analog->last_analog = av;
		pcnt = 100 - sys_value_to_percent(0, MAX_ANALOG_VALUE, av);
		if (ctx->sensors[id].analog->percent != pcnt) {
			ctx->sensors[id].analog->percent = pcnt;
			ctx->sensors[id].updated = true;
		}
	}
}

// 1 - dry; 0 - wet
static void measure_digital(struct soil_context_t *ctx, int id)
{
	uint8_t digital;

	digital = gpio_get(ctx->sensors[id].digital_pin);
	if (digital != ctx->sensors[id].last_digital) {
		ctx->sensors[id].last_digital = digital;
		ctx->sensors[id].wh_send = true;
		ctx->sensors[id].updated = true;
	}
}

static void soil_run(void *context)
{
	struct soil_context_t *ctx = (struct soil_context_t *)context;
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		if (ctx->sensors[i].analog)
			measure_analog(ctx, i);
		if (ctx->sensors[i].digital_pin >= 0) {
			measure_digital(ctx, i);
			if (ctx->wh_idx >= 0 && ctx->sensors[i].updated)
				wh_notify_send(ctx, i);
		}
	}

	soil_mqtt_send(ctx);
}

static int soil_read_pin_cfg(struct soil_context_t *ctx, char *config, bool digital)
{
	char *config_tokens[MAX_SOIL_SENSORS_COUNT];
	char *config_pins[2];
	char *rest, *tok;
	int count = 0;
	int idx, id;
	int i, j;

	if (!config)
		return 0;

	i = 0;
	rest = config;
	while (i < MAX_SOIL_SENSORS_COUNT && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i > 0)
		i--;
	while (i >= 0) {
		j = 0;
		rest = config_tokens[i];
		while (j < 2 && (tok = strtok_r(rest, ":", &rest)))
			config_pins[j++] = tok;
		i--;
		if (j != 2)
			continue;
		idx = (int)strtol(config_pins[0], NULL, 10);
		id = (int)strtol(config_pins[1], NULL, 10);
		if (idx < 0 || idx >= MAX_SOIL_SENSORS_COUNT)
			continue;
		if (id < 0 || id > 40)
			continue;
		count++;
		if (digital)
			ctx->sensors[idx].digital_pin = id;
		else
			ctx->sensors[idx].analog_pin = id;
	}

	return count;
}

static void wh_callback(int idx, int http_code, void *context)
{
	UNUSED(idx);
	UNUSED(context);
	switch (http_code) {
	case 0:
		hlog_info(SOIL_MODULE, "http notify timeout");
		break;
	case HTTP_OK:
		break;
	default:
		hlog_info(SOIL_MODULE, "http notify error [%d]", http_code);
		break;
	}
}

static bool notify_get_config(char **server, char **endpoint, int *port)
{
	char *port_str = NULL;
	char *srv = NULL;
	char *ep = NULL;
	int port_id = 0;

	srv = USER_PRAM_GET(WEBHOOK_SERVER);
	if (!srv || strlen(srv) < 1)
		goto out_err;
	ep = USER_PRAM_GET(WEBHOOK_ENDPOINT);
	if (!ep || strlen(ep) < 1)
		goto out_err;

	port_str = USER_PRAM_GET(WEBHOOK_PORT);
	if (port_str && strlen(port_str) > 1)
		port_id = atoi(port_str);
	if (!port_id)
		port_id = WH_DEFAULT_PORT;
	free(port_str);

	if (server)
		*server = srv;
	else
		free(srv);
	if (endpoint)
		*endpoint = ep;
	else
		free(ep);
	if (port)
		*port = port_id;

	return true;
out_err:
	free(srv);
	free(ep);
	return false;
}

static bool soil_log(void *context)
{
	struct soil_context_t *ctx = (struct soil_context_t *)context;
	int s, p, i;

	for (i = 0; i < ctx->sensors_count; i++) {
		if (ctx->sensors[i].analog) {
			s = ctx->sensors[i].analog->last_analog;
			p = ctx->sensors[i].analog->percent;
		} else {
			s = -1;
			p = -1;
		}
		hlog_info(SOIL_MODULE, "Sensor %d: digital %d, analog %d / %d%%",
				  i, ctx->sensors[i].last_digital, s, p);
	}

	return true;
}

static void soil_mqtt_init(struct soil_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->sensors_count; i++) {
		ctx->sensors[i].mqtt_comp.module = SOIL_MODULE;
		ctx->sensors[i].mqtt_comp.platform = "binary_sensor";
		ctx->sensors[i].mqtt_comp.dev_class = "moisture";
		ctx->sensors[i].mqtt_comp.payload_on = "0";
		ctx->sensors[i].mqtt_comp.payload_off = "1";
		ctx->sensors[i].mqtt_comp.value_template = "{{ value_json.value_d }}";
		sys_asprintf(&ctx->sensors[i].mqtt_comp.name, "Soil_%d", i);
		mqtt_msg_component_register(&ctx->sensors[i].mqtt_comp);
		if (ctx->sensors[i].analog) {
			ctx->sensors[i].analog->mqtt_comp.module = SOIL_MODULE;
			ctx->sensors[i].analog->mqtt_comp.platform = "sensor";
			ctx->sensors[i].analog->mqtt_comp.dev_class = "moisture";
			ctx->sensors[i].analog->mqtt_comp.value_template = "{{ value_json.value_a }}";
			sys_asprintf(&ctx->sensors[i].analog->mqtt_comp.name, "SoilA_%d", i);
			ctx->sensors[i].analog->mqtt_comp.state_topic = ctx->sensors[i].mqtt_comp.state_topic;
			mqtt_msg_component_register(&ctx->sensors[i].analog->mqtt_comp);
		}
	}
}

static bool soil_init(struct soil_context_t **ctx)
{
	char *digital = param_get(SOIL_D);
	char *analog = param_get(SOIL_A);
	char *wh_server, *wh_endpoint;
	bool has_analog = false;
	unsigned int cnt, i, j;
	int wh_port;

	(*ctx) = NULL;
	if ((!digital || strlen(digital) < 1) && (!analog || strlen(analog) < 1))
		goto out_error;

	(*ctx) = calloc(1, sizeof(struct soil_context_t));
	if ((*ctx) == NULL)
		goto out_error;

	(*ctx)->wh_idx = -1;
	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		(*ctx)->sensors[i].analog_pin = -1;
		(*ctx)->sensors[i].digital_pin = -1;
	}

	cnt = soil_read_pin_cfg((*ctx), digital, true);
	cnt += soil_read_pin_cfg((*ctx), analog, false);
	if (cnt < 1)
		goto out_error;

	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		if ((*ctx)->sensors[i].digital_pin > 0)
			(*ctx)->sensors_count++;
		if ((*ctx)->sensors[i].analog_pin > 0) {
			for (j = 0; j < ARRAY_SIZE(adc_mapping); j++) {
				if (adc_mapping[j].gp_id == (*ctx)->sensors[i].analog_pin) {
					(*ctx)->sensors[i].analog = calloc(1, sizeof(struct soil_sensor_analog_t));
					if (!(*ctx)->sensors[i].analog)
						break;
					has_analog = true;
					(*ctx)->sensors[i].analog->adc_id = adc_mapping[j].adc_id;
					if ((*ctx)->sensors[i].digital_pin < 0)
						(*ctx)->sensors_count++;
					break;
				}
			}
		}
	}

	if ((*ctx)->sensors_count < 1)
		goto out_error;
	(*ctx)->dev_name = USER_PRAM_GET(DEV_HOSTNAME);
	free(digital);
	free(analog);

	if (notify_get_config(&wh_server, &wh_endpoint, &wh_port))
		(*ctx)->wh_idx = webhook_add(wh_server, wh_port, WH_HTTP_TYPE, wh_endpoint, WH_HTTP_CMD, true, wh_callback, NULL);

	if (has_analog) {
		adc_init();
	    adc_set_round_robin(0);
	}

	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		if ((*ctx)->sensors[i].digital_pin > 0) {
			gpio_init((*ctx)->sensors[i].digital_pin);
			gpio_set_dir((*ctx)->sensors[i].digital_pin, GPIO_IN);
			gpio_put((*ctx)->sensors[i].digital_pin, 0);
		}
		if ((*ctx)->sensors[i].analog_pin > 0)
			adc_gpio_init((*ctx)->sensors[i].analog_pin);
	}
	soil_mqtt_init(*ctx);

	hlog_info(SOIL_MODULE, "%d soil sensors initialized", (*ctx)->sensors_count);
	return true;

out_error:
	free(digital);
	free(analog);
	free((*ctx));
	return false;
}

static void soil_debug_set(uint32_t debug, void *context)
{
	struct soil_context_t *ctx = (struct soil_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void soil_register(void)
{
	struct soil_context_t *ctx = NULL;

	if (!soil_init(&ctx))
		return;

	ctx->mod.name = SOIL_MODULE;
	ctx->mod.run = soil_run;
	ctx->mod.log = soil_log;
	ctx->mod.debug = soil_debug_set;
	ctx->mod.commands.description = "Soil moisture measure";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
