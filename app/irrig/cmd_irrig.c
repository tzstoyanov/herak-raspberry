// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "irrig.h"

#define SSRLOG	"ssr"
#define SSR_URL	"/ssr"
#define SSR_DESC	"Solid State Relay controls"
#define WEB_DATA_LEN	64
#define SSR_STATE_DONE "\r\n"

static int cmd_ssr_set_state(char *cmd, char *params, void *context)
{
	int id, state, time, delay;
	char *tok, *rest = params;

	UNUSED(context);
	if (!params)
		return -1;

	hlog_info(SSRLOG, "Going to execute command [%s] with params [%s]", cmd, params);

	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		return -1;
	id = (int)strtol(tok, NULL, 10);
	if (id < 0)
		return -1;

	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		return -1;
	state = (int)strtol(tok, NULL, 10);

	/* Get state time */
	time = 0;
	delay = 0;
	tok = strtok_r(rest, ":", &rest);
	if (tok) {
		time = (int)strtol(tok, NULL, 10);
		if (time < 0)
			time = 0;
		/* sec -> ms */
		time *= 1000;

		/* Get delay */
		tok = strtok_r(rest, ":", &rest);
		if (tok) {
			delay = (int)strtol(tok, NULL, 10);
			if (delay < 0)
				delay = 0;
			/* sec -> ms */
			delay *= 1000;
		}
	}

	return ssr_state_set(id, state, time, delay);
}

#define STATUS_STR "\tSSR status: \r\n"
static int cmd_ssr_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	if (ctx->type == CMD_CTX_WEB) {
		weberv_client_send(ctx->context.web.client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
		debug_log_forward(ctx->context.web.client_idx);
	}
	ssr_log(NULL);

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
	WEB_CLIENT_REPLY_CLOSE(ctx, SSR_STATE_DONE, HTTP_RESP_OK);
	return 0;
}

static int cmd_ssr_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);
	UNUSED(user_data);

	ssr_reset_all();

	return 0;
}

#define SET_OK_STR "\tSSR switched.\r\n"
#define SET_ERR_STR "\tInvalid parameters.\r\n"
static int cmd_ssr_set(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	UNUSED(user_data);

	if (strlen(params) < 2 || params[0] != ':')
		goto out_err;

	if (cmd_ssr_set_state(cmd, params, user_data))
		goto out_err;

	WEB_CLIENT_REPLY_CLOSE(ctx, SET_OK_STR, HTTP_RESP_OK);
	return 0;

out_err:
	WEB_CLIENT_REPLY_CLOSE(ctx, SET_ERR_STR, HTTP_RESP_BAD);
	return -1;
}

static app_command_t ssr_requests[] = {
		{"set", ":<ssr_id>:<state_0_1>:<state_time_sec>:<delay_sec>", cmd_ssr_set},
		{"reset", NULL, cmd_ssr_reset},
		{"status", NULL, cmd_ssr_status},
};

int cmd_irrig_init(void)
{
	if (webserv_add_commands(SSR_URL, ssr_requests, ARRAY_SIZE(ssr_requests), SSR_DESC, NULL) < 0)
		hlog_warning(SSRLOG, "WEB Failed to register the commands.");
	if (mqtt_add_commands(SSR_URL, ssr_requests, ARRAY_SIZE(ssr_requests), SSR_DESC, NULL) < 0)
		hlog_warning(SSRLOG, "MQTT Failed to register the commands.");
	return 0;
}
