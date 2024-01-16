// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"

#include "shaft.h"

#define MQTTLOG	"mqtt"

#define MQTT_DATA_LEN	128

#define MQTT_MESSAGE	"{ \"time\": \"%s\", \"level\": %3.2f, \"in_temp\": %3.2f }"

static struct {
	float sonar_distance;
	float internal_temp;
	char payload[MQTT_DATA_LEN + 1];
} mqtt_shaft_context;

static void mqtt_data_send(bool force)
{
	static char time_buff[32];

	if (force)
		snprintf(mqtt_shaft_context.payload, MQTT_DATA_LEN, MQTT_MESSAGE,
				 get_current_time_str(time_buff, 32),
				 mqtt_shaft_context.sonar_distance, mqtt_shaft_context.internal_temp);
	mqtt_msg_publish(mqtt_shaft_context.payload, force);
}

void mqtt_data_sonar(float distance)
{
	bool force = false;

	if (mqtt_shaft_context.sonar_distance != distance) {
		mqtt_shaft_context.sonar_distance = distance;
		force = true;
	}
	mqtt_data_send(force);
}

void mqtt_data_internal_temp(float temp)
{
	bool force = false;

	if (mqtt_shaft_context.internal_temp != temp) {
		mqtt_shaft_context.internal_temp = temp;
		force = true;
	}
	mqtt_data_send(force);
}
