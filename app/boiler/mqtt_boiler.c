// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"
#include "lwip/apps/mqtt.h"
#include "boiler.h"

#define MQTTLOG	"mqtt"
#define MQTT_DATA_LEN	768
#define SMALL_STR_LEN	8

#define IS_MQTT_LOG (boiler_dbg_check(LOG_MQTT_DEBUG))
#define COMPONENTS_NUM	1
typedef struct {
	char dev_hw_ver[SMALL_STR_LEN];
	char dev_model[SMALL_STR_LEN];
	char dev_sw_ver[SMALL_STR_LEN];
	mqtt_component_t components[COMPONENTS_NUM];
} mqtt_boiler_discovery_t;

static struct {
	float internal_temp;
	opentherm_data_t data;
	char payload[MQTT_DATA_LEN + 1];
	bool force;
	mqtt_boiler_discovery_t discovery;
} mqtt_boiler_context = {0};

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return; } \
				count += snprintf(mqtt_boiler_context.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return; } \
				     count += snprintf(mqtt_boiler_context.payload + count, len - count, _S_, __VA_ARGS__); }
static void mqtt_data_send(bool force)
{
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	datetime_t dt = {0};
	int count = 0;

	ADD_MQTT_MSG("{");
	/* Data */
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"in_temp\":%3.2f", mqtt_boiler_context.internal_temp);
	ADD_MQTT_MSG_VAR(",\"ch_set\":%3.2f", mqtt_boiler_context.data.param_actual.ch_temperature_setpoint);
	ADD_MQTT_MSG_VAR(",\"dhw_set\":%3.2f", mqtt_boiler_context.data.param_actual.dhw_temperature_setpoint);
	ADD_MQTT_MSG_VAR(",\"ch\":%d", mqtt_boiler_context.data.ch_active);
	ADD_MQTT_MSG_VAR(",\"dhw\":%d", mqtt_boiler_context.data.dhw_active);
	ADD_MQTT_MSG_VAR(",\"ch_enabled\":%d", mqtt_boiler_context.data.ch_enabled);
	ADD_MQTT_MSG_VAR(",\"dhw_enabled\":%d", mqtt_boiler_context.data.dhw_enabled);
	ADD_MQTT_MSG_VAR(",\"flame\":%d", mqtt_boiler_context.data.flame_active);
	ADD_MQTT_MSG_VAR(",\"flow_temp\":%3.2f", mqtt_boiler_context.data.flow_temperature);
	ADD_MQTT_MSG_VAR(",\"ret_temp\":%3.2f", mqtt_boiler_context.data.return_temperature);
	ADD_MQTT_MSG_VAR(",\"exh_temp\":%d", mqtt_boiler_context.data.exhaust_temperature);
	ADD_MQTT_MSG_VAR(",\"dhw_temp\":%3.2f", mqtt_boiler_context.data.dhw_temperature);
	ADD_MQTT_MSG_VAR(",\"ch_press\":%3.2f", mqtt_boiler_context.data.ch_pressure);
	ADD_MQTT_MSG_VAR(",\"mdl_level\":%3.2f", mqtt_boiler_context.data.modulation_level);
	ADD_MQTT_MSG_VAR(",\"flame_ua\":%3.2f", mqtt_boiler_context.data.flame_current);
	ADD_MQTT_MSG_VAR(",\"ch_max\":%d", mqtt_boiler_context.data.ch_max_cfg);
	ADD_MQTT_MSG_VAR(",\"ch_min\":%d", mqtt_boiler_context.data.ch_min_cfg);
	ADD_MQTT_MSG_VAR(",\"dhw_max\":%d", mqtt_boiler_context.data.dhw_max_cfg);
	ADD_MQTT_MSG_VAR(",\"dhw_min\":%d", mqtt_boiler_context.data.dhw_min_cfg);

	/* Errors  */
	ADD_MQTT_MSG_VAR(",\"diag\":%d", mqtt_boiler_context.data.diagnostic_event);
	ADD_MQTT_MSG_VAR(",\"service\":%d", mqtt_boiler_context.data.fault_svc_needed);
	ADD_MQTT_MSG_VAR(",\"fault\":%d", mqtt_boiler_context.data.fault_active);
	ADD_MQTT_MSG_VAR(",\"fault_lwp\":%d", mqtt_boiler_context.data.fault_low_water_pressure);
	ADD_MQTT_MSG_VAR(",\"fault_fl\":%d", mqtt_boiler_context.data.fault_flame);
	ADD_MQTT_MSG_VAR(",\"fault_lap\":%d", mqtt_boiler_context.data.fault_low_air_pressure);
	ADD_MQTT_MSG_VAR(",\"fault_hwt\":%d", mqtt_boiler_context.data.fault_high_water_temperature);
	ADD_MQTT_MSG_VAR(",\"fault_code\":%2d", mqtt_boiler_context.data.fault_code);
	ADD_MQTT_MSG_VAR(",\"fault_burn_start\":%2d", mqtt_boiler_context.data.fault_burner_starts);
	ADD_MQTT_MSG_VAR(",\"fault_low_flame\":%2d", mqtt_boiler_context.data.fault_flame_low);

	/* Stats  */
	time_msec2datetime(&dt, time_ms_since_boot() - mqtt_boiler_context.data.stat_reset_time);
	ADD_MQTT_MSG_VAR(",\"stat_reset_time\":\"%s\"", time_date2str(time_buff, TIME_STR, &dt));
	ADD_MQTT_MSG_VAR(",\"burner_starts\":%2d", mqtt_boiler_context.data.stat_burner_starts);
	ADD_MQTT_MSG_VAR(",\"ch_pump_starts\":%2d", mqtt_boiler_context.data.stat_ch_pump_starts);
	ADD_MQTT_MSG_VAR(",\"dhw_pump_starts\":%2d", mqtt_boiler_context.data.stat_dhw_pump_starts);
	ADD_MQTT_MSG_VAR(",\"dhw_burner_starts\":%2d", mqtt_boiler_context.data.stat_dhw_burn_burner_starts);
	ADD_MQTT_MSG_VAR(",\"burner_hours\":%2d", mqtt_boiler_context.data.stat_burner_hours);
	ADD_MQTT_MSG_VAR(",\"ch_pump_hours\":%2d", mqtt_boiler_context.data.stat_ch_pump_hours);
	ADD_MQTT_MSG_VAR(",\"dhw_pump_hours\":%2d", mqtt_boiler_context.data.stat_dhw_pump_hours);
	ADD_MQTT_MSG_VAR(",\"dhw_burner_hours\":%2d", mqtt_boiler_context.data.stat_dhw_burn_hours);

	ADD_MQTT_MSG("}");

	mqtt_boiler_context.payload[MQTT_DATA_LEN] = 0;
	mqtt_msg_publish(NULL, mqtt_boiler_context.payload, force);
}

void mqtt_data_internal_temp(float temp)
{
	if (mqtt_boiler_context.internal_temp != temp) {
		mqtt_boiler_context.internal_temp = temp;
		mqtt_boiler_context.force = true;
	}
}

void mqtt_boiler_data(opentherm_context_t *boiler)
{
	if (memcmp(&boiler->data, &mqtt_boiler_context.data, sizeof(opentherm_data_t))) {
		memcpy(&mqtt_boiler_context.data, &boiler->data, sizeof(opentherm_data_t));
		mqtt_boiler_context.force = true;
	}
}

#define DEV_QOS    2
#define ORG_NAME   "OpenTherm"
#define ORG_VER	   "2.2"
static int mqtt_boiler_discovery_add(opentherm_data_t *boiler)
{
	mqtt_component_t *comps = mqtt_boiler_context.discovery.components;
	
	/* 1 */
	comps[0].name = "Chip_Temperature";
	comps[0].platform = "sensor";
	comps[0].dev_class = "temperature";
	comps[0].unit = "°C";
	comps[0].value_template = "{{value_json.in_temp}}";

	return mqtt_msg_component_register(&mqtt_boiler_context.discovery.components[0]);
}

void mqtt_boiler_send(opentherm_context_t *boiler)
{
	mqtt_data_send(mqtt_boiler_context.force);
	mqtt_boiler_context.force = false;
}


void mqtt_boiler_init(opentherm_context_t *boiler)
{
	mqtt_boiler_discovery_add(&boiler->data);
}
