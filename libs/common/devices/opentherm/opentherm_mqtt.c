// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"
#include "lwip/apps/mqtt.h"
#include "opentherm.h"

#define TIME_STR	64
#define MQTT_SEND_INTERVAL_MS 10000
#define IS_PIO_LOG(C) ((C) && LOG_MQTT_DEBUG)

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return -1; } \
				count += snprintf(ctx->mqtt.payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("MQTT %s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt.payload + count, len - count, _S_, __VA_ARGS__); }
static int mqtt_data_send(opentherm_context_t *ctx)
{
	static char time_buff[TIME_STR];
	int len = OTH_MQTT_DATA_LEN;
	int count = 0;
	int ret;

	/* Data */
	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"ch_set\":%3.2f", ctx->data.param_actual.ch_temperature_setpoint);
	ADD_MQTT_MSG_VAR(",\"dhw_set\":%3.2f", ctx->data.param_actual.dhw_temperature_setpoint);
	ADD_MQTT_MSG_VAR(",\"ch\":%d", ctx->data.status.ch_active);
	ADD_MQTT_MSG_VAR(",\"dhw\":%d", ctx->data.status.dhw_active);
	ADD_MQTT_MSG_VAR(",\"ch_enabled\":%d", ctx->data.status.ch_enabled);
	ADD_MQTT_MSG_VAR(",\"dhw_enabled\":%d", ctx->data.status.dhw_enabled);
	ADD_MQTT_MSG_VAR(",\"flame\":%d", ctx->data.status.flame_active);
	ADD_MQTT_MSG_VAR(",\"flow_temp\":%3.2f", ctx->data.data.flow_temperature);
	ADD_MQTT_MSG_VAR(",\"ret_temp\":%3.2f", ctx->data.data.return_temperature);
	ADD_MQTT_MSG_VAR(",\"exh_temp\":%d", ctx->data.data.exhaust_temperature);
	ADD_MQTT_MSG_VAR(",\"dhw_temp\":%3.2f", ctx->data.data.flame_current);
	ADD_MQTT_MSG_VAR(",\"ch_press\":%3.2f", ctx->data.data.ch_pressure);
	ADD_MQTT_MSG_VAR(",\"mdl_level\":%3.2f", ctx->data.data.modulation_level);
	ADD_MQTT_MSG_VAR(",\"flame_ua\":%3.2f", ctx->data.data.flame_current);
	ADD_MQTT_MSG_VAR(",\"ch_max\":%d", ctx->data.dev_config.ch_max_cfg);
	ADD_MQTT_MSG_VAR(",\"ch_min\":%d", ctx->data.dev_config.ch_min_cfg);
	ADD_MQTT_MSG_VAR(",\"dhw_max\":%d", ctx->data.dev_config.dhw_max_cfg);
	ADD_MQTT_MSG_VAR(",\"dhw_min\":%d", ctx->data.dev_config.dhw_min_cfg);
	ADD_MQTT_MSG("}");

	ctx->mqtt.payload[OTH_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ctx->mqtt.data, ctx->mqtt.payload);
	ctx->data.data.force = false;
	ctx->data.status.force = false;
	if (IS_PIO_LOG(ctx->log_mask))
		hlog_info(OTHM_MODULE, "Published %d bytes MQTT data: %d / %d",
					strlen(ctx->mqtt.payload), ret, ctx->mqtt.data->force);
	ctx->data.dev_config.force = false;
	return ret;
}

static int mqtt_errors_send(opentherm_context_t *ctx)
{
	static char time_buff[TIME_STR];
	int len = OTH_MQTT_DATA_LEN;
	int count = 0;
	int ret;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"diag\":%d", ctx->data.errors.diagnostic_event);
	ADD_MQTT_MSG_VAR(",\"service\":%d", ctx->data.errors.fault_svc_needed);
	ADD_MQTT_MSG_VAR(",\"fault\":%d", ctx->data.errors.fault_active);
	ADD_MQTT_MSG_VAR(",\"fault_lwp\":%d", ctx->data.errors.fault_low_water_pressure);
	ADD_MQTT_MSG_VAR(",\"fault_fl\":%d", ctx->data.errors.fault_flame);
	ADD_MQTT_MSG_VAR(",\"fault_lap\":%d", ctx->data.errors.fault_low_air_pressure);
	ADD_MQTT_MSG_VAR(",\"fault_hwt\":%d", ctx->data.errors.fault_high_water_temperature);
	ADD_MQTT_MSG_VAR(",\"fault_code\":%2d", ctx->data.errors.fault_code);
	ADD_MQTT_MSG_VAR(",\"fault_burn_start\":%2d", ctx->data.errors.fault_burner_starts);
	ADD_MQTT_MSG_VAR(",\"fault_low_flame\":%2d", ctx->data.errors.fault_flame_low);
	ADD_MQTT_MSG("}");

	ctx->mqtt.payload[OTH_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ctx->mqtt.errors, ctx->mqtt.payload);
	if (IS_PIO_LOG(ctx->log_mask))
		hlog_info(OTHM_MODULE, "Published %d bytes MQTT errors: %d / %d",
					strlen(ctx->mqtt.payload), ret, ctx->mqtt.errors->force);
	ctx->data.errors.force = false;
	return ret;
}

static int mqtt_stats_send(opentherm_context_t *ctx)
{
	static char time_buff[TIME_STR];
	int len = OTH_MQTT_DATA_LEN;
	struct tm dt = {0};
	int count = 0;
	int ret;

	/* Stats  */
	ADD_MQTT_MSG("{");
	time_msec2datetime(&dt, time_ms_since_boot() - ctx->data.stats.stat_reset_time);
	ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
	ADD_MQTT_MSG_VAR(",\"stat_reset_time\":\"%s\"", time_date2str(time_buff, TIME_STR, &dt));
	ADD_MQTT_MSG_VAR(",\"burner_starts\":%2d", ctx->data.stats.stat_burner_starts);
	ADD_MQTT_MSG_VAR(",\"ch_pump_starts\":%2d", ctx->data.stats.stat_ch_pump_starts);
	ADD_MQTT_MSG_VAR(",\"dhw_pump_starts\":%2d", ctx->data.stats.stat_dhw_pump_starts);
	ADD_MQTT_MSG_VAR(",\"dhw_burner_starts\":%2d", ctx->data.stats.stat_dhw_burn_burner_starts);
	ADD_MQTT_MSG_VAR(",\"burner_hours\":%2d", ctx->data.stats.stat_burner_hours);
	ADD_MQTT_MSG_VAR(",\"ch_pump_hours\":%2d", ctx->data.stats.stat_ch_pump_hours);
	ADD_MQTT_MSG_VAR(",\"dhw_pump_hours\":%2d", ctx->data.stats.stat_dhw_pump_hours);
	ADD_MQTT_MSG_VAR(",\"dhw_burner_hours\":%2d", ctx->data.stats.stat_dhw_burn_hours);
	ADD_MQTT_MSG("}");

	ctx->mqtt.payload[OTH_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ctx->mqtt.stats, ctx->mqtt.payload);
	if (IS_PIO_LOG(ctx->log_mask))
		hlog_info(OTHM_MODULE, "Published %d bytes MQTT statistics: %d / %d",
					strlen(ctx->mqtt.payload), ret, ctx->mqtt.stats->force);

	ctx->data.stats.force = false;

	return ret;
}

void opentherm_mqtt_send(opentherm_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	int ret;

	if (ctx->data.data.force || ctx->data.status.force || ctx->data.dev_config.force)
		ctx->mqtt.data->force = true;
	if (ctx->data.stats.force)
		ctx->mqtt.stats->force = true;
	if (ctx->data.errors.force)
		ctx->mqtt.errors->force = true;

	if (!mqtt_is_discovery_sent())
		return;

	if (ctx->mqtt.data->force) {
		mqtt_data_send(ctx);
		goto out;
	}
	if (ctx->mqtt.stats->force) {
		mqtt_stats_send(ctx);
		goto out;
	}
	if (ctx->mqtt.errors->force) {
		mqtt_errors_send(ctx);
		goto out;
	}

	if (ctx->mqtt.last_send &&
	    (now - ctx->mqtt.last_send) < MQTT_SEND_INTERVAL_MS)
		return;

	if (ctx->mqtt.send_id >= MQTT_SEND_MAX)
		ctx->mqtt.send_id = 0;

	switch (ctx->mqtt.send_id) {
	case MQTT_SEND_DATA:
		ret = mqtt_data_send(ctx);
		break;
	case MQTT_SEND_STATS:
		ret = mqtt_stats_send(ctx);
		break;
	case MQTT_SEND_ERR:
		ret = mqtt_errors_send(ctx);
		break;
	default:
		ret = 0;
		break;
	}
	if (!ret)
		ctx->mqtt.send_id++;

out:
	ctx->mqtt.last_send	= now;
}

#define MQTT_ADD_DATA_SENSOR(T, N, D, U) do {\
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE; \
	ctx->mqtt.mqtt_comp[i].platform = "sensor"; \
	ctx->mqtt.mqtt_comp[i].dev_class = (D); \
	ctx->mqtt.mqtt_comp[i].unit = (U); \
	ctx->mqtt.mqtt_comp[i].value_template = (T);\
	ctx->mqtt.mqtt_comp[i].name = (N);\
	ctx->mqtt.mqtt_comp[i].state_topic = ctx->mqtt.data->state_topic; \
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);\
} while (0)
#define MQTT_ADD_DATA_TEMP(T, N) MQTT_ADD_DATA_SENSOR(T, N, "temperature", "°C")

#define MQTT_ADD_DATA_BINARY(T, N) do {\
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE; \
	ctx->mqtt.mqtt_comp[i].platform = "binary_sensor";\
	ctx->mqtt.mqtt_comp[i].payload_on = "1";\
	ctx->mqtt.mqtt_comp[i].payload_off = "0";\
	ctx->mqtt.mqtt_comp[i].value_template = (T);\
	ctx->mqtt.mqtt_comp[i].name = (N);\
	ctx->mqtt.mqtt_comp[i].state_topic = ctx->mqtt.data->state_topic; \
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);\
} while (0)

#define MQTT_ADD_ERROR(T, N) do {\
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE; \
	ctx->mqtt.mqtt_comp[i].platform = "binary_sensor";\
	ctx->mqtt.mqtt_comp[i].payload_on = "1";\
	ctx->mqtt.mqtt_comp[i].payload_off = "0";\
	ctx->mqtt.mqtt_comp[i].value_template = (T);\
	ctx->mqtt.mqtt_comp[i].name = (N);\
	ctx->mqtt.mqtt_comp[i].state_topic = ctx->mqtt.errors->state_topic; \
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);\
} while (0)

#define MQTT_ADD_INT_ERROR(T, N) do {\
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE; \
	ctx->mqtt.mqtt_comp[i].platform = "sensor";\
	ctx->mqtt.mqtt_comp[i].value_template = (T);\
	ctx->mqtt.mqtt_comp[i].name = (N);\
	ctx->mqtt.mqtt_comp[i].state_topic = ctx->mqtt.errors->state_topic; \
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);\
} while (0)

#define MQTT_ADD_STAT(T, N) do {\
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE; \
	ctx->mqtt.mqtt_comp[i].platform = "sensor";\
	ctx->mqtt.mqtt_comp[i].value_template = (T);\
	ctx->mqtt.mqtt_comp[i].name = (N);\
	ctx->mqtt.mqtt_comp[i].state_topic = ctx->mqtt.stats->state_topic; \
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);\
} while (0)

void opentherm_mqtt_init(opentherm_context_t *ctx)
{
	int i = 0;

	/* Data */
	ctx->mqtt.data = &ctx->mqtt.mqtt_comp[i];
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE;
	ctx->mqtt.mqtt_comp[i].platform = "sensor";
	ctx->mqtt.mqtt_comp[i].dev_class = "temperature";
	ctx->mqtt.mqtt_comp[i].unit = "°C";
	ctx->mqtt.mqtt_comp[i].value_template = "{{ value_json['ch_set'] }}";
	ctx->mqtt.mqtt_comp[i].name = "CH_set";
	ctx->mqtt.data->force = true;
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);
	MQTT_ADD_DATA_TEMP("{{ value_json['dhw_set'] }}", "DHW_set");
	MQTT_ADD_DATA_TEMP("{{ value_json['flow_temp'] }}", "Flow_temperature");
	MQTT_ADD_DATA_TEMP("{{ value_json['ret_temp'] }}", "Return_temperature");
	MQTT_ADD_DATA_TEMP("{{ value_json['exh_temp'] }}", "Exhaust_temperature");
	MQTT_ADD_DATA_TEMP("{{ value_json['dhw_temp'] }}", "DHW_temperature");
	MQTT_ADD_DATA_TEMP("{{ value_json['ch_max'] }}", "CH_max");
	MQTT_ADD_DATA_TEMP("{{ value_json['ch_min'] }}", "CH_min");
	MQTT_ADD_DATA_TEMP("{{ value_json['dhw_max'] }}", "DHW_max");
	MQTT_ADD_DATA_TEMP("{{ value_json['dhw_min'] }}", "DHW_min");
	MQTT_ADD_DATA_SENSOR("{{ value_json['ch_press'] }}", "CH_press", "pressure", "bar");
	MQTT_ADD_DATA_SENSOR("{{ value_json['mdl_level'] }}", "Mod_level", NULL, "%");
	MQTT_ADD_DATA_SENSOR("{{ value_json['flame_ua'] }}", "Flame_ua", NULL, "uA");
	MQTT_ADD_DATA_BINARY("{{ value_json['ch'] }}", "CH");
	MQTT_ADD_DATA_BINARY("{{ value_json['dhw'] }}", "DHW");
	MQTT_ADD_DATA_BINARY("{{ value_json['ch_enabled'] }}", "CH_enabled");
	MQTT_ADD_DATA_BINARY("{{ value_json['dhw_enabled'] }}", "DHW_enabled");
	MQTT_ADD_DATA_BINARY("{{ value_json['flame'] }}", "Flame");

	/* Errors  */
	ctx->mqtt.errors = &ctx->mqtt.mqtt_comp[i];
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE;
	ctx->mqtt.mqtt_comp[i].platform = "binary_sensor";
	ctx->mqtt.mqtt_comp[i].dev_class = "problem";
	ctx->mqtt.mqtt_comp[i].payload_on = "1";
	ctx->mqtt.mqtt_comp[i].payload_off = "0";
	ctx->mqtt.mqtt_comp[i].value_template = "{{ value_json['diag'] }}";
	ctx->mqtt.mqtt_comp[i].name = "Diagnostic";
	ctx->mqtt.errors->force = true;
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);
	MQTT_ADD_ERROR("{{ value_json['service'] }}", "Service");
	MQTT_ADD_ERROR("{{ value_json['fault'] }}", "Fault");
	MQTT_ADD_ERROR("{{ value_json['fault_lwp'] }}", "Low_Water_Pressure");
	MQTT_ADD_ERROR("{{ value_json['fault_fl'] }}", "Fault_Flame");
	MQTT_ADD_ERROR("{{ value_json['fault_lap'] }}", "Low_Air_Pressure");
	MQTT_ADD_ERROR("{{ value_json['fault_hwt'] }}", "High_Water_Temperature");
	MQTT_ADD_INT_ERROR("{{ value_json['fault_code'] }}", "Fault_Code");
	MQTT_ADD_INT_ERROR("{{ value_json['fault_burn_start'] }}", "Fault_Burner_Start");
	MQTT_ADD_INT_ERROR("{{ value_json['fault_low_flame'] }}", "Fault_Low_Flame");

	/* Stats  */
	ctx->mqtt.stats = &ctx->mqtt.mqtt_comp[i];
	ctx->mqtt.mqtt_comp[i].module = OTHM_MODULE;
	ctx->mqtt.mqtt_comp[i].platform = "sensor";
	ctx->mqtt.mqtt_comp[i].value_template = "{{ value_json['stat_reset_time'] }}";
	ctx->mqtt.mqtt_comp[i].name = "Stat_Reset_Time";
	ctx->mqtt.stats->force = true;
	mqtt_msg_component_register(&ctx->mqtt.mqtt_comp[i++]);
	MQTT_ADD_STAT("{{ value_json['burner_starts'] }}", "Burner_Starts");
	MQTT_ADD_STAT("{{ value_json['ch_pump_starts'] }}", "CH_Pump_Starts");
	MQTT_ADD_STAT("{{ value_json['dhw_pump_starts'] }}", "DHW_Pump_Starts");
	MQTT_ADD_STAT("{{ value_json['dhw_burner_starts'] }}", "DHW_Burner_Starts");
	MQTT_ADD_STAT("{{ value_json['burner_hours'] }}", "Burner_Hours");
	MQTT_ADD_STAT("{{ value_json['ch_pump_hours'] }}", "CH_Pump_Hours");
	MQTT_ADD_STAT("{{ value_json['dhw_pump_hours'] }}", "DHW_Pump_Hours");
	MQTT_ADD_STAT("{{ value_json['dhw_burner_hours'] }}", "DHW_Burner_Hours");
}
