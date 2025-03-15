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
static void debug_reboot(int client_idx, char *params, void *user_data)
{
	int delay = WD_REBOOT_DELAY_MS;

	UNUSED(user_data);

	weberv_client_send(client_idx, REBOOT_STR, strlen(REBOOT_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	system_force_reboot(delay);
}

#define VERBOSE_STR "\tSetting verbose debug ...\r\n"
#define VERBOSE_ERR_STR "\tValid verbose level and module must be specified  ...\r\n"
static void debug_verbose(int client_idx, char *params, void *user_data)
{
	char *rest, *tok;
	uint32_t lvl, what = 0;

	UNUSED(user_data);

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

	weberv_client_send(client_idx, VERBOSE_STR, strlen(VERBOSE_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	return;

out_err:
	weberv_client_send(client_idx, VERBOSE_ERR_STR, strlen(VERBOSE_ERR_STR), HTTP_RESP_BAD);
	weberv_client_close(client_idx);
}

#define LEVEL_STR "\tSetting log level ...\r\n"
#define LEVEL_ERR_STR "\tUknown log level  ...\r\n"
static void log_level(int client_idx, char *params, void *user_data)
{
	char *tok;
	uint32_t lvl;

	UNUSED(user_data);

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

	weberv_client_send(client_idx, LEVEL_STR, strlen(LEVEL_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	return;

out_err:
	weberv_client_send(client_idx, LEVEL_ERR_STR, strlen(LEVEL_ERR_STR), HTTP_RESP_BAD);
	weberv_client_close(client_idx);
}

#define STATUS_STR "\tGoing to send status ...\r\n"
#define STATUS_TOO_MANY_STR "\tA client is already receiving logs ...\r\n"
static void debug_status(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	if (webdebug_context.client_log >= 0) {
		weberv_client_send(client_idx, STATUS_TOO_MANY_STR, strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		return;
	}

	weberv_client_send(client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
	debug_log_forward(client_idx);
	webdebug_context.status_log = true;
	system_log_status();
}

#define PING_STR "pong\r\n"
static void debug_ping(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	weberv_client_send(client_idx, PING_STR, strlen(PING_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

#define LOGON_STR	"\tSending device logs ...\r\n"
static void debug_log_on(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	if (webdebug_context.client_log >= 0) {
		weberv_client_send(client_idx, STATUS_TOO_MANY_STR, strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		return;
	}

	weberv_client_send(client_idx, LOGON_STR, strlen(LOGON_STR), HTTP_RESP_OK);
	debug_log_forward(client_idx);
}

#define LOGOFF_STR	"\tStop sending device logs ...\r\n"
static void debug_log_off(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	weberv_client_send(client_idx, LOGOFF_STR, strlen(LOGOFF_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	if (webdebug_context.client_log != client_idx)
		weberv_client_close(webdebug_context.client_log);
	debug_log_forward(-1);
}

#define RESET_STR	"\tGoing to reset debug state ...\r\n"
static void debug_reset(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	weberv_client_send(client_idx, RESET_STR, strlen(RESET_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	system_set_periodic_log_ms(0);
	log_level_set(HLOG_INFO);
	log_debug_set(0);
	usb_debug_set(0);
	mqtt_debug_set(0);
	bt_debug_set(0);
}

#define PERIODIC_STR "\tSetting periodic status log interval...\r\n"
static void debug_periodic_log(int client_idx, char *params, void *user_data)
{
	int delay = -1;

	UNUSED(user_data);

	weberv_client_send(client_idx, PERIODIC_STR, strlen(PERIODIC_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	if (delay < 0)
		delay = 0;
	system_set_periodic_log_ms((uint32_t)delay);
}

static web_requests_t debug_requests[] = {
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

	idx = webserv_add_commands(WEBDEBUG_URL, debug_requests, ARRAY_SIZE(debug_requests), WEBDEBUG_DESC, NULL);
	if (idx < 0)
		return false;

	webdebug_context.hindex = idx;
	webdebug_context.client_log = -1;
	webdebug_context.status_log = false;

	return true;
}
