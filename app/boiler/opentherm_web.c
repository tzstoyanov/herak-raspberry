// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "boiler.h"

#define BOILER_URL   "/boiler"
#define BOILER_DESC  "Gas boiler control"
#define SET_ERR_STR "\tInvalid parameters.\r\n"

#define IS_WEB_LOG (boiler_dbg_check(LOG_WEB_DEBUG))

static uint32_t boiler_debug_mask; // Set boot log level here

bool boiler_dbg_check(uint32_t mask)
{
	return boiler_debug_mask & mask;
}

static int cmd_get_param_str(int client_idx, char *params, void *user_data, char **p1, char **p2)
{
	char *rest, *tok;

	UNUSED(user_data);

	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;

	rest = params + 1;
	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		goto out_err;
	if (p1)
		*p1 = tok;

	tok = strtok_r(rest, ":", &rest);
	if (p2)
		*p2 = tok;

	weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	return 0;

out_err:
	weberv_client_send(client_idx, SET_ERR_STR, strlen(SET_ERR_STR), HTTP_RESP_BAD);
	return -1;
}

static int cmd_get_param(int client_idx, char *params, void *user_data, int *cmd, int *data)
{
	char *p1 = NULL, *p2 = NULL;
	int ret;

	ret = cmd_get_param_str(client_idx, params, user_data, &p1, &p2);
	if (ret)
		return ret;
	if (cmd && p1)
		*cmd = (int)strtol(p1, NULL, 0);
	if (data && p2)
		*data = (int)strtol(p2, NULL, 0);

	return 0;
}


#define WEB_REPLY_MAX   64
static void cmd_send(int client_idx, char *params, void *user_data, bool read)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char rstr[WEB_REPLY_MAX];
	uint16_t reply = 0;
	int id = 0, data = 0;
	int ret;

	UNUSED(boiler);

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB OT %s command: [%s]",
				  read?"read":"write", params);

	if (cmd_get_param(client_idx, params, user_data, &id, &data))
		goto out;

	if (id < 0 || id >= DATA_ID_CMD_MAX) {
		snprintf(rstr, WEB_REPLY_MAX, "Invalid command id %d.\n\r", id);
		weberv_client_send(client_idx, rstr, strlen(rstr), HTTP_RESP_OK);
		goto out;
	}

	if (read)
		ret = opentherm_cmd_read(id, data, &reply);
	else
		ret = opentherm_cmd_write(id, data, &reply);

	if (ret) {
		if (IS_WEB_LOG)
			hlog_warning(OTHLOG, "WEB Requested %d, failed to get valid reply.\n\r", id);
		snprintf(rstr, WEB_REPLY_MAX, "Requested %d, failed to get valid reply.\n\r", id);
		weberv_client_send(client_idx, rstr, strlen(rstr), HTTP_RESP_OK);
		goto out;
	}
	if (IS_WEB_LOG)
		hlog_warning(OTHLOG, "WEB Requested %d, got valid reply 0x%X.\n\r", id, reply);
	snprintf(rstr, WEB_REPLY_MAX, "Requested %d, got valid reply 0x%X.\n\r", id, reply);
	weberv_client_send(client_idx, rstr, strlen(rstr), HTTP_RESP_OK);

out:
	weberv_client_close(client_idx);
}

static void cmd_read(int client_idx, char *params, void *user_data)
{
	cmd_send(client_idx, params, user_data, true);
}

static void cmd_write(int client_idx, char *params, void *user_data)
{
	cmd_send(client_idx, params, user_data, false);
}

static void cmd_debug(int client_idx, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	char rstr[WEB_REPLY_MAX];
	int dbg = 0;

	UNUSED(boiler);

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB set debug command: [%s]", params);

	if (cmd_get_param(client_idx, params, user_data, &dbg, NULL))
		goto out;

	boiler_debug_mask = dbg;

	snprintf(rstr, WEB_REPLY_MAX, "Set debug to 0x%lX.\n\r", boiler_debug_mask);
	weberv_client_send(client_idx, rstr, strlen(rstr), HTTP_RESP_OK);

out:
	weberv_client_close(client_idx);
}

static void cmd_set_status(int client_idx, char *params, void *user_data, bool ch_stat)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char *val = NULL;
	int set = 0;
	int err = 1;

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB set %s status param command: [%s]",
				  ch_stat?"CH":"DHW", params);

	if (cmd_get_param_str(client_idx, params, user_data, &val, NULL))
		goto out;
	if (!val)
		goto out;

	set = (int)strtol(val, NULL, 10);
	if (ch_stat)
		boiler->data.ch_enabled = ((set != 0) ? 1 : 0);
	else
		boiler->data.dhw_enabled = ((set != 0) ? 1 : 0);

	err = 0;

out:
	if (err) {
		if (IS_WEB_LOG)
			hlog_warning(OTHLOG, "WEB Failed to set the param: invalid data.");
		weberv_client_send(client_idx, SET_ERR_STR, strlen(SET_ERR_STR), HTTP_RESP_OK);
	} else {
		weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	}
	weberv_client_close(client_idx);
}

static void cmd_set_param_float(int client_idx, char *params, void *user_data, opentherm_cmd_id_t id)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char *val = NULL;
	float f = 0;
	int err = 1;

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB set float param %d command: [%s]", id, params);

	if (cmd_get_param_str(client_idx, params, user_data, &val, NULL))
		goto out;
	if (!val)
		goto out;
	f = (float)strtof(val, NULL);
	if (f < 0 || f > 100)
		goto out;

	switch (id) {
	case DATA_ID_TSET:
		boiler->data.param_desired.ch_temperature_setpoint = f;
		break;
	case DATA_ID_TDHWSET:
		boiler->data.param_desired.dhw_temperature_setpoint = f;
		break;
	default:
		goto out;
	}
	err = 0;

out:
	if (err) {
		if (IS_WEB_LOG)
			hlog_warning(OTHLOG, "WEB Failed to set the param: invalid data.");
		weberv_client_send(client_idx, SET_ERR_STR, strlen(SET_ERR_STR), HTTP_RESP_OK);
	} else {
		weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	}
	weberv_client_close(client_idx);
}


static void cmd_set_dwh(int client_idx, char *params, void *user_data)
{
	cmd_set_status(client_idx, params, user_data, false);
}

static void cmd_set_dwh_temp(int client_idx, char *params, void *user_data)
{
	cmd_set_param_float(client_idx, params, user_data, DATA_ID_TDHWSET);
}

static void cmd_set_ch(int client_idx, char *params, void *user_data)
{
	cmd_set_status(client_idx, params, user_data, true);
}

static void cmd_set_ch_temp(int client_idx, char *params, void *user_data)
{
	cmd_set_param_float(client_idx, params, user_data, DATA_ID_TSET);
}

#define SCANN_STR "\tSupported commands:\r\n"
static void cmd_scan_all(int client_idx, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);
	UNUSED(boiler);

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB scan all command.");

	weberv_client_send(client_idx, SCANN_STR, strlen(SCANN_STR), HTTP_RESP_OK);

	debug_log_forward(client_idx);
		opentherm_cmd_scan_all();
	debug_log_forward(-1);

	weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

#define STATUS_STR "\tBoiler status:\r\n"
static void boiler_status(int client_idx, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB boiler status command.");

	weberv_client_send(client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);

	debug_log_forward(client_idx);
		opentherm_status_log(boiler);
	debug_log_forward(-1);

	weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

static void boiler_statistics_reset(int client_idx, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);

	if (IS_WEB_LOG)
		hlog_info(OTHLOG, "WEB boiler statistics reset command.");

	opentherm_reset_statistics(&boiler->data);

	weberv_client_send(client_idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

static web_requests_t boiler_requests[] = {
	{"read", ":<opentherm_cmd_id>:<value>", cmd_read},
	{"write", ":<opentherm_cmd_id>:<value>", cmd_write},
	{"debug", ":<debug_mask> - 0x1 PIO, 0x2 CMD, 0x4 MQTT, 0x8 WEB", cmd_debug},
	{"dhw", ":<0/1>", cmd_set_dwh},
	{"dhw_temp", ":<0..100>*C", cmd_set_dwh_temp},
	{"ch", ":<0/1>", cmd_set_ch},
	{"ch_temp", ":<0..100>*C", cmd_set_ch_temp},
	{"status", NULL, boiler_status},
	{"scan", NULL, cmd_scan_all},
	{"stat_reset", NULL, boiler_statistics_reset},
};

int opentherm_web_init(opentherm_context_t *boiler)
{
	if (webserv_add_commands(BOILER_URL, boiler_requests, ARRAY_SIZE(boiler_requests), BOILER_DESC, boiler) < 0) {
		if (IS_WEB_LOG)
			hlog_warning(OTHLOG, "WEB Failed to register the commands.");
		return -1;
	}
	return 0;
}
