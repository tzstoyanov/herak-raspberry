// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"
#include "lwip/apps/mqtt.h"
#include "bms_jk.h"

#define TIME_STR	64
#define MQTT_SEND_INTERVAL_MS 20000
#define IS_MQTT_LOG(C) ((C) && LOG_MQTT_DEBUG)

enum {
	MQTT_SEND_CELL_V = 0,
	MQTT_SEND_CELL_R,
	MQTT_SEND_DATA,
	MQTT_SEND_DEV,
	MQTT_SEND_MAX
};

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return -1; } \
				count += snprintf(dev->mqtt.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(dev->mqtt.payload + count, len - count, _S_, __VA_ARGS__); }
static int mqtt_cells_v_send(struct jk_bms_dev_t *dev)
{
	static char time_buff[TIME_STR];
	int len = BMS_MQTT_DATA_LEN;
	int count = 0;
	int ret;
	int i;

	if (!dev->cell_info.valid)
		return 0;
	/* Cells voltage */
	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		ADD_MQTT_MSG_VAR(",\"cell_%d_v\":%3.2f", i, dev->cell_info.cells_v[i] * 0.001);
	}
	ADD_MQTT_MSG("}");

	dev->mqtt.payload[BMS_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(dev->mqtt.cells_v, dev->mqtt.payload);
	dev->cell_info.cell_v_force = false;
	if (IS_MQTT_LOG(dev->ctx->debug))
		hlog_info(BMS_JK_MODULE, "Published %d bytes MQTT cells voltages: %d", strlen(dev->mqtt.payload), ret);
	return ret;
}

static int mqtt_cells_r_send(struct jk_bms_dev_t *dev)
{
	static char time_buff[TIME_STR];
	int len = BMS_MQTT_DATA_LEN;
	int count = 0;
	int ret;
	int i;

	if (!dev->cell_info.valid)
		return 0;

	/* Cells voltage */
	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		ADD_MQTT_MSG_VAR(",\"cell_%d_r\":%3.2f", i, dev->cell_info.cells_res[i] * 0.001);
	}
	ADD_MQTT_MSG("}");

	dev->mqtt.payload[BMS_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(dev->mqtt.cells_res, dev->mqtt.payload);
	dev->cell_info.cell_r_force = false;
	if (IS_MQTT_LOG(dev->ctx->debug))
		hlog_info(BMS_JK_MODULE, "Published %d bytes MQTT cells resistances: %d", strlen(dev->mqtt.payload), ret);
	return ret;
}

static int mqtt_cells_data_send(struct jk_bms_dev_t *dev)
{
	static char time_buff[TIME_STR];
	int len = BMS_MQTT_DATA_LEN;
	int count = 0;
	int ret;

	if (!dev->cell_info.valid)
		return 0;

	/* Cells info  */
	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"v_avg\":%3.2f", dev->cell_info.v_avg*0.001);
	ADD_MQTT_MSG_VAR(",\"v_delta\":%3.2f", dev->cell_info.v_delta*0.001);
	ADD_MQTT_MSG_VAR(",\"cell_v_min\":%d", dev->cell_info.cell_v_min);
	ADD_MQTT_MSG_VAR(",\"cell_v_max\":%d", dev->cell_info.cell_v_max);
	ADD_MQTT_MSG_VAR(",\"batt_action\":%d", dev->cell_info.batt_action);
	ADD_MQTT_MSG_VAR(",\"power_temp\":%3.2f", dev->cell_info.power_temp*0.1);
	ADD_MQTT_MSG_VAR(",\"batt_temp1\":%3.2f", dev->cell_info.batt_temp1*0.1);
	ADD_MQTT_MSG_VAR(",\"batt_temp2\":%3.2f", dev->cell_info.batt_temp2*0.1);
	ADD_MQTT_MSG_VAR(",\"batt_temp_mos\":%3.2f", dev->cell_info.batt_temp_mos*0.1);
	ADD_MQTT_MSG_VAR(",\"batt_volt\":%3.2f", dev->cell_info.batt_volt*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_power\":%ld", dev->cell_info.batt_power);
	ADD_MQTT_MSG_VAR(",\"batt_state\":%d", dev->cell_info.batt_state);
	ADD_MQTT_MSG_VAR(",\"batt_cycles\":%ld", dev->cell_info.batt_cycles);
	ADD_MQTT_MSG_VAR(",\"batt_charge_curr\":%3.2f", dev->cell_info.batt_charge_curr*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_balance_curr\":%3.2f", dev->cell_info.batt_balance_curr*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_cap_rem\":%3.2f", dev->cell_info.batt_cap_rem*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_cap_nom\":%3.2f", dev->cell_info.batt_cap_nom*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_cycles_cap\":%3.2f", dev->cell_info.batt_cycles_cap*0.001);
	ADD_MQTT_MSG_VAR(",\"soh\":%d", dev->cell_info.soh);
	ADD_MQTT_MSG_VAR(",\"batt_v\":%3.2f", dev->cell_info.batt_v*0.001);
	ADD_MQTT_MSG_VAR(",\"batt_heat_a\":%3.2f", dev->cell_info.batt_heat_a*0.001);
	if (dev->track_batt_level)
		ADD_MQTT_MSG_VAR(",\"batt_low\":%d", !dev->full_battery);
	ADD_MQTT_MSG("}");

	dev->mqtt.payload[BMS_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(dev->mqtt.bms_data, dev->mqtt.payload);
	dev->cell_info.data_force = false;
	if (IS_MQTT_LOG(dev->ctx->debug))
		hlog_info(BMS_JK_MODULE, "Published %d bytes MQTT cells info: %d", strlen(dev->mqtt.payload), ret);
	return ret;
}

static int mqtt_dev_info_send(struct jk_bms_dev_t *dev)
{
	int len = BMS_MQTT_DATA_LEN;
	char time_buff[TIME_STR];
	struct tm date;
	int count = 0;
	int ret;

	if (!dev->dev_info.valid)
		return 0;

	time_msec2datetime(&date, dev->dev_info.Uptime * 1000);
	/* Device Info  */
	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"Vendor\":\"%s\"", dev->dev_info.Vendor);
	ADD_MQTT_MSG_VAR(",\"Model\":\"%s\"", dev->dev_info.Model);
	ADD_MQTT_MSG_VAR(",\"Hardware\":\"%s\"", dev->dev_info.Hardware);
	ADD_MQTT_MSG_VAR(",\"Software\":\"%s\"", dev->dev_info.Software);
	ADD_MQTT_MSG_VAR(",\"SerialN\":\"%s\"", dev->dev_info.SerialN);
	ADD_MQTT_MSG_VAR(",\"Uptime\":%s", time_date2str(time_buff, TIME_STR, &date));
	ADD_MQTT_MSG_VAR(",\"PowerOnCount\":%d", dev->dev_info.PowerOnCount);
	ADD_MQTT_MSG("}");

	dev->mqtt.payload[BMS_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(dev->mqtt.bms_info, dev->mqtt.payload);
	dev->cell_info.dev_force = false;
	if (IS_MQTT_LOG(dev->ctx->debug))
		hlog_info(BMS_JK_MODULE, "Published %d bytes MQTT device info: %d", strlen(dev->mqtt.payload), ret);
	return ret;
}

void bms_jk_mqtt_send(struct jk_bms_dev_t *dev)
{
	uint64_t now = time_ms_since_boot();
	int ret;

	if (!mqtt_is_discovery_sent())
		return;

	if (dev->cell_info.cell_v_force)
		dev->mqtt.cells_v->force = true;
	if (dev->cell_info.cell_r_force)
		dev->mqtt.cells_res->force = true;
	if (dev->cell_info.data_force)
		dev->mqtt.bms_data->force = true;
	if (dev->cell_info.dev_force)
		dev->mqtt.bms_info->force = true;

	if (dev->mqtt.cells_v->force) {
		mqtt_cells_v_send(dev);
		goto out;
	}
	if (dev->mqtt.cells_res->force) {
		mqtt_cells_r_send(dev);
		goto out;
	}
	if (dev->mqtt.bms_data->force) {
		mqtt_cells_data_send(dev);
		goto out;
	}
	if (dev->mqtt.bms_info->force) {
		mqtt_dev_info_send(dev);
		goto out;
	}

	if (dev->mqtt.last_send &&
	    (now - dev->mqtt.last_send) < MQTT_SEND_INTERVAL_MS)
		return;

	if (dev->mqtt.send_id >= MQTT_SEND_MAX)
		dev->mqtt.send_id = 0;

	switch (dev->mqtt.send_id) {
	case MQTT_SEND_CELL_V:
		ret = mqtt_cells_v_send(dev);
		break;
	case MQTT_SEND_CELL_R:
		ret = mqtt_cells_r_send(dev);
		break;
	case MQTT_SEND_DATA:
		ret = mqtt_cells_data_send(dev);
		break;
	case MQTT_SEND_DEV:
		ret = mqtt_dev_info_send(dev);
		break;
	default:
		ret = 0;
		break;
	}
	if (!ret)
		dev->mqtt.send_id++;

out:
	dev->mqtt.last_send	= now;
}

#define MQTT_ADD_CELL_V(T, N) do {\
	mqtt->mqtt_comp[i].module = mod_name; \
	mqtt->mqtt_comp[i].platform = "sensor"; \
	mqtt->mqtt_comp[i].dev_class = "voltage"; \
	mqtt->mqtt_comp[i].unit = "V"; \
	mqtt->mqtt_comp[i].value_template = (T);\
	mqtt->mqtt_comp[i].name = (N);\
	mqtt->mqtt_comp[i].state_topic = mqtt->cells_v->state_topic; \
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);\
	} while (0)

#define MQTT_ADD_CELL_RES(T, N) do {\
	mqtt->mqtt_comp[i].module = mod_name; \
	mqtt->mqtt_comp[i].platform = "sensor"; \
	mqtt->mqtt_comp[i].unit = "ohms"; \
	mqtt->mqtt_comp[i].value_template = (T);\
	mqtt->mqtt_comp[i].name = (N);\
	mqtt->mqtt_comp[i].state_topic = mqtt->cells_res->state_topic; \
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);\
	} while (0)

#define MQTT_ADD_BMS_DATA(T, N, D, U) do {\
	mqtt->mqtt_comp[i].module = mod_name; \
	mqtt->mqtt_comp[i].platform = "sensor"; \
	mqtt->mqtt_comp[i].dev_class = (D); \
	mqtt->mqtt_comp[i].unit = (U); \
	mqtt->mqtt_comp[i].value_template = (T);\
	mqtt->mqtt_comp[i].name = (N);\
	mqtt->mqtt_comp[i].state_topic = mqtt->bms_data->state_topic; \
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);\
	} while (0)

#define MQTT_ADD_BMS_DEV(T, N) do {\
	mqtt->mqtt_comp[i].module = mod_name; \
	mqtt->mqtt_comp[i].platform = "sensor"; \
	mqtt->mqtt_comp[i].value_template = (T);\
	mqtt->mqtt_comp[i].name = (N);\
	mqtt->mqtt_comp[i].state_topic = mqtt->bms_info->state_topic; \
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);\
	} while (0)

void bms_jk_mqtt_init(bms_context_t *ctx, int idx)
{
	bms_jk_mqtt_t *mqtt = &ctx->devices[idx]->mqtt;
	char *name, *templ, *mod_name;
	int i = 0, j;

	sys_asprintf(&mod_name, "%s%d", BMS_JK_MODULE, idx);
	/* Cells V */
	mqtt->cells_v = &mqtt->mqtt_comp[i];
	mqtt->mqtt_comp[i].module = mod_name;
	mqtt->mqtt_comp[i].platform = "sensor";
	mqtt->mqtt_comp[i].dev_class = "voltage";
	mqtt->mqtt_comp[i].unit = "V";
	mqtt->mqtt_comp[i].name = "cell_0_v";
	mqtt->mqtt_comp[i].value_template = "{{ value_json['cell_0_v'] }}";
	mqtt->cells_v->force = true;
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);
	for (j = 1; j < BMS_MAX_CELLS; j++) {
		sys_asprintf(&name, "cell_%d_v", j);
		sys_asprintf(&templ, "{{ value_json['cell_%d_v'] }}", j);
		MQTT_ADD_CELL_V(templ, name);
	}

	/* Cells Res */
	mqtt->cells_res = &mqtt->mqtt_comp[i];
	mqtt->mqtt_comp[i].module = mod_name;
	mqtt->mqtt_comp[i].platform = "sensor";
	mqtt->mqtt_comp[i].unit = "ohms";
	mqtt->mqtt_comp[i].name = "cell_0_r";
	mqtt->mqtt_comp[i].value_template = "{{ value_json['cell_0_r'] }}";
	mqtt->cells_res->force = true;
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);
	for (j = 1; j < BMS_MAX_CELLS; j++) {
		sys_asprintf(&name, "cell_%d_r", j);
		sys_asprintf(&templ, "{{ value_json['cell_%d_r'] }}", j);
		MQTT_ADD_CELL_RES(templ, name);
	}

	/* Data */
	mqtt->bms_data = &mqtt->mqtt_comp[i];
	mqtt->mqtt_comp[i].module = mod_name;
	mqtt->mqtt_comp[i].platform = "sensor";
	mqtt->mqtt_comp[i].dev_class = "voltage";
	mqtt->mqtt_comp[i].unit = "V";
	mqtt->mqtt_comp[i].name = "v_avg";
	mqtt->mqtt_comp[i].value_template = "{{ value_json['v_avg'] }}";
	mqtt->bms_data->force = true;
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);
	MQTT_ADD_BMS_DATA("{{ value_json['v_delta'] }}", "v_delta", "voltage", "V");
	MQTT_ADD_BMS_DATA("{{ value_json['cell_v_min'] }}", "cell_v_min", NULL, NULL);
	MQTT_ADD_BMS_DATA("{{ value_json['cell_v_max'] }}", "cell_v_max", NULL, NULL);
	MQTT_ADD_BMS_DATA("{{ value_json['batt_action'] }}", "batt_action", NULL, NULL);
	MQTT_ADD_BMS_DATA("{{ value_json['power_temp'] }}", "power_temp", "temperature", "°C");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_temp1'] }}", "batt_temp1", "temperature", "°C");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_temp2'] }}", "batt_temp2", "temperature", "°C");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_temp_mos'] }}", "batt_temp_mos", "temperature", "°C");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_volt'] }}", "batt_volt", "voltage", "V");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_power'] }}", "batt_power", NULL, NULL);
	MQTT_ADD_BMS_DATA("{{ value_json['batt_state'] }}", "batt_state", NULL, "%");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_cycles'] }}", "batt_cycles", NULL, NULL);
	MQTT_ADD_BMS_DATA("{{ value_json['batt_charge_curr'] }}", "batt_charge_curr", "current", "A");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_balance_curr'] }}", "batt_balance_curr", "current", "A");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_cap_rem'] }}", "batt_cap_rem", NULL, "Ah");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_cap_nom'] }}", "batt_cap_nom", NULL, "Ah");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_cycles_cap'] }}", "batt_cycles_cap", NULL, "Ah");
	MQTT_ADD_BMS_DATA("{{ value_json['soh'] }}", "soh", NULL, "%");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_v'] }}", "batt_v", "voltage", "V");
	MQTT_ADD_BMS_DATA("{{ value_json['batt_heat_a'] }}", "batt_heat_a", "current", "A");
	mqtt->mqtt_comp[i].module = mod_name;
	mqtt->mqtt_comp[i].platform = "binary_sensor";
	mqtt->mqtt_comp[i].value_template = "{{ value_json['batt_low'] }}";
	mqtt->mqtt_comp[i].payload_on = "1";
	mqtt->mqtt_comp[i].payload_off = "0";
	mqtt->mqtt_comp[i].name = "batt_low";
	mqtt->mqtt_comp[i].state_topic = mqtt->bms_data->state_topic;
	mqtt_msg_component_register(&mqtt->mqtt_comp[i++]);

	/* Device */
	mqtt->bms_info = &mqtt->mqtt_comp[i];
	mqtt->mqtt_comp[i].module = mod_name;
	mqtt->mqtt_comp[i].platform = "sensor";
	mqtt->mqtt_comp[i].name = "Vendor";
	mqtt->mqtt_comp[i].value_template = "{{ value_json['Vendor'] }}";
	mqtt->bms_info->force = true;
	mqtt_msg_component_register(&mqtt->mqtt_comp[i]);
	MQTT_ADD_BMS_DEV("{{ value_json['Model'] }}", "Model");
	MQTT_ADD_BMS_DEV("{{ value_json['Hardware'] }}", "Hardware");
	MQTT_ADD_BMS_DEV("{{ value_json['Software'] }}", "Software");
	MQTT_ADD_BMS_DEV("{{ value_json['SerialN'] }}", "SerialN");
	MQTT_ADD_BMS_DEV("{{ value_json['Uptime'] }}", "Uptime");
	MQTT_ADD_BMS_DEV("{{ value_json['PowerOnCount'] }}", "PowerOnCount");
}
