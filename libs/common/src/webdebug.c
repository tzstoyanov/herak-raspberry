// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "base64.h"
#include "params.h"

#define WEBDEBUG_URL	"/debug"
#define WDBLOG			"webdbg"
#define WEBDEBUG_DESC	"Debug and extended logs commands"
#define WD_REBOOT_DELAY_MS	3000

#define DEBUG_USB	0x01
#define DEBUG_LOG	0x02
#define DEBUG_MQTT	0x04
#define DEBUG_BT	0x08

static struct {
	int hindex;
	int client_log;
	bool status_log;
	uint32_t what;
} webdebug_context;

void debug_log_forward(int client_idx)
{
	webdebug_context.client_log = client_idx;
	if (client_idx < 0)
		hlog_web_enable(false);
	else
		hlog_web_enable(true);
}

#define REBOOT_STR "\tRebooting ...\r\n"
static int debug_reboot(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	int delay = WD_REBOOT_DELAY_MS;

	UNUSED(user_data);
	UNUSED(cmd);

	WEB_CLIENT_REPLY_CLOSE(ctx, REBOOT_STR, HTTP_RESP_OK);
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	system_force_reboot(delay);
	return 0;
}

#define VERBOSE_STR "\tSetting verbose debug ...\r\n"
#define VERBOSE_ERR_STR "\tValid verbose level and module must be specified  ...\r\n"
static int debug_verbose(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	char *rest, *tok;
	uint32_t lvl, what = 0;

	UNUSED(user_data);
	UNUSED(cmd);

	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;

	rest = params + 1;
	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		goto out_err;
	lvl = (uint32_t)strtol(tok, NULL, 16);

	while ((tok = strtok_r(rest, "|", &rest))) {
		if (!strcmp(tok, "all"))
			what = 0xFF;
		else if (!strcmp(tok, "usb"))
			what |= DEBUG_USB;
		else if (!strcmp(tok, "mqtt"))
			what |= DEBUG_MQTT;
		else if (!strcmp(tok, "log"))
			what |= DEBUG_LOG;
		else if (!strcmp(tok, "bt"))
			what |= DEBUG_BT;
	}

	if (!what)
		goto out_err;

	if (what & DEBUG_LOG)
		log_debug_set(lvl);
	if (what & DEBUG_MQTT)
		mqtt_debug_set(lvl);
	if (what & DEBUG_USB)
		usb_debug_set(lvl);
	if (what & DEBUG_BT)
		bt_debug_set(lvl);

	WEB_CLIENT_REPLY_CLOSE(ctx, VERBOSE_STR, HTTP_RESP_OK);
	return 0;

out_err:
	WEB_CLIENT_REPLY_CLOSE(ctx, VERBOSE_ERR_STR, HTTP_RESP_OK);
	return 0;
}

#define LEVEL_STR "\tSetting log level ...\r\n"
#define LEVEL_ERR_STR "\tUknown log level  ...\r\n"
static int log_level(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	char *tok;
	uint32_t lvl;

	UNUSED(user_data);
	UNUSED(cmd);

	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;
	tok = params + 1;

	if (!strcmp(tok, "emerg"))
		lvl = HLOG_EMERG;
	else if (!strcmp(tok, "alert"))
		lvl = HLOG_ALERT;
	else if (!strcmp(tok, "crit"))
		lvl = HLOG_CRIT;
	else if (!strcmp(tok, "err"))
		lvl = HLOG_ERR;
	else if (!strcmp(tok, "warn"))
		lvl = HLOG_WARN;
	else if (!strcmp(tok, "notice"))
		lvl = HLOG_NOTICE;
	else if (!strcmp(tok, "info"))
		lvl = HLOG_INFO;
	else if (!strcmp(tok, "debug"))
		lvl = HLOG_DEBUG;
	else
		goto out_err;

	log_level_set(lvl);

	WEB_CLIENT_REPLY_CLOSE(ctx, LEVEL_STR, HTTP_RESP_OK);
	return 0;

out_err:
	WEB_CLIENT_REPLY_CLOSE(ctx, LEVEL_ERR_STR, HTTP_RESP_BAD);
	return 0;
}

#define STATUS_STR "\tGoing to send status ...\r\n"
#define STATUS_TOO_MANY_STR "\tA client is already receiving logs ...\r\n"
static int debug_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	if (ctx->type == CMD_CTX_WEB) {
		if (webdebug_context.client_log >= 0) {
			weberv_client_send(ctx->context.web.client_idx, STATUS_TOO_MANY_STR,
								strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		} else {
			weberv_client_send(ctx->context.web.client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
			debug_log_forward(ctx->context.web.client_idx);
			ctx->context.web.not_close = true;
		}
		ctx->context.web.not_reply = true;
	}

	webdebug_context.status_log = true;
	system_log_status();
	return 0;
}

#define PING_STR "pong\r\n"
static int debug_ping(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	WEB_CLIENT_REPLY_CLOSE(ctx, PING_STR, HTTP_RESP_OK);
	return 0;
}

#define LOGON_STR	"\tSending device logs ...\r\n"
static int debug_log_on(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	if (ctx->type != CMD_CTX_WEB)
		return 0;

	if (webdebug_context.client_log >= 0) {
		weberv_client_send(ctx->context.web.client_idx, STATUS_TOO_MANY_STR, strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		ctx->context.web.not_reply = true;
		return 0;
	}
	weberv_client_send(ctx->context.web.client_idx, LOGON_STR, strlen(LOGON_STR), HTTP_RESP_OK);
	ctx->context.web.not_reply = true;
	ctx->context.web.not_close = true;
	debug_log_forward(ctx->context.web.client_idx);
	return 0;
}

#define LOGOFF_STR	"\tStop sending device logs ...\r\n"
static int debug_log_off(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	WEB_CLIENT_REPLY_CLOSE(ctx, LOGOFF_STR, HTTP_RESP_OK);
	if (ctx->type == CMD_CTX_WEB &&
		webdebug_context.client_log != ctx->context.web.client_idx)
		weberv_client_close(webdebug_context.client_log);
	debug_log_forward(-1);
	return 0;
}

#define RESET_STR	"\tGoing to reset debug state ...\r\n"
static int debug_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	WEB_CLIENT_REPLY_CLOSE(ctx, RESET_STR, HTTP_RESP_OK);
	system_set_periodic_log_ms(0);
	log_level_set(HLOG_INFO);
	log_debug_set(0);
	usb_debug_set(0);
	mqtt_debug_set(0);
	bt_debug_set(0);
	return 0;
}

#define PERIODIC_STR "\tSetting periodic status log interval...\r\n"
static int debug_periodic_log(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	int delay = -1;

	UNUSED(cmd);
	UNUSED(user_data);

	WEB_CLIENT_REPLY_CLOSE(ctx, PERIODIC_STR, HTTP_RESP_OK);
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	if (delay < 0)
		delay = 0;
	system_set_periodic_log_ms((uint32_t)delay);
	return 0;
}

static app_command_t debug_requests[] = {
		{"reboot", ":<delay_ms>",	debug_reboot},
		{"status", NULL,			debug_status},
		{"ping", NULL,			debug_ping},
		{"periodic_log", ":<delay_ms>",	debug_periodic_log},
		{"log_on", NULL,		debug_log_on},
		{"log_off", NULL,		debug_log_off},
		{"reset", NULL,			debug_reset},
		{"level", ":<emerg|alert|crit|err|warn|notice|info|debug> - one of", log_level},
		{"verbose", ":<level_hex>:all|log|mqtt|usb|bt>", debug_verbose},
};

int webdebug_log_send(char *logbuff)
{
	int ret;

	if (webdebug_context.client_log < 0)
		return -1;
	ret = weberv_client_send_data(webdebug_context.client_log, logbuff, strlen(logbuff));
	if (ret <= 0) {
		webdebug_context.client_log = -1;
		return -1;
	}

	return 0;
}

void webdebug_run(void)
{
	if (webdebug_context.status_log && !system_log_in_progress()) {
		webdebug_context.status_log = false;
		if (webdebug_context.client_log >= 0)
			weberv_client_close(webdebug_context.client_log);
		debug_log_forward(-1);
	}
}

static bool webdebug_read_config(void)
{
	char *str;

	if (WEBDEBUG_len <= 0)
		return false;
	str = param_get(WEBDEBUG);
	webdebug_context.what = strtol(str, NULL, 16);
	free(str);
	return true;
}

bool webdebug_init(void)
{
	int idx;

	if (!webdebug_read_config())
		return false;
	mqtt_add_commands(WEBDEBUG_URL, debug_requests, ARRAY_SIZE(debug_requests), WEBDEBUG_DESC, NULL);
	idx = webserv_add_commands(WEBDEBUG_URL, debug_requests, ARRAY_SIZE(debug_requests), WEBDEBUG_DESC, NULL);
	if (idx < 0)
		return false;

	webdebug_context.hindex = idx;
	webdebug_context.client_log = -1;
	webdebug_context.status_log = false;

	return true;
}
