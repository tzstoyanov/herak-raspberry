// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"

#include "irrig.h"

#define MQTTLOG	"mqtt"
#define MQTT_DATA_LEN	512
#define MQTT_DISCOVERY_MS	1800000 // on 30 min

struct soil_data {
	uint32_t analog;
	uint8_t digital;
};

struct ssr_data {
	uint32_t time;
	uint32_t delay;
};

static struct {
	float internal_temp;
	uint32_t ssr_state;
	char payload[MQTT_DATA_LEN + 1];
	struct soil_data soil[MAX_SOIL_SENSORS_COUNT];
	int soil_count;
	struct ssr_data ssr[MAX_SSR_COUNT];
	int ssr_count;
	bool force;
	uint64_t last_discovery;
} mqtt_irrig_context = {};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return; } \
							count += snprintf(mqtt_irrig_context.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return; } \
				     count += snprintf(mqtt_irrig_context.payload + count, len - count, _S_, __VA_ARGS__); }
static void mqtt_data_send(bool force)
{
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int i;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\": \"%s\", \"in_temp\": %3.2f", get_current_time_str(time_buff, TIME_STR), mqtt_irrig_context.internal_temp)
	if (mqtt_irrig_context.ssr_count) {
		count += snprintf(mqtt_irrig_context.payload + count, len - count, ", \"ssr_state\": %lu", mqtt_irrig_context.ssr_state);
		for (i = 0; i < mqtt_irrig_context.ssr_count; i++) {
			if (i == 0)
				ADD_MQTT_MSG_VAR(", \"ssr\":[ {\"id\":%d, \"time\": %lu, \"delay\": %lu }",
								 i, mqtt_irrig_context.ssr[i].time, mqtt_irrig_context.ssr[i].delay)
			else
				ADD_MQTT_MSG_VAR(", {\"id\":%d, \"time\": %lu, \"delay\": %lu }",
								 i, mqtt_irrig_context.ssr[i].time, mqtt_irrig_context.ssr[i].delay)
		}
		ADD_MQTT_MSG("]")
	}
	if (mqtt_irrig_context.soil_count) {
		for (i = 0; i < mqtt_irrig_context.soil_count; i++) {
			if (i == 0)
				ADD_MQTT_MSG_VAR(", \"soil\":[ {\"id\":%d, \"value_d\": %d, \"value_a\": %lu }",
								 i, mqtt_irrig_context.soil[i].digital, mqtt_irrig_context.soil[i].analog)
			else
				ADD_MQTT_MSG_VAR(" , {\"id\":%d, \"value_d\": %d, \"value_a\": %lu }",
								  i, mqtt_irrig_context.soil[i].digital, mqtt_irrig_context.soil[i].analog)

		}
		ADD_MQTT_MSG("]")
	}

	ADD_MQTT_MSG("}")
	mqtt_irrig_context.payload[MQTT_DATA_LEN] = 0;
	mqtt_msg_publish(mqtt_irrig_context.payload, force);
}

void mqtt_data_soil(int id, uint32_t analog, uint8_t digital)
{
	if (id >= mqtt_irrig_context.soil_count)
		return;
	if (mqtt_irrig_context.soil[id].analog != analog) {
		mqtt_irrig_context.soil[id].analog = analog;
		mqtt_irrig_context.force = true;
	}
	if (mqtt_irrig_context.soil[id].digital != digital) {
		mqtt_irrig_context.soil[id].digital = digital;
		mqtt_irrig_context.force = true;
	}
}

void mqtt_data_ssr_state(unsigned int state)
{
	if (mqtt_irrig_context.ssr_state != state) {
		mqtt_irrig_context.ssr_state = state;
		mqtt_irrig_context.force = true;
	}
}

void mqtt_data_ssr_data(int id, uint32_t time, uint32_t delay)
{
	if (id >= mqtt_irrig_context.ssr_count)
		return;
	if (mqtt_irrig_context.ssr[id].time != time) {
		mqtt_irrig_context.ssr[id].time = time;
		mqtt_irrig_context.force = true;
	}
	if (mqtt_irrig_context.ssr[id].delay != delay) {
		mqtt_irrig_context.ssr[id].delay = delay;
		mqtt_irrig_context.force = true;
	}
}

void mqtt_data_internal_temp(float temp)
{
	if (mqtt_irrig_context.internal_temp != temp) {
		mqtt_irrig_context.internal_temp = temp;
		mqtt_irrig_context.force = true;
	}
}

#define DEV_QOS    2
#define ORG_NAME   "RaspberryRelay"
#define COMPONENTS_NUM	1
static int mqtt_irrig_discovery_add(void)
{
	mqtt_discovery_comp_t comps[COMPONENTS_NUM] = {0};
	mqtt_discovery_t discovery = {0};

	discovery.origin_name = ORG_NAME;
	discovery.qos = DEV_QOS;

	/* 1 */
	comps[0].name = "Chip temperature";
	comps[0].id = "ch_temp";
	comps[0].platform = "sensor";
	comps[0].dev_class = "temperature";
	comps[0].unit = "°C";
	comps[0].value_template = "{{value_json.in_temp}}";

	discovery.comp_count = COMPONENTS_NUM;
	discovery.components = comps;
	return mqtt_msg_discovery_register(&discovery);
}

void mqtt_irrig_send(void)
{
	uint64_t now = time_ms_since_boot();

	if (!mqtt_irrig_context.last_discovery ||
		(now - mqtt_irrig_context.last_discovery) > MQTT_DISCOVERY_MS) {
		if (mqtt_irrig_discovery_add() >= 0)
			mqtt_irrig_context.last_discovery = now;
	}
	mqtt_data_send(mqtt_irrig_context.force);
	mqtt_irrig_context.force = false;
}

void mqtt_irrig_init(int soil_count, int ssr_count)
{
	mqtt_irrig_context.soil_count = soil_count;
	mqtt_irrig_context.ssr_count = ssr_count;
}
