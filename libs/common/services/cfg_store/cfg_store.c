// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico_hal.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define CFGS_MODULE "config"
#define CFG_DIR     "/herak_cfg"
#define MAX_VARS     50
#define BUFF_SIZE    300

struct cfg_store_t {
	char *name;
};

struct cfgs_context_t {
	sys_module_t mod;
	char buff[BUFF_SIZE];
	struct cfg_store_t *cfg_params[MAX_VARS];
	int count;
	uint32_t debug;
};

static struct cfgs_context_t *__cfgs_context;

static struct cfgs_context_t *cfgs_context_get(void)
{
	return __cfgs_context;
}

static bool sys_cfgs_log_status(void *context)
{
	struct cfgs_context_t *ctx = (struct cfgs_context_t *)context;

	UNUSED(ctx);

	return true;
}

static void sys_cfgs_debug_set(uint32_t lvl, void *context)
{
	struct cfgs_context_t *ctx = (struct cfgs_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_cfgs_init(struct cfgs_context_t **ctx)
{
	int fd;

	(*ctx) = (struct cfgs_context_t *)calloc(1, sizeof(struct cfgs_context_t));
	if (!(*ctx))
		return false;

	if (fs_is_mounted()) {
		fd = pico_dir_open(CFG_DIR);
		if (fd < 0)
			pico_mkdir(CFG_DIR);
		else
			pico_dir_close(fd);
	}

	__cfgs_context = *ctx;

	return true;
}

static struct cfg_store_t *cfgs_param_find(struct cfgs_context_t *ctx, char *name)
{
	int i;

	for (i = 0; i < MAX_VARS; i++) {
		if (!ctx->cfg_params[i])
			break;
		if (strlen(name) != strlen(ctx->cfg_params[i]->name))
			continue;
		if (strcmp(name, ctx->cfg_params[i]->name))
			continue;
		return ctx->cfg_params[i];
	}
	return NULL;
}

static struct cfg_store_t *cfgs_param_register(struct cfgs_context_t *ctx, char *name)
{
	if (ctx->count >= MAX_VARS)
		return NULL;
	ctx->cfg_params[ctx->count] = (struct cfg_store_t *)calloc(1, sizeof(struct cfg_store_t));
	if (!ctx->cfg_params[ctx->count])
		return NULL;
	ctx->cfg_params[ctx->count]->name = name;
	ctx->count++;
	return ctx->cfg_params[ctx->count - 1];
}

static char *cfgs_param_read(struct cfgs_context_t *ctx, struct cfg_store_t *var)
{
	char *ret = NULL;
	int sz;
	int fd;

	snprintf(ctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, var->name);

	fd = pico_open(ctx->buff, LFS_O_RDONLY);
	if (fd < 0)
		return NULL;
	sz = pico_read(fd, ctx->buff, BUFF_SIZE);
	if (sz > 1)
		ret = base64_decode(ctx->buff, sz);
	pico_close(fd);
	return ret;
}

static void cfgs_purge_uknown(struct cfgs_context_t *ctx)
{
	struct lfs_info linfo;
	int ret;
	int fd;

	fd = pico_dir_open(CFG_DIR);
	if (fd < 0)
		return;

	do {
		ret = pico_dir_read(fd, &linfo);
		if (ret > 0 && linfo.type == LFS_TYPE_REG) {
			if (!cfgs_param_find(ctx, linfo.name)) {
				snprintf(ctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, linfo.name);
				pico_remove(ctx->buff);
			}
		}
		if (ret <= 0)
			break;
	} while (true);

	pico_dir_close(fd);
}

static int cfgs_param_set(struct cfgs_context_t *ctx, char *name, char *value)
{
	char *enc_val = NULL;
	unsigned int sz;
	int ret = -1;
	int fd;

	if (!cfgs_param_find(ctx, name))
		return -1;

	snprintf(ctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, name);

	fd = pico_open(ctx->buff, LFS_O_WRONLY|LFS_O_TRUNC|LFS_O_CREAT);
	if (fd < 0)
		return -1;
	enc_val = base64_encode(value, strlen(value));
	if (!enc_val)
		goto out;

	sz = pico_write(fd, enc_val, strlen(enc_val));
	if (sz == strlen(enc_val))
		ret = 0;

out:
	if (enc_val)
		free(enc_val);
	pico_close(fd);
	return ret;
}

char *cfgs_param_get(char *name)
{
	struct cfgs_context_t *ctx = cfgs_context_get();
	struct cfg_store_t *var = NULL;

	if (!ctx)
		return NULL;
	var = cfgs_param_find(ctx, name);
	if (!var)
		var = cfgs_param_register(ctx, name);
	if (!var)
		return NULL;
	return cfgs_param_read(ctx, var);
}

static void cfgs_reset_all(struct cfgs_context_t *ctx)
{
	struct lfs_info linfo;
	int fd;

	do {
		fd = pico_dir_open(CFG_DIR);
		if (fd < 0)
			break;
		if (pico_dir_read(fd, &linfo) <= 0)
			break;
		snprintf(ctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, linfo.name);
		pico_remove(ctx->buff);
		pico_dir_close(fd);
	} while (true);
}

static int cfgs_reset_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct cfgs_context_t *wctx = (struct cfgs_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	WEB_CLIENT_REPLY(ctx, "Reset to default all configuration\r\n");
	cfgs_reset_all(wctx);
	return 0;
}

static int cfgs_list_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct cfgs_context_t *wctx = (struct cfgs_context_t *)user_data;
	int fd;
	int i;

	UNUSED(cmd);
	UNUSED(params);

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(ctx->context.web.client_idx);

	hlog_info(CFGS_MODULE, "Supported config parameters:");
	for (i = 0; i < wctx->count; i++) {
		snprintf(wctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, wctx->cfg_params[i]->name);
		fd = pico_open(wctx->buff, LFS_O_RDONLY);
		hlog_info(CFGS_MODULE, "\t [%c] %s",
				  fd >= 0 ? '*' : ' ',
				  wctx->cfg_params[i]->name);
		if (fd >= 0)
			pico_close(fd);
	}

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
	return 0;
}

static int cfgs_set_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct cfgs_context_t *wctx = (struct cfgs_context_t *)user_data;
	char *name, *rest = params;

	UNUSED(cmd);
	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;
	name = strtok_r(rest, ":", &rest);
	if (!name)
		goto out_err;

	if (cfgs_param_set(wctx, name, rest) < 0)
		goto out_err;

	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, "\tUknown parameter  ...\r\n");
	return 0;
}

static int cfgs_del_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct cfgs_context_t *wctx = (struct cfgs_context_t *)user_data;
	char *name, *rest = params;

	UNUSED(cmd);
	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;
	name = strtok_r(rest, ":", &rest);
	if (!name)
		goto out_err;

	snprintf(wctx->buff, BUFF_SIZE, "%s/%s", CFG_DIR, name);
	pico_remove(wctx->buff);

	return 0;

out_err:
	WEB_CLIENT_REPLY(ctx, "\tUknown parameter  ...\r\n");
	return 0;
}

static int cfgs_purge_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct cfgs_context_t *wctx = (struct cfgs_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	WEB_CLIENT_REPLY(ctx, "\tDelete unkown local configuration  ...\r\n");

	cfgs_purge_uknown(wctx);

	return 0;
}

static app_command_t cfg_store_cmd_requests[] = {
	{"reset", " - reset to default all user configuration", cfgs_reset_cmd},
	{"list", " - list supported user config parameters", cfgs_list_cmd},
	{"purge", " - delete unknown user configuration", cfgs_purge_cmd},
	{"set", ":<name>:<value> - set user parameter", cfgs_set_cmd},
	{"del", ":<name> - delete user parameter", cfgs_del_cmd}
};

void sys_cfg_store_register(void)
{
	struct cfgs_context_t *ctx = NULL;

	if (!sys_cfgs_init(&ctx))
		return;

	ctx->mod.name = CFGS_MODULE;
	ctx->mod.log = sys_cfgs_log_status;
	ctx->mod.debug = sys_cfgs_debug_set;
	ctx->mod.commands.hooks = cfg_store_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(cfg_store_cmd_requests);
	ctx->mod.commands.description = "Config store";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
