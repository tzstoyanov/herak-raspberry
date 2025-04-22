// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "boiler.h"

#define BOILER_URL   "/boiler"
#define BOILER_DESC  "Gas boiler control"
#define SET_ERR_STR "\tInvalid parameters.\r\n"
#define BOILERLOG    "boiler"

#define STATE_CMD_HELP	":<0/1>"
#define TEMP_CMD_HELP	":<0..100>*C"

#define IS_CMD_LOG (boiler_dbg_check(LOG_UCMD_DEBUG))

static uint32_t boiler_debug_mask; // Set boot log level here

bool boiler_dbg_check(uint32_t mask)
{
	return boiler_debug_mask & mask;
}

static int cmd_get_param_str(char *params, char **p1, char **p2)
{
	char *rest, *tok;

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

	return 0;

out_err:
	return -1;
}

static int cmd_get_param(char *params, int *cmd, int *data)
{
	char *p1 = NULL, *p2 = NULL;
	int ret;

	ret = cmd_get_param_str(params, &p1, &p2);
	if (ret)
		return ret;
	if (cmd && p1)
		*cmd = (int)strtol(p1, NULL, 0);
	if (data && p2)
		*data = (int)strtol(p2, NULL, 0);

	return 0;
}


#define WEB_REPLY_MAX   64
static int cmd_send(cmd_run_context_t *ctx, char *params, void *user_data, bool read)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char rstr[WEB_REPLY_MAX] = "Fail";
	uint16_t reply = 0;
	int id = 0, data = 0;
	int ret;

	UNUSED(boiler);

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB OT %s command: [%s]",
				  read?"read":"write", params);

	if (cmd_get_param(params, &id, &data))
		goto out_err;

	if (id < 0 || id >= DATA_ID_CMD_MAX) {
		snprintf(rstr, WEB_REPLY_MAX, "Invalid command id %d.\n\r", id);
		goto out_err;
	}

	if (read)
		ret = opentherm_cmd_read(id, data, &reply);
	else
		ret = opentherm_cmd_write(id, data, &reply);

	if (ret) {
		if (IS_CMD_LOG)
			hlog_warning(OTHLOG, "WEB Requested %d, failed to get valid reply.\n\r", id);
		snprintf(rstr, WEB_REPLY_MAX, "Requested %d, failed to get valid reply.\n\r", id);
	} else {
		if (IS_CMD_LOG)
			hlog_warning(OTHLOG, "WEB Requested %d, got valid reply 0x%X.\n\r", id, reply);
		snprintf(rstr, WEB_REPLY_MAX, "Requested %d, got valid reply 0x%X.\n\r", id, reply);
	}

	WEB_CLIENT_REPLY(ctx, rstr);
	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, rstr);
	return -1;
}

static int cmd_read(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_send(ctx, params, user_data, true);
}

static int cmd_write(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_send(ctx, params, user_data, false);
}

static int cmd_debug(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char rstr[WEB_REPLY_MAX] = "\tInvalid parameters.\r\n";
	int dbg = 0;

	UNUSED(boiler);
	UNUSED(cmd);

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB set debug command: [%s]", params);

	if (cmd_get_param(params, &dbg, NULL))
		goto out_err;

	boiler_debug_mask = dbg;

	snprintf(rstr, WEB_REPLY_MAX, "Set debug to 0x%lX.\n\r", boiler_debug_mask);
	WEB_CLIENT_REPLY(ctx, rstr);
	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, rstr);
	return -1;
}

static int cmd_set_status(cmd_run_context_t *ctx, char *params, void *user_data, bool ch_stat)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char *val = NULL;
	int set = 0;

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB set %s status param command: [%s]",
				  ch_stat?"CH":"DHW", params);

	if (cmd_get_param_str(params, &val, NULL))
		goto out_err;
	if (!val)
		goto out_err;

	set = (int)strtol(val, NULL, 10);
	if (ch_stat)
		boiler->data.ch_enabled = ((set != 0) ? 1 : 0);
	else
		boiler->data.dhw_enabled = ((set != 0) ? 1 : 0);

	WEB_CLIENT_REPLY(ctx, WEB_CMD_NR);
	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, SET_ERR_STR);
	if (IS_CMD_LOG)
		hlog_warning(OTHLOG, "WEB Failed to set the param: invalid data.");

	return -1;
}

static int cmd_set_param_float(cmd_run_context_t *ctx, char *params, void *user_data, opentherm_cmd_id_t id)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;
	char *val = NULL;
	float f = 0;

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB set float param %d command: [%s]", id, params);

	if (cmd_get_param_str(params, &val, NULL))
		goto out_err;
	if (!val)
		goto out_err;
	f = (float)strtof(val, NULL);
	if (f < 0 || f > 100)
		goto out_err;

	switch (id) {
	case DATA_ID_TSET:
		boiler->data.param_desired.ch_temperature_setpoint = f;
		break;
	case DATA_ID_TDHWSET:
		boiler->data.param_desired.dhw_temperature_setpoint = f;
		break;
	default:
		goto out_err;
	}
	WEB_CLIENT_REPLY(ctx, WEB_CMD_NR);
	return 0;

out_err:

	WEB_CLIENT_REPLY(ctx, SET_ERR_STR);
	if (IS_CMD_LOG)
		hlog_warning(OTHLOG, "WEB Failed to set the param: invalid data.");
	return -1;

}

int cmd_set_dwh(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_set_status(ctx, params, user_data, false);
}

int cmd_set_dwh_temp(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_set_param_float(ctx, params, user_data, DATA_ID_TDHWSET);
}

int cmd_set_ch(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_set_status(ctx, params, user_data, true);
}

int cmd_set_ch_temp(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	return cmd_set_param_float(ctx, params, user_data, DATA_ID_TSET);
}

#define SCANN_STR "\tSupported commands:\r\n"
static int cmd_scan_all(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);
	UNUSED(boiler);
	UNUSED(cmd);

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB scan all command.");

	if (ctx->type == CMD_CTX_WEB) {
		weberv_client_send(ctx->context.web.client_idx, SCANN_STR, strlen(SCANN_STR), HTTP_RESP_OK);
		debug_log_forward(ctx->context.web.client_idx);
	}
	opentherm_cmd_scan_all();

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);

	WEB_CLIENT_REPLY(ctx, WEB_CMD_NR);
	return 0;
}

#define STATUS_STR "\tBoiler status:\r\n"
static int boiler_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);
	UNUSED(cmd);

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB boiler status command.");

	if (ctx->type == CMD_CTX_WEB) {
		weberv_client_send(ctx->context.web.client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
		debug_log_forward(ctx->context.web.client_idx);
	}

	opentherm_status_log(boiler);

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);

	WEB_CLIENT_REPLY(ctx, WEB_CMD_NR);
	return 0;
}

static int boiler_statistics_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	opentherm_context_t *boiler = (opentherm_context_t *)user_data;

	UNUSED(params);
	UNUSED(cmd);

	if (IS_CMD_LOG)
		hlog_info(OTHLOG, "WEB boiler statistics reset command.");

	opentherm_reset_statistics(&boiler->data);

	WEB_CLIENT_REPLY(ctx, WEB_CMD_NR);
	return 0;
}

static app_command_t boiler_web_requests[] = {
	{"read", ":<opentherm_cmd_id>:<value>", cmd_read},
	{"write", ":<opentherm_cmd_id>:<value>", cmd_write},
	{"debug", ":<debug_mask> - 0x1 PIO, 0x2 OT CMD, 0x4 MQTT, 0x8 USER CMD", cmd_debug},
	{"dhw", STATE_CMD_HELP, cmd_set_dwh},
	{"dhw_temp", TEMP_CMD_HELP, cmd_set_dwh_temp},
	{"ch", STATE_CMD_HELP, cmd_set_ch},
	{"ch_temp", TEMP_CMD_HELP, cmd_set_ch_temp},
	{"status", NULL, boiler_status},
	{"scan", NULL, cmd_scan_all},
	{"stat_reset", NULL, boiler_statistics_reset},
};

int boiler_cmd_init(opentherm_context_t *boiler)
{
	if (webserv_add_commands(BOILER_URL, boiler_web_requests, ARRAY_SIZE(boiler_web_requests), BOILER_DESC, boiler) < 0)
		hlog_warning(OTHLOG, "WEB Failed to register the commands.");
	if (mqtt_add_commands(BOILER_URL, boiler_web_requests, ARRAY_SIZE(boiler_web_requests), BOILER_DESC, boiler) < 0)
		hlog_warning(OTHLOG, "MQTT Failed to register the commands.");

	return 0;
}
