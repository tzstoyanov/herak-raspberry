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
#define MQTT_DATA_LEN	384

struct soil_data {
	uint32_t analog;
	uint8_t digital;
};

struct ssr_data {
	uint32_t time;
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
} mqtt_irrig_context = {};

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full", __func__); return; } \
							count += snprintf(mqtt_irrig_context.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full", __func__); return; } \
				     count += snprintf(mqtt_irrig_context.payload + count, len - count, _S_, __VA_ARGS__); }
static void mqtt_data_send(bool force)
{
	static char time_buff[32];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int i;

	ADD_MQTT_MSG_VAR("{ \"time\": \"%s\", \"in_temp\": %3.2f", get_current_time_str(time_buff, 32), mqtt_irrig_context.internal_temp)
	if (mqtt_irrig_context.ssr_count) {
		count += snprintf(mqtt_irrig_context.payload + count, len - count, ", \"ssr_state\": %lu", mqtt_irrig_context.ssr_state);
		for (i = 0; i < mqtt_irrig_context.ssr_count; i++) {
			if (i == 0)
				ADD_MQTT_MSG_VAR(", \"ssr\":[ {\"id\":%d, \"time\": %lu }", i, mqtt_irrig_context.ssr[i].time)
			else
				ADD_MQTT_MSG_VAR(", {\"id\":%d, \"time\": %lu }", i, mqtt_irrig_context.ssr[i].time)
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

void mqtt_data_ssr_data(int id, uint32_t time)
{
	if (id >= mqtt_irrig_context.ssr_count)
		return;
	if (mqtt_irrig_context.ssr[id].time != time) {
		mqtt_irrig_context.ssr[id].time = time;
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

void mqtt_irrig_send(void)
{
	mqtt_data_send(mqtt_irrig_context.force);
	mqtt_irrig_context.force = false;
}

void mqtt_irrig_init(int soil_count, int ssr_count)
{
	mqtt_irrig_context.soil_count = soil_count;
	mqtt_irrig_context.ssr_count = ssr_count;
}
