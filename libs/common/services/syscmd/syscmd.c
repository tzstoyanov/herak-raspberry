// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "herak_sys.h"
#include "base64.h"
#include "params.h"

#define SYSCMD_MODULE		"sys"
#define SYSCMD_DESC			"System commands"
#define WD_REBOOT_DELAY_MS	3000

#define DEBUG_LOG	0x01

struct syscmd_context_t {
	sys_module_t mod;
	int client_log;
	bool status_log;
	uint32_t what;
	uint32_t debug;
};

static struct syscmd_context_t *__sys_cmd_context;

static struct syscmd_context_t *syscmd_context_get(void)
{
	return __sys_cmd_context;
}

static int sys_reboot(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;
	int delay = WD_REBOOT_DELAY_MS;

	UNUSED(cmd);
	UNUSED(wctx);

	WEB_CLIENT_REPLY(ctx, "\tRebooting ...\r\n");
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	system_force_reboot(delay);
	return 0;
}

static int sys_log_level(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;
	char *tok;
	uint32_t lvl;

	UNUSED(cmd);
	UNUSED(wctx);

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

	WEB_CLIENT_REPLY(ctx, "\tSetting log level ...\r\n");
	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, "\tUknown log level  ...\r\n");
	return 0;
}

#define LSYS_STATUS_STR "\tLow level internals ...\r\n"
static int sys_log_system(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);
	UNUSED(wctx);

#ifdef HAVE_SYS_WEBSERVER
	if (ctx->type == CMD_CTX_WEB) {
		webserv_client_send_data(ctx->context.web.client_idx, LSYS_STATUS_STR, strlen(LSYS_STATUS_STR));
		debug_log_forward(ctx->context.web.client_idx);
	}
#endif

	hlog_info(SYSCMD_MODULE, "Uptime: %s; free RAM: %d bytes; chip temperature: %3.2f *C",
			  get_uptime(), get_free_heap(), temperature_internal_get());
	log_sys_health();
	stats_display();

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
	return 0;
}

#define STATUS_STR "\tGoing to send status ...\r\n"
#define STATUS_TOO_MANY_STR "\tA client is already receiving logs ...\r\n"
static int sys_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

#ifdef HAVE_SYS_WEBSERVER
	if (ctx->type == CMD_CTX_WEB) {
		if (wctx->client_log >= 0) {
			webserv_client_send(ctx->context.web.client_idx, STATUS_TOO_MANY_STR,
								strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		} else {
			webserv_client_send(ctx->context.web.client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
			debug_log_forward(ctx->context.web.client_idx);
			ctx->context.web.keep_open = true;
		}
		ctx->context.web.keep_silent = true;
	}
#else
	UNUSED(wctx);
	UNUSED(ctx);
#endif

	wctx->status_log = true;
	system_log_status();
	return 0;
}

static int sys_ping(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);
	UNUSED(wctx);

	WEB_CLIENT_REPLY(ctx, "pong\r\n");
	return 0;
}

#define LOGON_STR	"\tSending device logs ...\r\n"
static int sys_log_on(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	if (ctx->type != CMD_CTX_WEB)
		return 0;

#ifdef HAVE_SYS_WEBSERVER
	if (wctx->client_log >= 0) {
		webserv_client_send(ctx->context.web.client_idx, STATUS_TOO_MANY_STR, strlen(STATUS_TOO_MANY_STR), HTTP_RESP_TOO_MANY_ERROR);
		ctx->context.web.keep_open = true;
		return 0;
	}
	webserv_client_send(ctx->context.web.client_idx, LOGON_STR, strlen(LOGON_STR), HTTP_RESP_OK);
#else
	UNUSED(wctx);
#endif /* HAVE_SYS_WEBSERVER */

	ctx->context.web.keep_silent = true;
	ctx->context.web.keep_open = true;
	debug_log_forward(ctx->context.web.client_idx);
	return 0;
}

static int sys_log_off(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	WEB_CLIENT_REPLY(ctx, "\tStop sending device logs ...\r\n");

#ifdef HAVE_SYS_WEBSERVER
	if (ctx->type == CMD_CTX_WEB &&
		wctx->client_log != ctx->context.web.client_idx)
		webserv_client_close(wctx->client_log);
#else
	UNUSED(wctx);
#endif /* HAVE_SYS_WEBSERVER */

	debug_log_forward(-1);
	return 0;
}

static int sys_debug_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);
	UNUSED(wctx);

	WEB_CLIENT_REPLY(ctx, "\tGoing to reset debug state ...\r\n");
	system_set_periodic_log_ms(0);
	log_level_set(HLOG_INFO);
	sys_modules_debug_set(0);
	return 0;
}

static int sys_periodic_log(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;
	int delay = -1;

	UNUSED(cmd);
	UNUSED(wctx);

	WEB_CLIENT_REPLY(ctx, "\tSetting periodic status log interval...\r\n");
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	if (delay < 0)
		delay = 0;
	system_set_periodic_log_ms((uint32_t)delay);
	return 0;
}

static app_command_t syscmd_requests[] = {
		{"reboot", ":<delay_ms>",	sys_reboot},
		{"status", NULL,			sys_status},
		{"log_sys", NULL,			sys_log_system},
		{"ping", NULL,			sys_ping},
		{"periodic_log", ":<delay_ms>",	sys_periodic_log},
		{"log_on", NULL,		sys_log_on},
		{"log_off", NULL,		sys_log_off},
		{"reset", NULL,			sys_debug_reset},
		{"log", ":<emerg|alert|crit|err|warn|notice|info|debug> - one of", sys_log_level},
};

/*   API  */
void debug_log_forward(int client_idx)
{
	struct syscmd_context_t *ctx = syscmd_context_get();

	if (!ctx)
		return;

	ctx->client_log = client_idx;
	if (client_idx < 0)
		hlog_web_enable(false);
	else
		hlog_web_enable(true);
}

int syscmd_log_send(char *logbuff)
{
	struct syscmd_context_t *ctx = syscmd_context_get();
	int ret;

	if (!ctx)
		return -1;

	if (ctx->client_log < 0)
		return -1;

#ifdef HAVE_SYS_WEBSERVER
	ret = webserv_client_send_data(ctx->client_log, logbuff, strlen(logbuff));
	if (ret <= 0) {
		ctx->client_log = -1;
		return -1;
	}
#else
	UNUSED(logbuff);
	UNUSED(ret);
#endif

	return 0;
}

/*   System Module  */
static void sys_commands_run(void *context)
{
	struct syscmd_context_t *ctx = (struct syscmd_context_t *)context;

	if (ctx->status_log && !system_log_in_progress()) {
		ctx->status_log = false;
#ifdef HAVE_SYS_WEBSERVER
		if (ctx->client_log >= 0)
			webserv_client_close(ctx->client_log);
#endif /* HAVE_SYS_WEBSERVER */
		debug_log_forward(-1);
	}
}

static bool syscmd_read_config(struct syscmd_context_t **ctx)
{
	char *str;

	if (SYS_CMD_DEBUG_len <= 0)
		return false;

	(*ctx) = (struct syscmd_context_t *)calloc(1, sizeof(struct syscmd_context_t));
	if (!(*ctx))
		return false;

	str = param_get(SYS_CMD_DEBUG);
	(*ctx)->what = strtol(str, NULL, 0);
	free(str);

	return true;
}

static bool sys_commands_init(struct syscmd_context_t **ctx)
{
	if (!syscmd_read_config(ctx))
		return false;

	(*ctx)->client_log = -1;
	(*ctx)->status_log = false;
	__sys_cmd_context = (*ctx);

	return true;
}

static void sys_commands_debug_set(uint32_t lvl, void *context)
{
	struct syscmd_context_t *ctx = (struct syscmd_context_t *)context;

	ctx->debug = lvl;
}

void sys_commands_register(void)
{
	struct syscmd_context_t *ctx = NULL;

	if (!sys_commands_init(&ctx))
		return;

	ctx->mod.name = SYSCMD_MODULE;
	ctx->mod.run = sys_commands_run;
	ctx->mod.debug = sys_commands_debug_set;
	ctx->mod.commands.hooks = syscmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(syscmd_requests);
	ctx->mod.commands.description = "System";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
