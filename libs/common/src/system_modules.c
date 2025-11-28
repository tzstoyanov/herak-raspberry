// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "devices_init.h"
#include "systems_init.h"

#define MAX_MODULES 30
#define SYSMODLOG   "sys_mod"

static struct {
	int modules_count;
	sys_module_t *modules[MAX_MODULES];
	uint32_t job_state;
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

	if (!mod)
		goto out;
	if (!mod->debug) {
		hlog_info(SYSMODLOG, "Module %s does not support debug flags", mod->name);
		goto out;
	}

	if (strlen(params) < 2 || params[0] != ':')
		goto out_err;
	debug = (int)strtol(params + 1, NULL, 0);
	mod->debug(debug, mod->context);
	hlog_info(SYSMODLOG, "Set debug of module %s to 0x%X", mod->name, debug);

out:
	return 0;

out_err:
	return -1;

}

static int cmd_mod_help(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	sys_module_t *mod = (sys_module_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);

	if (!mod)
		return 0;

	cmd_module_help(mod->name);
	return 0;
}

static int cmd_module_status(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	sys_module_t *mod = (sys_module_t *)user_data;
	bool ret;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);

	if (!mod)
		goto out;

	if (!mod->log) {
		hlog_info(SYSMODLOG, "Module %s does not support status reporting", mod->name);
		goto out;
	}

	do {
		ret = mod->log(mod->context);
	} while (!ret);

out:
	return 0;
}

static void sys_module_debug_init(sys_module_t *module)
{
	if (!module->debug)
		return;

#ifdef HAVE_SYS_CFG_STORE
	{
		char *dname;
		char *val = NULL;
		uint32_t debug;

		sys_asprintf(&dname, "dbg_%s", module->name);
		val = cfgs_param_get(dname);
		if (val) {
			debug = (int)strtol(val, NULL, 0);
			module->debug(debug, module->context);
		}
	}
#endif /* HAVE_SYS_CFG_STORE */
}

#define CMD_COMMON_DESC "Common module commands"
static app_command_t module_common_requests[] = {
		{"debug", ":<debug_flags> - set module debug flags", cmd_module_debug},
		{"status", " - report module status", cmd_module_status},
		{"help", " - list commands supported by the module", cmd_mod_help},
};

void sys_modules_init(void)
{
	int ret;
	int i;

	systems_register_and_init();
	devices_register_and_init();

	for (i = 0; i < sys_modules_context.modules_count; i++) {
		sys_module_debug_init(sys_modules_context.modules[i]);
		if (sys_modules_context.modules[i]->commands.hooks)	{
#ifdef HAVE_COMMANDS
		ret = cmd_handler_add(sys_modules_context.modules[i]->name,
					 sys_modules_context.modules[i]->commands.hooks, sys_modules_context.modules[i]->commands.count,
					 sys_modules_context.modules[i]->commands.description, sys_modules_context.modules[i]->context);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "Failed to register commands for module %s",
							 sys_modules_context.modules[i]->name);					 
#endif /* HAVE_COMMANDS */
		}
#ifdef HAVE_COMMANDS
		ret = cmd_handler_add(sys_modules_context.modules[i]->name,
				 module_common_requests, ARRAY_SIZE(module_common_requests),
				 CMD_COMMON_DESC, sys_modules_context.modules[i]);
		if (ret < 0)
			hlog_warning(SYSMODLOG, "Failed to register common commands for module %s",
						 sys_modules_context.modules[i]->name);
#endif /* HAVE_COMMANDS */			
		if (sys_modules_context.modules[i]->log) {
			ret = sys_state_callback_add(sys_modules_context.modules[i]->log, sys_modules_context.modules[i]->context);
			if (ret < 0)
				hlog_warning(SYSMODLOG, "LOG Failed to register log callback for module %s",
							 sys_modules_context.modules[i]->name);
		}
	}
}

void sys_modules_log(void)
{
	int i;

	if (sys_modules_context.job_state)
		hlog_info(SYSMODLOG, "  Running job 0x%04X", sys_modules_context.job_state);
	hlog_info(SYSMODLOG, "  Registered %d modules:", sys_modules_context.modules_count);
	for (i = 0; i < sys_modules_context.modules_count; i++) {
		hlog_info(SYSMODLOG, "    [%s]%s",
				  sys_modules_context.modules[i]->name,
				  (sys_modules_context.job_state &&
				  !(sys_modules_context.job_state & sys_modules_context.modules[i]->job_flags)) ?
				  "\t\tpaused" : "");
	}
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
		if (sys_modules_context.job_state &&
			!(sys_modules_context.job_state & sys_modules_context.modules[i]->job_flags))
			continue;
		LOOP_FUNC_RUN(sys_modules_context.modules[i]->name,
					  sys_modules_context.modules[i]->run,
					  sys_modules_context.modules[i]->context);
	}
}

void sys_job_state_set(uint32_t job)
{
	sys_modules_context.job_state |= job;
}

void sys_job_state_clear(uint32_t job)
{
	sys_modules_context.job_state &= ~job;
}
