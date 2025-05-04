// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "opentherm.h"

#define CMD_SEND_INTERVAL_MS	1000
#define CMD_ERR_INTERVAL_MS		10000
#define CMD_STATS_INTERVAL_MS	10000
#define CMD_CFG_INTERVAL_MS		60000
#define CMD_FIND_INTERVAL_MS	60000 // 1 min
#define CMD_SUPPORTED_RETRIES	10

#define CMD_READ	0x01
#define CMD_WRITE	0x02

#define IS_CMD_LOG(C) ((C) && LOG_OCMD_DEBUG)

opentherm_cmd_response_t
opentherm_dev_read(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value)
{
	opentherm_msg_t req = {0}, repl = {0};

	req.id = cmd;
	req.msg_type = MSG_TYPE_READ_DATA;
	req.value = send;
	if (opentherm_dev_pio_exchange(&ctx->pio, &req, &repl))
		return CMD_RESPONSE_L1_ERR;

	if (repl.msg_type == MSG_TYPE_READ_ACK && repl.id == cmd) {
		if (value)
			*value = repl.value;
		return CMD_RESPONSE_OK;
	}

	if (IS_CMD_LOG(ctx->log_mask))
		hlog_warning(OTHM_MODULE, "Not expected read msg received for commands %d: %d",
					 repl.id, repl.msg_type);
	if (repl.msg_type == MSG_TYPE_DATA_INVALID)
		return CMD_RESPONSE_INVALID;
	if (repl.msg_type == MSG_TYPE_UNKNOWN_DATA_ID)
		return CMD_RESPONSE_UNKNOWN;

	return CMD_RESPONSE_WRONG_PARAM;
}

opentherm_cmd_response_t
opentherm_dev_write(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value)
{
	opentherm_msg_t req = {0}, repl = {0};

	req.id = cmd;
	req.msg_type = MSG_TYPE_WRITE_DATA;
	req.value = send;
	if (opentherm_dev_pio_exchange(&ctx->pio, &req, &repl))
		return CMD_RESPONSE_L1_ERR;

	if (repl.msg_type == MSG_TYPE_WRITE_ACK && repl.id == cmd) {
		if (value)
			*value = repl.value;
		return CMD_RESPONSE_OK;
	}

	if (IS_CMD_LOG(ctx->log_mask))
		hlog_warning(OTHM_MODULE, "Not expected write msg received: cmd %d, type %d",
					 repl.id, repl.msg_type);

	if (repl.msg_type == MSG_TYPE_DATA_INVALID)
		return CMD_RESPONSE_INVALID;
	if (repl.msg_type == MSG_TYPE_UNKNOWN_DATA_ID)
		return CMD_RESPONSE_UNKNOWN;

	return CMD_RESPONSE_WRONG_PARAM;
}

static opentherm_cmd_response_t
opentherm_cmd_uint16(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_dev_write(ctx, cmd, out?out->u16:0, &v);
	else
		ret = opentherm_dev_read(ctx, cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in)
		in->u16 = v;
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_int16(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_dev_write(ctx, cmd, out?out->u16:0, &v);
	else
		ret = opentherm_dev_read(ctx, cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) // Signed int16 conversion
		in->i16 = (int16_t)((v ^ 0x8000) - 0x8000);
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_float(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	int16_t i16, s = 0;
	uint16_t v = 0;

	if (out) { // Fixed-point 8.8 conversion
		if (out->f >= 0)
			s = 0x100*out->f;
		else
			s = 0x10000 - (0x100*out->f);
	}
	if (write)
		ret = opentherm_dev_write(ctx, cmd, s, &v);
	else
		ret	= opentherm_dev_read(ctx, cmd, s, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Fixed-point 8.8 conversion
		i16 = (int16_t)((v ^ 0x8000) - 0x8000);
		in->f = (float)i16 / 256.0f;
	}
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_int8arr(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_dev_write(ctx, cmd, out?out->u16:0, &v);
	else
		ret = opentherm_dev_read(ctx, cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Signed 8-bit conversion
		in->i8arr[0] = (int8_t)(((v & 0xFF) ^ 0x80) - 0x80);
		in->i8arr[1] = (int8_t)(((v >> 8) ^ 0x80) - 0x80);
	}
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_uint8arr(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_dev_write(ctx, cmd, out?out->u16:0, &v);
	else
		ret = opentherm_dev_read(ctx, cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Unsigned 8-bit conversion
		in->u8arr[0] = (int8_t)((v & 0xFF));
		in->u8arr[1] = (int8_t)((v >> 8));
	}
	return ret;
}

static int ot_cmd_read(opentherm_context_t *ctx, int id, ot_data_t *out, ot_data_t *in)
{
	ot_commands_t *cmd;
	int ret;

	if (id >= DATA_ID_CMD_MAX)
		return -1;
	if (!(ctx->dev.ot_commands[id].cmd_type & CMD_READ))
		return -1;
	if (!ctx->dev.ot_commands[id].supported)
		return -1;
	cmd = &ctx->dev.ot_commands[id];
	ret = cmd->func(ctx, id, out, in, false);
	if (ret == CMD_RESPONSE_UNKNOWN)
		ctx->dev.ot_commands[id].supported--;
	if (!ctx->dev.ot_commands[id].supported)
		hlog_warning(OTHM_MODULE, "Command %d is not supported by the OT device", id);

	return  ret == CMD_RESPONSE_OK ? 0 : -1;
}

static int ot_cmd_write(opentherm_context_t *ctx, int id, ot_data_t *out, ot_data_t *in)
{
	ot_commands_t *cmd;
	int ret;

	if (id >= DATA_ID_CMD_MAX)
		return -1;
	if (!(ctx->dev.ot_commands[id].cmd_type & CMD_WRITE))
		return -1;
	if (!ctx->dev.ot_commands[id].supported)
		return -1;
	cmd = &ctx->dev.ot_commands[id];
	ret = cmd->func(ctx, id, out, in, true);
	if (ret == CMD_RESPONSE_UNKNOWN)
		ctx->dev.ot_commands[id].supported--;
	if (!ctx->dev.ot_commands[id].supported)
		hlog_warning(OTHM_MODULE, "Command %d is not supported by the OT device", id);

	return  ret == CMD_RESPONSE_OK ? 0 : -1;
}

#define DATA_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.data.force = true; (S) = (V); }}
static bool opentherm_read_data(opentherm_context_t *ctx)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(ctx, DATA_ID_REL_MOD_LEVEL, NULL, &repl))
		DATA_READ(ctx->data.data.modulation_level, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_CH_PRESSURE, NULL, &repl))
		DATA_READ(ctx->data.data.ch_pressure, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_DHW_FLOW_RATE, NULL, &repl))
		DATA_READ(ctx->data.data.dhw_flow_rate, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_TBOILER, NULL, &repl))
		DATA_READ(ctx->data.data.flow_temperature, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_TDHW, NULL, &repl))
		DATA_READ(ctx->data.data.dhw_temperature, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_TRET, NULL, &repl))
		DATA_READ(ctx->data.data.return_temperature, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_TEXHAUST, NULL, &repl))
		DATA_READ(ctx->data.data.exhaust_temperature, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_FLAME_CURRENT, NULL, &repl))
		DATA_READ(ctx->data.data.flame_current, repl.f);

	return true;
}

static int opentherm_sync_param_f(opentherm_context_t *ctx, int cmd, float *desired, float *actual)
{
	ot_data_t req = {0}, repl = {0};
	int ret = 0;

	if (*desired != *actual) {
		req.f = *desired;
		ret = ot_cmd_write(ctx, cmd, &req, &repl);
		if (!ret)
			*actual = repl.f;
	}
	return ret;
}

static bool opentherm_sync_params(opentherm_context_t *ctx)
{
	opentherm_sync_param_f(ctx, DATA_ID_MAXTSET,
						   &ctx->data.param_desired.ch_max,
						   &ctx->data.param_actual.ch_max);
	opentherm_sync_param_f(ctx, DATA_ID_TDHWSET,
						   &ctx->data.param_desired.dhw_temperature_setpoint,
						   &ctx->data.param_actual.dhw_temperature_setpoint);						   
	opentherm_sync_param_f(ctx, DATA_ID_TDHWSET,
						   &ctx->data.param_desired.dhw_max,
						   &ctx->data.param_actual.dhw_max);
	opentherm_sync_param_f(ctx, DATA_ID_TSET,
						   &ctx->data.param_desired.ch_temperature_setpoint,
						   &ctx->data.param_actual.ch_temperature_setpoint);


	return true;

}

#define ERRORS_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.errors.force = true; (S) = (V); }}
static void opentherm_read_errors(opentherm_context_t *ctx)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(ctx, DATA_ID_ASF_FAULT, NULL, &repl)) {
		ERRORS_READ(ctx->data.errors.fault_code, repl.u8arr[0]);
		ERRORS_READ(ctx->data.errors.fault_svc_needed, ((repl.u8arr[1] & 0x01) ? 1 : 0));
		ERRORS_READ(ctx->data.errors.fault_low_water_pressure, ((repl.u8arr[1] & 0x04) ? 1 : 0));
		ERRORS_READ(ctx->data.errors.fault_flame, ((repl.u8arr[1] & 0x08) ? 1 : 0));
		ERRORS_READ(ctx->data.errors.fault_low_air_pressure, ((repl.u8arr[1] & 0x10) ? 1 : 0));
		ERRORS_READ(ctx->data.errors.fault_high_water_temperature, ((repl.u8arr[1] & 0x20) ? 1 : 0));
	}
	if (!ot_cmd_read(ctx, DATA_ID_UNSUCCESSFUL_BURNER_STARTS, NULL, &repl))
		ERRORS_READ(ctx->data.errors.fault_burner_starts, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_FLAME_SIGNAL_LOW_COUNT, NULL, &repl))
		ERRORS_READ(ctx->data.errors.fault_flame_low, repl.u16);
}

#define STATUS_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.status.force = true; (S) = (V); }}
static int opentherm_exchange_status(opentherm_context_t *ctx)
{
	ot_data_t req = {0}, repl = {0};
	int ret;

	if (ctx->data.status.ch_enabled)
		req.u8arr[1] |= 0x01;
	if (ctx->data.status.dhw_enabled)
		req.u8arr[1] |= 0x02;
	if (ctx->data.status.cooling_enabled)
		req.u8arr[1] |= 0x04;
	if (ctx->data.status.otc_active)
		req.u8arr[1] |= 0x08;
	if (ctx->data.status.ch2_enabled)
		req.u8arr[1] |= 0x10;

	ret = ot_cmd_read(ctx, DATA_ID_STATUS, &req, &repl);
	if (ret) {
		if (IS_CMD_LOG(ctx->log_mask))
			hlog_warning(OTHM_MODULE, "Failed to get valid status");
		return ret;
	}
	if (IS_CMD_LOG(ctx->log_mask))
		hlog_info(OTHM_MODULE, "Got valid status: %0X %0X", repl.u8arr[0], repl.u8arr[1]);
	ERRORS_READ(ctx->data.errors.fault_active, ((repl.u8arr[0] & 0x01) ? 1 : 0));
	STATUS_READ(ctx->data.status.ch_active, ((repl.u8arr[0] & 0x02) ? 1 : 0));
	STATUS_READ(ctx->data.status.dhw_active, ((repl.u8arr[0] & 0x04) ? 1 : 0));
	STATUS_READ(ctx->data.status.flame_active, ((repl.u8arr[0] & 0x08) ? 1 : 0));
	STATUS_READ(ctx->data.status.cooling_active, ((repl.u8arr[0] & 0x10) ? 1 : 0));
	STATUS_READ(ctx->data.status.ch2_active, ((repl.u8arr[0] & 0x20) ? 1 : 0));
	ERRORS_READ(ctx->data.errors.diagnostic_event, ((repl.u8arr[0] & 0x40) ? 1 : 0));
	return ret;
}

#define CFG_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.dev_config.force = true; (S) = (V); }}
static void opentherm_read_cfg_data(opentherm_context_t *ctx)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(ctx, DATA_ID_MAXTSET_BOUNDS, NULL, &repl)) {
		CFG_READ(ctx->data.dev_config.ch_max_cfg, repl.u8arr[1]);
		CFG_READ(ctx->data.dev_config.ch_min_cfg, repl.u8arr[0]);
		ctx->data.param_desired.ch_max = ctx->data.dev_config.ch_max_cfg;
	}
	if (!ot_cmd_read(ctx, DATA_ID_TDHWSET_BOUNDS, NULL, &repl)) {
		CFG_READ(ctx->data.dev_config.dhw_max_cfg, repl.u8arr[1]);
		CFG_READ(ctx->data.dev_config.dhw_min_cfg, repl.u8arr[0]);
		ctx->data.param_desired.dhw_max = ctx->data.dev_config.dhw_max_cfg;
	}
	if (!ot_cmd_read(ctx, DATA_ID_MAXTSET, NULL, &repl))
		CFG_READ(ctx->data.param_actual.ch_max, repl.f);
	if (!ot_cmd_read(ctx, DATA_ID_TDHWSET, NULL, &repl))
		CFG_READ(ctx->data.param_actual.dhw_max, repl.f);
}

#define STATIC_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.dev_static.force = true; (S) = (V); }}
static bool opentherm_read_static_data(opentherm_context_t *ctx)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(ctx, DATA_ID_SECONDARY_CONFIG, NULL, &repl)) {
		STATIC_READ(ctx->data.dev_static.dwh_present, ((repl.u8arr[1] & 0x01) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.control_type, ((repl.u8arr[1] & 0x02) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.cool_present, ((repl.u8arr[1] & 0x04) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.dhw_config, ((repl.u8arr[1] & 0x08) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.pump_control, ((repl.u8arr[1] & 0x10) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.ch2_present, ((repl.u8arr[1] & 0x20) ? 1 : 0));
		STATIC_READ(ctx->data.dev_static.dev_id, repl.u8arr[0]);
	}
	if (!ot_cmd_read(ctx, DATA_ID_SECONDARY_VERSION, NULL, &repl)) {
		STATIC_READ(ctx->data.dev_static.dev_type, repl.u8arr[1]);
		STATIC_READ(ctx->data.dev_static.dev_ver, repl.u8arr[0]);
	}
	if (!ot_cmd_read(ctx, DATA_ID_OPENTHERM_VERSION_SECONDARY, NULL, &repl))
		STATIC_READ(ctx->data.dev_static.ot_ver, ((int)(100*repl.f)));

	// DATA_ID_BRAND, DATA_ID_BRAND_VER, DATA_ID_BRAD_SNUMBER

	return true;
}

void opentherm_reset_statistics(opentherm_context_t *ctx)
{
	ot_data_t req = {0};

	ot_cmd_write(ctx, DATA_ID_UNSUCCESSFUL_BURNER_STARTS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_FLAME_SIGNAL_LOW_COUNT, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_BURNER_STARTS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_CH_PUMP_STARTS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_DHW_PUMP_STARTS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_DHW_BURNER_STARTS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_BURNER_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_CH_PUMP_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_DHW_PUMP_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(ctx, DATA_ID_DHW_BURNER_OPERATION_HOURS, &req, NULL);

	ctx->data.stats.stat_reset_time = time_ms_since_boot();
	ctx->data.stats.force = true;
}

#define STATISTIC_READ(S, V)\
	{ if ((S) != (V)) { ctx->data.stats.force = true; (S) = (V); }}
static void opentherm_read_statistics(opentherm_context_t *ctx)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(ctx, DATA_ID_BURNER_STARTS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_burner_starts, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_CH_PUMP_STARTS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_ch_pump_starts, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_DHW_PUMP_STARTS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_dhw_pump_starts, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_DHW_BURNER_STARTS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_dhw_burn_burner_starts, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_BURNER_OPERATION_HOURS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_burner_hours, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_CH_PUMP_OPERATION_HOURS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_ch_pump_hours, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_DHW_PUMP_OPERATION_HOURS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_dhw_pump_hours, repl.u16);
	if (!ot_cmd_read(ctx, DATA_ID_DHW_BURNER_OPERATION_HOURS, NULL, &repl))
		STATISTIC_READ(ctx->data.stats.stat_dhw_burn_hours, repl.u16);
}

bool opentherm_dev_log(opentherm_context_t *ctx)
{
	static int in_progress;

	if (!opentherm_dev_pio_attached(&ctx->pio))
		return false;

	switch (in_progress) {
	case 0:
		hlog_info(OTHM_MODULE, "Params");
		hlog_info(OTHM_MODULE, "  CH %s", ctx->data.status.ch_enabled ? "enabled" : "disabled");
		hlog_info(OTHM_MODULE, "  DHW %s", ctx->data.status.dhw_enabled ? "enabled" : "disabled");
		hlog_info(OTHM_MODULE, "  CH set: %3.2f/%3.2f*C",
				ctx->data.param_desired.ch_temperature_setpoint, ctx->data.param_actual.ch_temperature_setpoint);
		hlog_info(OTHM_MODULE, "  DHW set: %3.2f/%3.2f*C",
				ctx->data.param_desired.dhw_temperature_setpoint, ctx->data.param_actual.dhw_temperature_setpoint);
		in_progress++;
		break;
	case 1:
		hlog_info(OTHM_MODULE, "Sensors");
		hlog_info(OTHM_MODULE, "  Modulation level: %3.2f%%", ctx->data.data.modulation_level);
		hlog_info(OTHM_MODULE, "  Central heating pressure: %3.2fbar", ctx->data.data.ch_pressure);
		hlog_info(OTHM_MODULE, "  Central heating temperature: %3.2f*C", ctx->data.data.flow_temperature);
		hlog_info(OTHM_MODULE, "  Domestic Hot Water flow: %3.2fl/min", ctx->data.data.dhw_flow_rate);
		hlog_info(OTHM_MODULE, "  Domestic Hot Water temperature: %3.2f*C", ctx->data.data.dhw_temperature);
		hlog_info(OTHM_MODULE, "  Return Water temperature: %3.2f*C", ctx->data.data.return_temperature);
		in_progress++;
		break;
	case 2:
		hlog_info(OTHM_MODULE, "Errors");
		hlog_info(OTHM_MODULE, "  Fault code: %d", ctx->data.errors.fault_code);
		hlog_info(OTHM_MODULE, "  Service needed: %d", ctx->data.errors.fault_svc_needed);
		hlog_info(OTHM_MODULE, "  Low water pressure: %d", ctx->data.errors.fault_low_water_pressure);
		hlog_info(OTHM_MODULE, "  Flame fault: %d", ctx->data.errors.fault_flame);
		hlog_info(OTHM_MODULE, "  Low air pressure: %d", ctx->data.errors.fault_low_air_pressure);
		hlog_info(OTHM_MODULE, "  High water temperature fault: %d", ctx->data.errors.fault_high_water_temperature);
		in_progress++;
		break;
	case 3:
		hlog_info(OTHM_MODULE, "Static data");
		hlog_info(OTHM_MODULE, "  Device ID: %d", ctx->data.dev_static.dev_id);
		hlog_info(OTHM_MODULE, "  Device type: %d", ctx->data.dev_static.dev_type);
		hlog_info(OTHM_MODULE, "  Device ver: %d", ctx->data.dev_static.dev_ver);
		hlog_info(OTHM_MODULE, "  OpenTherm ver: %f", ctx->data.dev_static.ot_ver / 100);
		hlog_info(OTHM_MODULE, "  Domestic Hot Water: %s", ctx->data.dev_static.dwh_present?"present":"not present");
		hlog_info(OTHM_MODULE, "  Control type: modulating %s", ctx->data.dev_static.control_type?"on":"off");
		hlog_info(OTHM_MODULE, "  Cooling: %s", ctx->data.dev_static.cool_present?"present":"not present");
		hlog_info(OTHM_MODULE, "  Domestic Hot Water type: %s", ctx->data.dev_static.dhw_config?"instantaneous":"storage tank");
		hlog_info(OTHM_MODULE, "  Pump control: %s", ctx->data.dev_static.pump_control?"allowed":"not allowed");
		hlog_info(OTHM_MODULE, "  Central heating 2: %s", ctx->data.dev_static.ch2_present?"present":"not present");

		in_progress = 0;
		break;
	default:
		in_progress = 0;
		break;
	}

	return in_progress;
}

void opentherm_dev_run(opentherm_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	static bool cmd_static;

	if (!opentherm_dev_pio_attached(&ctx->pio)) {
		if (ctx->dev.last_dev_lookup &&
			(now - ctx->dev.last_dev_lookup) < CMD_FIND_INTERVAL_MS)
			return;
		opentherm_dev_pio_find(&ctx->pio);
		ctx->dev.last_dev_lookup = time_ms_since_boot();
		if (!opentherm_dev_pio_attached(&ctx->pio))
			return;
	}

	if (!cmd_static) {
		cmd_static = opentherm_read_static_data(ctx);
		goto out;
	}
	if (ctx->dev.last_send &&
	    (now - ctx->dev.last_send) < CMD_SEND_INTERVAL_MS)
		return;
	opentherm_exchange_status(ctx);
	opentherm_sync_params(ctx);

	if ((now - ctx->dev.last_cfg_read) > CMD_CFG_INTERVAL_MS) {
		opentherm_read_cfg_data(ctx);
		ctx->dev.last_cfg_read = time_ms_since_boot();
	} else if ((now - ctx->dev.last_err_read) > CMD_ERR_INTERVAL_MS) {
		opentherm_read_errors(ctx);
		ctx->dev.last_err_read = time_ms_since_boot();
	} else if ((now - ctx->dev.last_stat_read) > CMD_STATS_INTERVAL_MS) {
		opentherm_read_statistics(ctx);
		ctx->dev.last_stat_read = time_ms_since_boot();
	} else {
		opentherm_read_data(ctx);
	}

out:
	ctx->dev.last_send = time_ms_since_boot();
}

#define CMD_ARR_INIT(A, I, T, F) {\
				A[I].cmd_type = (T);\
				A[I].func = (F);\
				A[I].supported = CMD_SUPPORTED_RETRIES;\
			}
static void commands_init(ot_commands_t *cmds)
{
	CMD_ARR_INIT(cmds, DATA_ID_STATUS, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_TSET, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_PRIMARY_CONFIG, CMD_WRITE, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_SECONDARY_CONFIG, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_COMMAND, CMD_WRITE, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_ASF_FAULT, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_RBP_FLAGS, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_COOLING_CONTROL, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TSETCH2, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TROVERRIDE, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TSP_COUNT, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_TSP_DATA, CMD_READ|CMD_WRITE, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_FHB_COUNT, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_FHB_DATA, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_MAX_REL_MODULATION, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_MAX_CAPACITY_MIN_MODULATION, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_TRSET, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_REL_MOD_LEVEL, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_CH_PRESSURE, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_DHW_FLOW_RATE, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_DAY_TIME, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_DATE, CMD_READ|CMD_WRITE, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_YEAR, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_TRSETCH2, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TR, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TBOILER, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TDHW, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TOUTSIDE, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TRET, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TSTORAGE, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TCOLLECTOR, CMD_READ, opentherm_cmd_int16);
	CMD_ARR_INIT(cmds, DATA_ID_TFLOWCH2, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TDHW2, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TEXHAUST, CMD_READ, opentherm_cmd_int16);
	CMD_ARR_INIT(cmds, DATA_ID_BOILER_FAN_SPEED, CMD_READ, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_FLAME_CURRENT, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_TDHWSET_BOUNDS, CMD_READ, opentherm_cmd_int8arr);
	CMD_ARR_INIT(cmds, DATA_ID_MAXTSET_BOUNDS, CMD_READ, opentherm_cmd_int8arr);
	CMD_ARR_INIT(cmds, DATA_ID_HCRATIO_BOUNDS, CMD_READ, opentherm_cmd_int8arr);
	CMD_ARR_INIT(cmds, DATA_ID_TDHWSET, CMD_READ|CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_MAXTSET, CMD_READ|CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_HCRATIO, CMD_READ|CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_BRAND, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_BRAND_VER, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_BRAD_SNUMBER, CMD_READ, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_REMOTE_OVERRIDE_FUNCTION, CMD_READ, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_UNSUCCESSFUL_BURNER_STARTS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_FLAME_SIGNAL_LOW_COUNT, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_OEM_DIAGNOSTIC_CODE, CMD_READ, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_BURNER_STARTS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_CH_PUMP_STARTS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_DHW_PUMP_STARTS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_DHW_BURNER_STARTS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_BURNER_OPERATION_HOURS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_CH_PUMP_OPERATION_HOURS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_DHW_PUMP_OPERATION_HOURS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_DHW_BURNER_OPERATION_HOURS, CMD_READ|CMD_WRITE, opentherm_cmd_uint16);
	CMD_ARR_INIT(cmds, DATA_ID_OPENTHERM_VERSION_PRIMARY, CMD_WRITE, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_OPENTHERM_VERSION_SECONDARY, CMD_READ, opentherm_cmd_float);
	CMD_ARR_INIT(cmds, DATA_ID_PRIMARY_VERSION, CMD_WRITE, opentherm_cmd_uint8arr);
	CMD_ARR_INIT(cmds, DATA_ID_SECONDARY_VERSION, CMD_READ, opentherm_cmd_uint8arr);
}

int opentherm_dev_init(opentherm_context_t *ctx)
{
	commands_init(ctx->dev.ot_commands);
	return 0;
}

void opentherm_dev_scan_all(opentherm_context_t *ctx)
{
	uint8_t u8arr[2];
	int8_t i8arr[2];
	uint16_t u16;
	int16_t i16;
	float f;
	int ret;
	int i;

	for (i = 0; i < DATA_ID_CMD_MAX; i++) {
		ret = opentherm_dev_read(ctx, i, 0, &u16);
		if (ret == CMD_RESPONSE_OK) {
			i16 = (int16_t)((u16 ^ 0x8000) - 0x8000);
			f = (float)i16 / 256.0f;
			i8arr[0] = (int8_t)(((u16 & 0xFF) ^ 0x80) - 0x80);
			i8arr[1] = (int8_t)(((u16 >> 8) ^ 0x80) - 0x80);
			u8arr[0] = (int8_t)((u16 & 0xFF));
			u8arr[1] = (int8_t)((u16 >> 8));
			hlog_info(OTHM_MODULE, "Command %d -> (uint16)0x%0X (int16)%d (float)%f (int8)[%d %d] (uint8)[%d %d]; %s",
					  i, u16, i16, f, i8arr[1], i8arr[0], u8arr[1], u8arr[0],
					  ctx->dev.ot_commands[i].func?"known":"uknown");
		} else if (ret == CMD_RESPONSE_UNKNOWN) {
			hlog_info(OTHM_MODULE, "Command %d is not supported by the OT device.", i);
		} else if (ret == CMD_RESPONSE_INVALID) {
			hlog_info(OTHM_MODULE, "Command %d: Invalid data received", i);
		} else if (ret == CMD_RESPONSE_L1_ERR) {
			hlog_info(OTHM_MODULE, "Command %d: PIO exchange error", i);
		} else {
			hlog_info(OTHM_MODULE, "Command %d: wrong parameters", i);
		}
	}
}
