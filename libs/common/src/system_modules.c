// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_lib.h"
#include "common_internal.h"
#include "pico/stdlib.h"

#include "devices_init.h"
#include "systems_init.h"

#include "herak_sys.h"

#define MAX_MODULES 30
#define SYSMODLOG   "sys_mod"

static struct {
	int modules_count;
	sys_module_t *modules[MAX_MODULES];
} sys_modules_context;

int sys_module_register(sys_module_t *module)
{
	if (sys_modules_context.modules_count >= MAX_MODULES)
		return -1;
	sys_modules_context.modules[sys_modules_context.modules_count] = module;
	sys_modules_context.modules_count++;
	return 0;
}

static int cmd_module_debug(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	sys_module_t *mod = (sys_module_t *)user_data;
	uint32_t debug;

	UNUSED(cmd);
	UNUSED(ctx);

	if (!mod || !mod->debug)
		goto out;

	if (strlen(params) < 2 || params[0] != ':')
		goto out_err;
	debug = (int)strtol(params + 1, NULL, 10);
	mod->debug(debug, mod->context);
	hlog_info(SYSMODLOG, "Set debug of module %s to 0x%X", mod->name, debug);

out:
	return 0;

out_err:
	return -1;

}

#define STATUS_STR	"Module status:\n\r"
static int cmd_module_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	sys_module_t *mod = (sys_module_t *)user_data;
	bool ret;

	UNUSED(cmd);
	UNUSED(params);

	if (!mod || !mod->log)
		goto out;

	if (ctx->type == CMD_CTX_WEB) {
#ifdef HAVE_SYS_WEBSERVER
		webserv_client_send_data(ctx->context.web.client_idx, STATUS_STR, strlen(STATUS_STR));
#endif /* HAVE_SYS_WEBSERVER */
#ifdef HAVE_SYS_COMMANDS
		debug_log_forward(ctx->context.web.client_idx);
#endif /* HAVE_SYS_COMMANDS */
	}

	do {
		ret = mod->log(mod->context);
	} while (!ret);
#ifdef HAVE_SYS_COMMANDS
	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
#endif /* HAVE_SYS_COMMANDS */
out:
	return 0;
}

#define CMD_COMMON_DESC "Common module commands"
static app_command_t module_common_requests[] = {
		{"debug", ":<debug_flags>", cmd_module_debug},
		{"status", NULL, cmd_module_status},
};

void sys_modules_init(void)
{
	int ret;
	int i;

	systems_register_and_init();
	devices_register_and_init();

	for (i = 0; i < sys_modules_context.modules_count; i++) {
		if (sys_modules_context.modules[i]->commands.hooks)	{
#ifdef HAVE_SYS_WEBSERVER
			ret = webserv_add_commands(sys_modules_context.modules[i]->name,
					 sys_modules_context.modules[i]->commands.hooks, sys_modules_context.modules[i]->commands.count,
					 sys_modules_context.modules[i]->commands.description, sys_modules_context.modules[i]->context);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "WEB Failed to register commands for module %s",
							 sys_modules_context.modules[i]->name);
#endif /* HAVE_SYS_WEBSERVER */

#ifdef HAVE_SYS_MQTT
			ret = mqtt_add_commands(sys_modules_context.modules[i]->name,
					  sys_modules_context.modules[i]->commands.hooks, sys_modules_context.modules[i]->commands.count,
					  sys_modules_context.modules[i]->commands.description, sys_modules_context.modules[i]->context);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "MQTT Failed to register commands for module %s",
							 sys_modules_context.modules[i]->name);
#endif /* HAVE_SYS_MQTT */
		}
		if (sys_modules_context.modules[i]->log || sys_modules_context.modules[i]->debug) {
#ifdef HAVE_SYS_WEBSERVER
			ret = webserv_add_commands(sys_modules_context.modules[i]->name,
					 module_common_requests, ARRAY_SIZE(module_common_requests),
					 CMD_COMMON_DESC, sys_modules_context.modules[i]);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "WEB Failed to register common commands for module %s",
							 sys_modules_context.modules[i]->name);
#endif /* HAVE_SYS_WEBSERVER */

#ifdef HAVE_SYS_MQTT
			ret = mqtt_add_commands(sys_modules_context.modules[i]->name,
					  module_common_requests, ARRAY_SIZE(module_common_requests),
					  CMD_COMMON_DESC, sys_modules_context.modules[i]);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "MQTT Failed to register common commands for module %s",
							 sys_modules_context.modules[i]->name);
		}
#endif /* HAVE_SYS_MQTT */
		if (sys_modules_context.modules[i]->log) {
			ret = add_status_callback(sys_modules_context.modules[i]->log, sys_modules_context.modules[i]->context);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "LOG Failed to register log callback for module %s",
							 sys_modules_context.modules[i]->name);
		}
	}
}

void sys_modules_log(void)
{
	int i;

	hlog_info(SYSMODLOG, "  Registered %d modules:", sys_modules_context.modules_count);
	for (i = 0; i < sys_modules_context.modules_count; i++)
		hlog_info(SYSMODLOG, "    [%s]", sys_modules_context.modules[i]->name);
}

void sys_modules_reconnect(void)
{
	int i;

	for (i = 0; i < sys_modules_context.modules_count; i++) {
		if (!sys_modules_context.modules[i]->reconnect)
			continue;
		LOOP_FUNC_RUN(sys_modules_context.modules[i]->name,
					  sys_modules_context.modules[i]->reconnect,
					  sys_modules_context.modules[i]->context);
	}
}

void sys_modules_debug_set(int debug)
{
	int i;

	for (i = 0; i < sys_modules_context.modules_count; i++) {
		if (!sys_modules_context.modules[i]->debug)
			continue;
		sys_modules_context.modules[i]->debug(debug, sys_modules_context.modules[i]->context);
	}
}

void sys_modules_run(void)
{
	int i;

	for (i = 0; i < sys_modules_context.modules_count; i++) {
		if (!sys_modules_context.modules[i]->run)
			continue;
		LOOP_FUNC_RUN(sys_modules_context.modules[i]->name,
					  sys_modules_context.modules[i]->run,
					  sys_modules_context.modules[i]->context);
	}
}
