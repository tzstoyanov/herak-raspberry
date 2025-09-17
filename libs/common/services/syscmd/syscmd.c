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
	int delay = 0;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(wctx);

	hlog_info(SYSCMD_MODULE, "\tRebooting ...");
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
	UNUSED(ctx);
	UNUSED(wctx);

	if (!params || params[0] != ':' || strlen(params) < 2)
		return -1;
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
		return -1;

	log_level_set(lvl);

	hlog_info(SYSCMD_MODULE, "\tSetting log level ... %d", lvl);
	return 0;
}

static int sys_log_system(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);
	UNUSED(wctx);

	hlog_info(SYSCMD_MODULE, "Uptime: %s; free RAM: %d bytes; chip temperature: %3.2f *C",
			  get_uptime(), get_free_heap(), temperature_internal_get());
	sys_state_log_resources();

	return 0;
}

static int sys_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	WEBCTX_SET_KEEP_OPEN(ctx, true);
	WEBCTX_SET_KEEP_SILENT(ctx, true);
	wctx->status_log = true;
	sys_state_log_status();
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

static int sys_log_on(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(wctx);
	UNUSED(params);

	WEBCTX_SET_KEEP_SILENT(ctx, true);
	WEBCTX_SET_KEEP_OPEN(ctx, true);

	return 0;
}

static int sys_log_off(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);

	hlog_info(SYSCMD_MODULE, "\tStop sending device logs ...");

#ifdef HAVE_SYS_WEBSERVER
	if (wctx->client_log >= 0)
		webserv_client_close(wctx->client_log);
#else
	UNUSED(wctx);
#endif /* HAVE_SYS_WEBSERVER */

	return 0;
}

static int sys_debug_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);
	UNUSED(wctx);

	hlog_info(SYSCMD_MODULE, "\tGoing to reset debug state ...");
	sys_state_set_periodic_log_ms(-1);
	log_level_set(HLOG_INFO);
	sys_modules_debug_set(0);
	return 0;
}

static int sys_periodic_log(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct syscmd_context_t *wctx = (struct syscmd_context_t *)user_data;
	int delay = -1;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(wctx);

	hlog_info(SYSCMD_MODULE, "\tSetting periodic status log interval...");
	if (params && params[0] == ':' && strlen(params) > 2)
		delay = atoi(params + 1);
	if (delay < 0)
		delay = 0;
	sys_state_set_periodic_log_ms((uint32_t)delay);
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

	if (ctx->status_log && !sys_state_log_in_progress()) {
		ctx->status_log = false;
#ifdef HAVE_SYS_WEBSERVER
		if (ctx->client_log >= 0)
			webserv_client_close(ctx->client_log);
#endif /* HAVE_SYS_WEBSERVER */
	}
}

static bool syscmd_read_config(struct syscmd_context_t **ctx)
{
	char *str;

	str = USER_PRAM_GET(SYS_CMD_DEBUG);
	if (!str)
		return false;

	(*ctx) = (struct syscmd_context_t *)calloc(1, sizeof(struct syscmd_context_t));
	if (!(*ctx)) {
		free(str);
		return false;
	}

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

void sys_syscmd_register(void)
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
