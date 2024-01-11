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

#include "solar.h"

#define MQTTLOG	"mqtt"

#define MQTT_DATA_LEN	512

#define MQTT_MESSAGE	\
"{ \"time\": \"%s\", \"in_temp\": %3.2f, \"mppt_ac_out_v\": %3.2f, \"mppt_ac_out_hz\": %3.2f,\
\"mppt_ac_out_va\": %d, \"mppt_ac_out_w\": %d, \"mppt_out_load_p\": %d,\
\"mppt_bus_v\": %d, \"mppt_bat_v\": %3.2f, \"mppt_bat_charge_a\": %d,\
\"mppt_bat_capacity_p\": %d, \"mppt_sink_temp\": %d, \"mppt_pv_in_bat_a\": %3.2f,\
\"mppt_pv_in_v\": %3.2f, \"mppt_bat_discharge_a\": %d,\
\"bms_total_v\": %3.2f, \"bms_current_a\": %3.2f,\"bms_soc_p\": %3.2f,\
\"bms_life\": %d, \"bms_remain_capacity_mah\": %d}"

struct {
	mqtt_mppt_data_t mppt;
	mqtt_bms_data_t bms;
	float internal_temp;
	char payload[MQTT_DATA_LEN + 1];
} static mqtt_solar_context;

static void mqtt_data_send(bool force)
{
	static char time_buff[32];

	if (force)
		snprintf(mqtt_solar_context.payload, MQTT_DATA_LEN, MQTT_MESSAGE,
				get_current_time_str(time_buff, 32),
				mqtt_solar_context.internal_temp, mqtt_solar_context.mppt.ac_out_v,
				mqtt_solar_context.mppt.ac_out_hz, mqtt_solar_context.mppt.ac_out_va,
				mqtt_solar_context.mppt.ac_out_w, mqtt_solar_context.mppt.out_load_p,
				mqtt_solar_context.mppt.bus_v, mqtt_solar_context.mppt.bat_v,
				mqtt_solar_context.mppt.bat_charge_a, mqtt_solar_context.mppt.bat_capacity_p,
				mqtt_solar_context.mppt.sink_temp, mqtt_solar_context.mppt.pv_in_bat_a,
				mqtt_solar_context.mppt.pv_in_v, mqtt_solar_context.mppt.bat_discharge_a,
				mqtt_solar_context.bms.bat_v, mqtt_solar_context.bms.bat_i,
				mqtt_solar_context.bms.soc_p, mqtt_solar_context.bms.bms_life,
				mqtt_solar_context.bms.remain_capacity);

	if (strlen(mqtt_solar_context.payload))
		mqtt_msg_publish(mqtt_solar_context.payload, force);
}

void mqtt_data_mppt(mqtt_mppt_data_t *data)
{
	bool force = false;

	if (memcmp(&mqtt_solar_context.mppt, data, sizeof(mqtt_mppt_data_t))) {
		memcpy(&mqtt_solar_context.mppt, data, sizeof(mqtt_mppt_data_t));
		force = true;
	}
	mqtt_data_send(force);
}

void mqtt_data_bms(mqtt_bms_data_t *data)
{
	bool force = false;

	if (memcmp(&mqtt_solar_context.bms, data, sizeof(mqtt_bms_data_t))) {
		memcpy(&mqtt_solar_context.bms, data, sizeof(mqtt_bms_data_t));
		force = true;
	}
	mqtt_data_send(force);
}

void mqtt_data_internal_temp(float temp)
{
	bool force = false;

	if (mqtt_solar_context.internal_temp != temp) {
		mqtt_solar_context.internal_temp = temp;
		force = true;
	}
	mqtt_data_send(force);
}
