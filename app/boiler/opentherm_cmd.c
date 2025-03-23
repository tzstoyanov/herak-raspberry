// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "boiler.h"

#define BOILERLOG    "boiler"
#define CMD_SEND_INTERVAL_MS	1000
#define CMD_ERR_INTERVAL_MS		10000
#define CMD_STATS_INTERVAL_MS	10000
#define CMD_CFG_INTERVAL_MS		60000
#define CMD_FIND_INTERVAL_MS	60000 // 1 min
#define CMD_SUPPORTED_RETRIES	10

#define CMD_READ	0x01
#define CMD_WRITE	0x02

#define IS_CMD_LOG (boiler_dbg_check(LOG_CMD_DEBUG))

typedef union {
	uint16_t u16;
	int16_t i16;
	float f;
	int8_t i8arr[2];
	uint8_t u8arr[2];
} ot_data_t;

typedef opentherm_cmd_response_t (*data_handler_t)(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write);
typedef struct {
	opentherm_cmd_id_t id;
	int cmd_type;
	int supported;
	data_handler_t func;
} ot_commands_t;

static struct {
	int log_mask;
	uint64_t last_send;
	uint64_t last_dev_lookup;
	uint64_t last_err_read;
	uint64_t last_stat_read;
	uint64_t last_cfg_read;
	ot_commands_t ot_commands[DATA_ID_CMD_MAX];
} opentherm_cmd_context;

opentherm_cmd_response_t
opentherm_cmd_read(opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value)
{
	opentherm_msg_t req = {0}, repl = {0};

	req.id = cmd;
	req.msg_type = MSG_TYPE_READ_DATA;
	req.value = send;
	if (opentherm_pio_exchange(&req, &repl))
		return CMD_RESPONSE_L1_ERR;

	if (repl.msg_type == MSG_TYPE_READ_ACK && repl.id == cmd) {
		if (value)
			*value = repl.value;
		return CMD_RESPONSE_OK;
	}

	if (IS_CMD_LOG)
		hlog_warning(OTHLOG, "Not expected read msg received for commands %d: %d",
					 repl.id, repl.msg_type);
	if (repl.msg_type == MSG_TYPE_DATA_INVALID)
		return CMD_RESPONSE_INVALID;
	if (repl.msg_type == MSG_TYPE_UNKNOWN_DATA_ID)
		return CMD_RESPONSE_UNKNOWN;

	return CMD_RESPONSE_WRONG_PARAM;
}

opentherm_cmd_response_t
opentherm_cmd_write(opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value)
{
	opentherm_msg_t req = {0}, repl = {0};

	req.id = cmd;
	req.msg_type = MSG_TYPE_WRITE_DATA;
	req.value = send;
	if (opentherm_pio_exchange(&req, &repl))
		return CMD_RESPONSE_L1_ERR;

	if (repl.msg_type == MSG_TYPE_WRITE_ACK && repl.id == cmd) {
		if (value)
			*value = repl.value;
		return CMD_RESPONSE_OK;
	}

	if (IS_CMD_LOG)
		hlog_warning(OTHLOG, "Not expected write msg received: cmd %d, type %d",
					 repl.id, repl.msg_type);

	if (repl.msg_type == MSG_TYPE_DATA_INVALID)
		return CMD_RESPONSE_INVALID;
	if (repl.msg_type == MSG_TYPE_UNKNOWN_DATA_ID)
		return CMD_RESPONSE_UNKNOWN;

	return CMD_RESPONSE_WRONG_PARAM;
}

static opentherm_cmd_response_t
opentherm_cmd_uint16(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_cmd_write(cmd, out?out->u16:0, &v);
	else
		ret = opentherm_cmd_read(cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in)
		in->u16 = v;
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_int16(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_cmd_write(cmd, out?out->u16:0, &v);
	else
		ret = opentherm_cmd_read(cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) // Signed int16 conversion
		in->i16 = (int16_t)((v ^ 0x8000) - 0x8000);
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_float(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
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
		ret = opentherm_cmd_write(cmd, s, &v);
	else
		ret	= opentherm_cmd_read(cmd, s, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Fixed-point 8.8 conversion
		i16 = (int16_t)((v ^ 0x8000) - 0x8000);
		in->f = (float)i16 / 256.0f;
	}
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_int8arr(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_cmd_write(cmd, out?out->u16:0, &v);
	else
		ret = opentherm_cmd_read(cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Signed 8-bit conversion
		in->i8arr[0] = (int8_t)(((v & 0xFF) ^ 0x80) - 0x80);
		in->i8arr[1] = (int8_t)(((v >> 8) ^ 0x80) - 0x80);
	}
	return ret;
}

static opentherm_cmd_response_t
opentherm_cmd_uint8arr(opentherm_cmd_id_t cmd, ot_data_t *out, ot_data_t *in, bool write)
{
	opentherm_cmd_response_t ret;
	uint16_t v = 0;

	if (write)
		ret = opentherm_cmd_write(cmd, out?out->u16:0, &v);
	else
		ret = opentherm_cmd_read(cmd, out?out->u16:0, &v);
	if (ret != CMD_RESPONSE_OK)
		return ret;

	if (in) { // Unsigned 8-bit conversion
		in->u8arr[0] = (int8_t)((v & 0xFF));
		in->u8arr[1] = (int8_t)((v >> 8));
	}
	return ret;
}

static int ot_cmd_read(int id, ot_data_t *out, ot_data_t *in)
{
	ot_commands_t *cmd;
	int ret;

	if (id >= DATA_ID_CMD_MAX)
		return -1;
	if (!(opentherm_cmd_context.ot_commands[id].cmd_type & CMD_READ))
		return -1;
	if (!opentherm_cmd_context.ot_commands[id].supported)
		return -1;
	cmd = &opentherm_cmd_context.ot_commands[id];
	ret = cmd->func(id, out, in, false);
	if (ret == CMD_RESPONSE_UNKNOWN)
		opentherm_cmd_context.ot_commands[id].supported--;
	if (!opentherm_cmd_context.ot_commands[id].supported)
		hlog_warning(OTHLOG, "Command %d is not supported by the OT device", id);

	return  ret == CMD_RESPONSE_OK ? 0 : -1;
}

static int ot_cmd_write(int id, ot_data_t *out, ot_data_t *in)
{
	ot_commands_t *cmd;
	int ret;

	if (id >= DATA_ID_CMD_MAX)
		return -1;
	if (!(opentherm_cmd_context.ot_commands[id].cmd_type & CMD_WRITE))
		return -1;
	if (!opentherm_cmd_context.ot_commands[id].supported)
		return -1;
	cmd = &opentherm_cmd_context.ot_commands[id];
	ret = cmd->func(id, out, in, true);
	if (ret == CMD_RESPONSE_UNKNOWN)
		opentherm_cmd_context.ot_commands[id].supported--;
	if (!opentherm_cmd_context.ot_commands[id].supported)
		hlog_warning(OTHLOG, "Command %d is not supported by the OT device", id);

	return  ret == CMD_RESPONSE_OK ? 0 : -1;
}

static int opentherm_exchange_status(opentherm_data_t *boiler)
{
	ot_data_t req = {0}, repl = {0};
	int ret;

	if (boiler->ch_enabled)
		req.u8arr[1] |= 0x01;
	if (boiler->dhw_enabled)
		req.u8arr[1] |= 0x02;
	if (boiler->cooling_enabled)
		req.u8arr[1] |= 0x04;
	if (boiler->otc_active)
		req.u8arr[1] |= 0x08;
	if (boiler->ch2_enabled)
		req.u8arr[1] |= 0x10;

	ret = ot_cmd_read(DATA_ID_STATUS, &req, &repl);
	if (ret) {
		if (IS_CMD_LOG)
			hlog_warning(OTHLOG, "Failed to get valid status");
		return ret;
	}
	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "Got valid status: %0X %0X", repl.u8arr[0], repl.u8arr[1]);
	boiler->fault_active = (repl.u8arr[0] & 0x01) ? 1 : 0;
	boiler->ch_active = (repl.u8arr[0] & 0x02) ? 1 : 0;
	boiler->dhw_active = (repl.u8arr[0] & 0x04) ? 1 : 0;
	boiler->flame_active = (repl.u8arr[0] & 0x08) ? 1 : 0;
	boiler->cooling_active = (repl.u8arr[0] & 0x10) ? 1 : 0;
	boiler->ch2_active = (repl.u8arr[0] & 0x20) ? 1 : 0;
	boiler->diagnostic_event = (repl.u8arr[0] & 0x40) ? 1 : 0;
	return ret;
}

static bool opentherm_read_date(opentherm_data_t *boiler)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(DATA_ID_REL_MOD_LEVEL, NULL, &repl))
		boiler->modulation_level = repl.f;
	if (!ot_cmd_read(DATA_ID_CH_PRESSURE, NULL, &repl))
		boiler->ch_pressure = repl.f;
	if (!ot_cmd_read(DATA_ID_DHW_FLOW_RATE, NULL, &repl))
		boiler->dhw_flow_rate = repl.f;
	if (!ot_cmd_read(DATA_ID_TBOILER, NULL, &repl))
		boiler->flow_temperature = repl.f;
	if (!ot_cmd_read(DATA_ID_TDHW, NULL, &repl))
		boiler->dhw_temperature = repl.f;
	if (!ot_cmd_read(DATA_ID_TRET, NULL, &repl))
		boiler->return_temperature = repl.f;
	if (!ot_cmd_read(DATA_ID_TEXHAUST, NULL, &repl))
		boiler->exhaust_temperature = repl.i16;
	if (!ot_cmd_read(DATA_ID_FLAME_CURRENT, NULL, &repl))
		boiler->flame_current = repl.f;

	return true;
}

static int opentherm_sync_param_f(int cmd, float *desired, float *actual)
{
	ot_data_t req = {0}, repl = {0};
	int ret = 0;

	if (*desired != *actual) {
		req.f = *desired;
		ret = ot_cmd_write(cmd, &req, &repl);
		if (!ret)
			*actual = repl.f;
	}
	return ret;
}

static bool  opentherm_sync_params(opentherm_data_t *boiler)
{
	opentherm_sync_param_f(DATA_ID_MAXTSET,
						   &boiler->param_desired.ch_max,
						   &boiler->param_actual.ch_max);
	opentherm_sync_param_f(DATA_ID_TDHWSET,
						   &boiler->param_desired.dhw_max,
						   &boiler->param_actual.dhw_max);
	opentherm_sync_param_f(DATA_ID_TSET,
						   &boiler->param_desired.ch_temperature_setpoint,
						   &boiler->param_actual.ch_temperature_setpoint);
	opentherm_sync_param_f(DATA_ID_TDHWSET,
						   &boiler->param_desired.dhw_temperature_setpoint,
						   &boiler->param_actual.dhw_temperature_setpoint);

	return true;

}

static void opentherm_read_errors(opentherm_data_t *boiler)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(DATA_ID_ASF_FAULT, NULL, &repl)) {
		boiler->fault_code					= repl.u8arr[0];
		boiler->fault_svc_needed			= (repl.u8arr[1] & 0x01) ? 1 : 0;
		boiler->fault_low_water_pressure	= (repl.u8arr[1] & 0x04) ? 1 : 0;
		boiler->fault_flame					= (repl.u8arr[1] & 0x08) ? 1 : 0;
		boiler->fault_low_air_pressure		= (repl.u8arr[1] & 0x10) ? 1 : 0;
		boiler->fault_high_water_temperature = (repl.u8arr[1] & 0x20) ? 1 : 0;
	}
	if (!ot_cmd_read(DATA_ID_UNSUCCESSFUL_BURNER_STARTS, NULL, &repl))
		boiler->fault_burner_starts = repl.u16;
	if (!ot_cmd_read(DATA_ID_FLAME_SIGNAL_LOW_COUNT, NULL, &repl))
		boiler->fault_flame_low = repl.u16;
}

static void opentherm_read_cfg_data(opentherm_data_t *boiler)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(DATA_ID_MAXTSET_BOUNDS, NULL, &repl)) {
		boiler->ch_max_cfg = repl.u8arr[1];
		boiler->ch_min_cfg = repl.u8arr[0];
		boiler->param_desired.ch_max = boiler->ch_max_cfg;
	}
	if (!ot_cmd_read(DATA_ID_TDHWSET_BOUNDS, NULL, &repl)) {
		boiler->dhw_max_cfg = repl.u8arr[1];
		boiler->dhw_min_cfg = repl.u8arr[0];
		boiler->param_desired.dhw_max = boiler->dhw_max_cfg;
	}
	if (!ot_cmd_read(DATA_ID_MAXTSET, NULL, &repl)) {
		boiler->param_actual.ch_max = repl.f;
	}
	if (!ot_cmd_read(DATA_ID_TDHWSET, NULL, &repl)) {
		boiler->param_actual.dhw_max = repl.f;
	}
}

static bool opentherm_read_static_data(opentherm_data_t *boiler)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(DATA_ID_SECONDARY_CONFIG, NULL, &repl)) {
		boiler->dwh_present = (repl.u8arr[1] & 0x01) ? 1 : 0;
		boiler->control_type = (repl.u8arr[1] & 0x02) ? 1 : 0;
		boiler->cool_present = (repl.u8arr[1] & 0x04) ? 1 : 0;
		boiler->dhw_config = (repl.u8arr[1] & 0x08) ? 1 : 0;
		boiler->pump_control = (repl.u8arr[1] & 0x10) ? 1 : 0;
		boiler->ch2_present = (repl.u8arr[1] & 0x20) ? 1 : 0;
		boiler->dev_id = repl.u8arr[0];
	}
	if (!ot_cmd_read(DATA_ID_SECONDARY_VERSION, NULL, &repl)) {
		boiler->dev_type = repl.u8arr[1];
		boiler->dev_ver = repl.u8arr[0];
	}
	if (!ot_cmd_read(DATA_ID_OPENTHERM_VERSION_SECONDARY, NULL, &repl))
		boiler->ot_ver = (int)(100*repl.f);

	// DATA_ID_BRAND, DATA_ID_BRAND_VER, DATA_ID_BRAD_SNUMBER

	return true;
}

void opentherm_reset_statistics(opentherm_data_t *boiler)
{
	ot_data_t req = {0};

	ot_cmd_write(DATA_ID_UNSUCCESSFUL_BURNER_STARTS, &req, NULL);
	ot_cmd_write(DATA_ID_FLAME_SIGNAL_LOW_COUNT, &req, NULL);
	ot_cmd_write(DATA_ID_BURNER_STARTS, &req, NULL);
	ot_cmd_write(DATA_ID_CH_PUMP_STARTS, &req, NULL);
	ot_cmd_write(DATA_ID_DHW_PUMP_STARTS, &req, NULL);
	ot_cmd_write(DATA_ID_DHW_BURNER_STARTS, &req, NULL);
	ot_cmd_write(DATA_ID_BURNER_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(DATA_ID_CH_PUMP_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(DATA_ID_DHW_PUMP_OPERATION_HOURS, &req, NULL);
	ot_cmd_write(DATA_ID_DHW_BURNER_OPERATION_HOURS, &req, NULL);

	boiler->stat_reset_time = time_ms_since_boot();
}

static void opentherm_read_statistics(opentherm_data_t *boiler)
{
	ot_data_t repl = {0};

	if (!ot_cmd_read(DATA_ID_BURNER_STARTS, NULL, &repl))
		boiler->stat_burner_starts = repl.u16;
	if (!ot_cmd_read(DATA_ID_CH_PUMP_STARTS, NULL, &repl))
		boiler->stat_ch_pump_starts = repl.u16;
	if (!ot_cmd_read(DATA_ID_DHW_PUMP_STARTS, NULL, &repl))
		boiler->stat_dhw_pump_starts = repl.u16;
	if (!ot_cmd_read(DATA_ID_DHW_BURNER_STARTS, NULL, &repl))
		boiler->stat_dhw_burn_burner_starts = repl.u16;
	if (!ot_cmd_read(DATA_ID_BURNER_OPERATION_HOURS, NULL, &repl))
		boiler->stat_burner_hours = repl.u16;
	if (!ot_cmd_read(DATA_ID_CH_PUMP_OPERATION_HOURS, NULL, &repl))
		boiler->stat_ch_pump_hours = repl.u16;
	if (!ot_cmd_read(DATA_ID_DHW_PUMP_OPERATION_HOURS, NULL, &repl))
		boiler->stat_dhw_pump_hours = repl.u16;
	if (!ot_cmd_read(DATA_ID_DHW_BURNER_OPERATION_HOURS, NULL, &repl))
		boiler->stat_dhw_burn_hours = repl.u16;
}

void opentherm_cmd_log(opentherm_context_t *boiler)
{
	if (!opentherm_pio_attached())
		return;
	hlog_info(OTHLOG, "Static data");
	hlog_info(OTHLOG, "  Device ID: %d", boiler->data.dev_id);
	hlog_info(OTHLOG, "  Device type: %d", boiler->data.dev_type);
	hlog_info(OTHLOG, "  Device ver: %d", boiler->data.dev_ver);
	hlog_info(OTHLOG, "  OpenTherm ver: %f", boiler->data.ot_ver / 100);
	hlog_info(OTHLOG, "  Domestic Hot Water: %s", boiler->data.dwh_present?"present":"not present");
	hlog_info(OTHLOG, "  Control type: modulating %s", boiler->data.control_type?"on":"off");
	hlog_info(OTHLOG, "  Cooling: %s", boiler->data.cool_present?"present":"not present");
	hlog_info(OTHLOG, "  Domestic Hot Water type: %s", boiler->data.dhw_config?"instantaneous":"storage tank");
	hlog_info(OTHLOG, "  Pump control: %s", boiler->data.pump_control?"allowed":"not allowed");
	hlog_info(OTHLOG, "  Central heating 2: %s", boiler->data.ch2_present?"present":"not present");

	hlog_info(OTHLOG, "Errors");
	hlog_info(OTHLOG, "  Fault code: %d", boiler->data.fault_code);
	hlog_info(OTHLOG, "  Service needed: %d", boiler->data.fault_svc_needed);
	hlog_info(OTHLOG, "  Low water pressure: %d", boiler->data.fault_low_water_pressure);
	hlog_info(OTHLOG, "  Flame fault: %d", boiler->data.fault_flame);
	hlog_info(OTHLOG, "  Low air pressure: %d", boiler->data.fault_low_air_pressure);
	hlog_info(OTHLOG, "  High water temperature fault: %d", boiler->data.fault_high_water_temperature);

	hlog_info(OTHLOG, "Sensors");
	hlog_info(OTHLOG, "  Modulation level: %3.2f%%", boiler->data.modulation_level);
	hlog_info(OTHLOG, "  Central heating pressure: %3.2fbar", boiler->data.ch_pressure);
	hlog_info(OTHLOG, "  Central heating temperature: %3.2f*C", boiler->data.flow_temperature);
	hlog_info(OTHLOG, "  Domestic Hot Water flow: %3.2fl/min", boiler->data.dhw_flow_rate);
	hlog_info(OTHLOG, "  Domestic Hot Water temperature: %3.2f*C", boiler->data.dhw_temperature);
	hlog_info(OTHLOG, "  Return Water temperature: %3.2f*C", boiler->data.return_temperature);

	hlog_info(OTHLOG, "Params");
	hlog_info(OTHLOG, "  CH %s", boiler->data.ch_enabled ? "enabled" : "disabled");
	hlog_info(OTHLOG, "  DHW %s", boiler->data.dhw_enabled ? "enabled" : "disabled");
	hlog_info(OTHLOG, "  CH set: %3.2f/%3.2f*C",
			  boiler->data.param_desired.ch_temperature_setpoint, boiler->data.param_actual.ch_temperature_setpoint);
	hlog_info(OTHLOG, "  DHW set: %3.2f/%3.2f*C",
			  boiler->data.param_desired.dhw_temperature_setpoint, boiler->data.param_actual.dhw_temperature_setpoint);
}

void opentherm_cmd_run(opentherm_context_t *boiler)
{
	uint64_t now = time_ms_since_boot();
	static bool cmd_static;

	if (!opentherm_pio_attached()) {
		if (opentherm_cmd_context.last_dev_lookup &&
			(now - opentherm_cmd_context.last_dev_lookup) < CMD_FIND_INTERVAL_MS)
			return;
		opentherm_pio_find();
		opentherm_cmd_context.last_dev_lookup = time_ms_since_boot();
		if (!opentherm_pio_attached())
			return;
	}

	if (!cmd_static) {
		cmd_static = opentherm_read_static_data(&boiler->data);
		goto out;
	}
	if (opentherm_cmd_context.last_send &&
	    (now - opentherm_cmd_context.last_send) < CMD_SEND_INTERVAL_MS)
		return;
	opentherm_exchange_status(&boiler->data);
	opentherm_sync_params(&boiler->data);

	if ((now - opentherm_cmd_context.last_cfg_read) > CMD_CFG_INTERVAL_MS) {
		opentherm_read_cfg_data(&boiler->data);
		opentherm_cmd_context.last_cfg_read = time_ms_since_boot();
	} else if ((now - opentherm_cmd_context.last_err_read) > CMD_ERR_INTERVAL_MS) {
		opentherm_read_errors(&boiler->data);
		opentherm_cmd_context.last_err_read = time_ms_since_boot();
	} else if ((now - opentherm_cmd_context.last_stat_read) > CMD_STATS_INTERVAL_MS) {
		opentherm_read_statistics(&boiler->data);
		opentherm_cmd_context.last_stat_read = time_ms_since_boot();
	} else {
		opentherm_read_date(&boiler->data);
	}

out:
	opentherm_cmd_context.last_send = time_ms_since_boot();
	mqtt_boiler_data(boiler);
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

int opentherm_cmd_init(opentherm_context_t *boiler)
{
	UNUSED(boiler);

	memset(&opentherm_cmd_context, 0, sizeof(opentherm_cmd_context));
	commands_init(opentherm_cmd_context.ot_commands);
	return 0;
}

void opentherm_cmd_scan_all(void)
{
	uint8_t u8arr[2];
	int8_t i8arr[2];
	uint16_t u16;
	int16_t i16;
	float f;
	int ret;
	int i;

	for (i = 0; i < DATA_ID_CMD_MAX; i++) {
		ret = opentherm_cmd_read(i, 0, &u16);
		if (ret == CMD_RESPONSE_OK) {
			i16 = (int16_t)((u16 ^ 0x8000) - 0x8000);
			f = (float)i16 / 256.0f;
			i8arr[0] = (int8_t)(((u16 & 0xFF) ^ 0x80) - 0x80);
			i8arr[1] = (int8_t)(((u16 >> 8) ^ 0x80) - 0x80);
			u8arr[0] = (int8_t)((u16 & 0xFF));
			u8arr[1] = (int8_t)((u16 >> 8));
			hlog_info(OTHLOG, "Command %d -> (uint16)0x%0X (int16)%d (float)%f (int8)[%d %d] (uint8)[%d %d]; %s",
					  i, u16, i16, f, i8arr[1], i8arr[0], u8arr[1], u8arr[0],
					  opentherm_cmd_context.ot_commands[i].func?"known":"uknown");
		} else if (ret == CMD_RESPONSE_UNKNOWN) {
			hlog_info(OTHLOG, "Command %d is not supported by the OT device.", i);
		} else if (ret == CMD_RESPONSE_INVALID) {
			hlog_info(OTHLOG, "Command %d: Invalid data received", i);
		} else if (ret == CMD_RESPONSE_L1_ERR) {
			hlog_info(OTHLOG, "Command %d: PIO exchange error", i);
		} else {
			hlog_info(OTHLOG, "Command %d: wrong parameters", i);
		}
	}
}
