// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "herak_sys.h"
#include "common_internal.h"

#define CMD_MODULE				"commands"
#define MAX_CMD_MOD_HANDLERS	64
#define MAX_CMD_MOD_HOOKS		2
#define CMD_PARAM_DELIMITER		'?'

#define IS_DEBUG(C)	((C) && (C)->debug)

struct cmd_handler_t {
	int count;
	char *description;
	void *user_data;
	app_command_t *cmd_cb;
};

struct cmd_mod_handler_t {
	char *module;
	int count;
	struct cmd_handler_t *mod_cb[MAX_CMD_MOD_HOOKS];
};

struct cmd_context_t {
	sys_module_t mod;
	int count;
	struct cmd_mod_handler_t *handlers[MAX_CMD_MOD_HANDLERS];
	uint32_t debug;
};

static struct cmd_context_t *__cmd_context;

static struct cmd_context_t *cmd_context_get(void)
{
	return __cmd_context;
}

static void sys_cmd_module_help(struct cmd_mod_handler_t *handler)
{
	int i, j;
	
	hlog_info(CMD_MODULE, "\tModule [%s]:", handler->module);
	for (i = 0; i < handler->count; i++) {
		for (j = 0; j < handler->mod_cb[i]->count; j++) {
			hlog_info(CMD_MODULE, "\t  \t%s?%s%s",
					  handler->module,
					  handler->mod_cb[i]->cmd_cb[j].command,
					  handler->mod_cb[i]->cmd_cb[j].help);
		}
	}
}

void cmd_module_help(char *module)
{
	struct cmd_context_t *ctx = cmd_context_get();
	int i;

	if (!ctx)
		return;
	for (i = 0; i < ctx->count; i++) {
		if (strlen(module) != strlen(ctx->handlers[i]->module))
			continue;
		if (strncmp(module, ctx->handlers[i]->module, strlen(module)))
			continue;
		sys_cmd_module_help(ctx->handlers[i]);
		break;
	}

}

static bool sys_cmd_log_status(void *context)
{
	struct cmd_context_t *ctx = (struct cmd_context_t *)context;
	int i;

	hlog_info(CMD_MODULE, "Registered modules:");
	for (i = 0; i < ctx->count; i++)
		hlog_info(CMD_MODULE, "\t%s",ctx->handlers[i]->module);
	hlog_info(CMD_MODULE, "Run `<module_name>?help` for more information.");

	return true;
}

static void sys_cmd_debug_set(uint32_t lvl, void *context)
{
	struct cmd_context_t *ctx = (struct cmd_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_cmd_init(struct cmd_context_t **ctx)
{
	(*ctx) = (struct cmd_context_t *)calloc(1, sizeof(struct cmd_context_t));
	if (!(*ctx))
		return false;

	__cmd_context = (*ctx);
	return true;
}

static char *run_ctx_name(cmd_run_context_t *cmd_ctx)
{
	switch (cmd_ctx->type) {
	case CMD_CTX_WEB:
		return "WEB";
	case CMD_CTX_MQTT:
		return "MQTT";
	case CMD_CTX_SCRIPT:
		return "Script";
	}

	return "Invalid";
}

#define HELP_CMD	"help"
static int cmd_help(struct cmd_context_t *ctx, cmd_run_context_t *cmd_ctx, char *cmd_str)
{
	UNUSED(cmd_ctx);

	if (strlen(HELP_CMD) != strlen(cmd_str))
		return -1;
	if (strncmp(HELP_CMD, cmd_str, strlen(HELP_CMD)))
		return -1;
	sys_cmd_log_status(ctx);
	return 0;
}

/* API */
int cmd_exec(cmd_run_context_t *cmd_ctx, char *cmd_str)
{
	struct cmd_context_t *ctx = cmd_context_get();
	app_command_t *cmd;
	bool exec = false;
	int data_offset;
	int ret = -1;
	int i, j, k;
	char *url;

	if (!ctx || !cmd_str)
		return -1;

	for (i = 0; i < ctx->count; i++) {
		url = ctx->handlers[i]->module;
		if (!url)
			continue;
		if (strlen(url) > strlen(cmd_str))
			continue;
		if (strncmp(url, cmd_str, strlen(url)))
			continue;
		data_offset = strlen(url);
		if (cmd_str[data_offset] != CMD_PARAM_DELIMITER)
			continue;
		data_offset++;
		for (j = 0; j < ctx->handlers[i]->count; j++) {
			for (k = 0; k < ctx->handlers[i]->mod_cb[j]->count; k++) {
				cmd = &ctx->handlers[i]->mod_cb[j]->cmd_cb[k];
				if (!cmd->cb)
					continue;
				if (strlen(cmd->command) > strlen(cmd_str + data_offset))
					continue;
				if (strncmp(cmd->command, cmd_str + data_offset, strlen(cmd->command)))
					continue;
				if (strlen(cmd_str + data_offset) > strlen(cmd->command)) {
					if (cmd_str[data_offset + strlen(cmd->command)] != ':')
						continue;
				}
				data_offset += strlen(cmd->command);
				ret = cmd->cb(cmd_ctx, cmd->command, cmd_str + data_offset,
							  ctx->handlers[i]->mod_cb[j]->user_data);
				exec = true;
				break;
			}
			if (k < ctx->handlers[i]->mod_cb[j]->count)
				break;
		}
		break;
	}

	if (!exec) {
		ret = cmd_help(ctx, cmd_ctx, cmd_str);
		if (!ret)
			exec = true;
	}

	if (exec) {
		if (IS_DEBUG(ctx))
			hlog_info(CMD_MODULE, "Executed %s command: [%s]",
					  run_ctx_name(cmd_ctx), cmd_str);
	} else {
		if (IS_DEBUG(ctx))
			hlog_info(CMD_MODULE, "Uknown %s command: [%s]",
					  run_ctx_name(cmd_ctx), cmd_str);
	}

	return ret;
}

int cmd_handler_add(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data)
{
	struct cmd_context_t *ctx = cmd_context_get();
	struct cmd_handler_t *cmd_handler = NULL;
	struct cmd_mod_handler_t *mod_handler = NULL;
	int i;

	if (!ctx)
		return -1;

	for (i = 0; i < ctx->count; i++) {
		if (strlen(module) != strlen(ctx->handlers[i]->module))
			continue;
		if (strncmp(module, ctx->handlers[i]->module, strlen(module)))
			continue;
		mod_handler = ctx->handlers[i];
		break;
	}

	if (!mod_handler) {
		if (ctx->count >= MAX_CMD_MOD_HANDLERS)
			return -1;

		mod_handler = calloc(1, sizeof(struct cmd_mod_handler_t));
		if (!mod_handler)
			return -1;
		mod_handler->module = module;
		ctx->handlers[ctx->count] = mod_handler;
		ctx->count++;
	}

	if (mod_handler->count >= MAX_CMD_MOD_HOOKS)
		return -1;
	cmd_handler = calloc(1, sizeof(struct cmd_handler_t));
	if (!cmd_handler)
		return -1;
	cmd_handler->description = description;
	cmd_handler->count = commands_cont;
	cmd_handler->user_data = user_data;
	cmd_handler->cmd_cb = commands;
	mod_handler->mod_cb[mod_handler->count] = cmd_handler;
	mod_handler->count++;

	return 0;
}

void sys_commands_register(void)
{
	struct cmd_context_t *ctx = NULL;

	if (!sys_cmd_init(&ctx))
		return;

	ctx->mod.name = CMD_MODULE;
	ctx->mod.log = sys_cmd_log_status;
	ctx->mod.debug = sys_cmd_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

