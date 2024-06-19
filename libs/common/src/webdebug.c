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
	bool disable_log_forward;
	uint32_t what;
} webdebug_context;

static void debug_log_forward(int client_idx)
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

#define STATUS_STR "\tGoing to send status ...\r\n"
static void debug_status(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);
	weberv_client_send(client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
	debug_log_forward(client_idx);
	system_log_status();
	debug_log_forward(-1);
	weberv_client_close(client_idx);
}

#define LOGON_STR	"\tSending device logs ...\r\n"
static void debug_log_on(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

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
		{"periodic_log", ":<delay_ms>",	debug_periodic_log},
		{"log_on", NULL,		debug_log_on},
		{"log_off", NULL,		debug_log_off},
		{"reset", NULL,			debug_reset},
		{"verbose", ":<level_hex>:all|log|mqtt|usb|bt", debug_verbose},
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
	return true;
}
