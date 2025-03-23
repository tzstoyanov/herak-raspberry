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
#define MQTT_DISCOVERY_MS	1800000 // on 30 min
static struct {
	mqtt_mppt_data_t mppt;
	mqtt_bms_data_t bms;
	float internal_temp;
	uint64_t last_discovery;
	char payload[MQTT_DATA_LEN + 1];
} mqtt_solar_context;

#define DEV_QOS    2
#define ORG_NAME   "MPPT"
#define ORG_VER	   "MAX"
#define COMPONENTS_NUM	1
static int mqtt_mppt_discovery_add(mqtt_mppt_data_t *mppt)
{
	mqtt_discovery_comp_t comps[COMPONENTS_NUM] = {0};
	mqtt_discovery_t discovery = {0};

	discovery.dev_model = mppt->model_name;
	discovery.dev_sw_ver = mppt->firmware_vesion;
	discovery.dev_hw_ver = mppt->firmware_vesion3;
	discovery.dev_sn = mppt->serial_number;
	discovery.origin_name = ORG_NAME;
	discovery.origin_sw_ver = ORG_VER;
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

static void mqtt_discovery_add(void)
{
	uint64_t now = time_ms_since_boot();

	if (mqtt_solar_context.last_discovery &&
		(now - mqtt_solar_context.last_discovery) < MQTT_DISCOVERY_MS)
		return;

	if (mqtt_mppt_discovery_add(&mqtt_solar_context.mppt) >= 0)
		mqtt_solar_context.last_discovery = now;
	else
		hlog_warning(MQTTLOG, "Failed to register discovery message");
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return; } \
				count += snprintf(mqtt_solar_context.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return; } \
				     count += snprintf(mqtt_solar_context.payload + count, len - count, _S_, __VA_ARGS__); }
void mqtt_data_send(bool force)
{
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;

	if (force) {
		ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"in_temp\":%3.2f", mqtt_solar_context.internal_temp);
		ADD_MQTT_MSG_VAR(",\"mppt_ac_out_v\":%3.2f", mqtt_solar_context.mppt.ac_out_v);
		ADD_MQTT_MSG_VAR(",\"mppt_ac_out_hz\":%3.2f", mqtt_solar_context.mppt.ac_out_hz);
		ADD_MQTT_MSG_VAR(",\"mppt_ac_out_va\":%d", mqtt_solar_context.mppt.ac_out_va);
		ADD_MQTT_MSG_VAR(",\"mppt_ac_out_w\":%d", mqtt_solar_context.mppt.ac_out_w);
		ADD_MQTT_MSG_VAR(",\"mppt_out_load_p\":%d", mqtt_solar_context.mppt.out_load_p);
		ADD_MQTT_MSG_VAR(",\"mppt_bus_v\":%d", mqtt_solar_context.mppt.bus_v);
		ADD_MQTT_MSG_VAR(",\"mppt_bat_v\":%3.2f", mqtt_solar_context.mppt.bat_v);
		ADD_MQTT_MSG_VAR(",\"mppt_bat_charge_a\":%d", mqtt_solar_context.mppt.bat_charge_a);
		ADD_MQTT_MSG_VAR(",\"mppt_bat_capacity_p\":%d", mqtt_solar_context.mppt.bat_capacity_p);
		ADD_MQTT_MSG_VAR(",\"mppt_sink_temp\":%d", mqtt_solar_context.mppt.sink_temp);
		ADD_MQTT_MSG_VAR(",\"mppt_pv_in_bat_a\":%3.2f", mqtt_solar_context.mppt.pv_in_bat_a);
		ADD_MQTT_MSG_VAR(",\"mppt_pv_in_v\":%3.2f", mqtt_solar_context.mppt.pv_in_v);
		ADD_MQTT_MSG_VAR(",\"mppt_bat_discharge_a\":%d", mqtt_solar_context.mppt.bat_discharge_a);
		ADD_MQTT_MSG_VAR(",\"bms_total_v\":%3.2f", mqtt_solar_context.bms.bat_v);
		ADD_MQTT_MSG_VAR(",\"bms_current_a\":%3.2f", mqtt_solar_context.bms.bat_i);
		ADD_MQTT_MSG_VAR(",\"bms_soc_p\":%3.2f", mqtt_solar_context.bms.soc_p);
		ADD_MQTT_MSG_VAR(",\"bms_life\":%d", mqtt_solar_context.bms.bms_life);
		ADD_MQTT_MSG_VAR(",\"bms_remain_capacity_mah\":%ld", mqtt_solar_context.bms.remain_capacity);
		ADD_MQTT_MSG("}");
	}

	if (strlen(mqtt_solar_context.payload))
		mqtt_msg_publish(mqtt_solar_context.payload, force);
}

static void mqtt_solar_run(bool force)
{
	mqtt_discovery_add();
	mqtt_data_send(force);
}

void mqtt_data_mppt(mqtt_mppt_data_t *data)
{
	bool force = false;

	if (memcmp(&mqtt_solar_context.mppt, data, sizeof(mqtt_mppt_data_t))) {
		memcpy(&mqtt_solar_context.mppt, data, sizeof(mqtt_mppt_data_t));
		force = true;
	}
	mqtt_solar_run(force);
}

void mqtt_data_bms(mqtt_bms_data_t *data)
{
	bool force = false;

	if (memcmp(&mqtt_solar_context.bms, data, sizeof(mqtt_bms_data_t))) {
		memcpy(&mqtt_solar_context.bms, data, sizeof(mqtt_bms_data_t));
		force = true;
	}
	mqtt_solar_run(force);
}

void mqtt_data_internal_temp(float temp)
{
	bool force = false;

	if (mqtt_solar_context.internal_temp != temp) {
		mqtt_solar_context.internal_temp = temp;
		force = true;
	}
	mqtt_solar_run(force);
}
