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

#define HAVE_CAT_COMMAND	1

#define FS_MODULE	"fs"

struct fs_context_t {
	sys_module_t mod;
	uint32_t debug;
};

static struct fs_context_t *__fs_context;

static struct fs_context_t *fs_context_get(void)
{
	return __fs_context;
}

static bool sys_fs_log_status(void *context)
{
	struct fs_context_t *ctx = (struct fs_context_t *)context;
	struct pico_fsstat_t stat;

	UNUSED(ctx);

	if (pico_fsstat(&stat) < 0) {
		hlog_info(FS_MODULE, "Failed to read file system status");
		return true;
	}
	hlog_info(FS_MODULE, "blocks %d, block size %d, used %d",
			  stat.block_count, stat.block_size, stat.blocks_used);
	return true;
}

static void sys_fs_debug_set(uint32_t lvl, void *context)
{
	struct fs_context_t *ctx = (struct fs_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_fs_init(struct fs_context_t **ctx)
{
	(*ctx) = (struct fs_context_t *)calloc(1, sizeof(struct fs_context_t));
	if (!(*ctx))
		return false;

	if (pico_mount(false) < 0) {
		hlog_info(FS_MODULE, "Fromatting new FS in flash.");
		if (pico_mount(true) < 0)
			goto out_err;
	}
	__fs_context = (*ctx);
	return true;

 out_err:
	free(*ctx);
	hlog_info(FS_MODULE, "Failed to init FS in flash.");
	return false;
}

static int fs_format(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;
	int ret;

	UNUSED(cmd);
	UNUSED(params);
	UNUSED(wctx);

	WEB_CLIENT_REPLY(ctx, "Formatting file system ...");

	ret = pico_unmount();
	if (!ret)
		ret = pico_mount(true);

	if (ret < 0) {
		WEB_CLIENT_REPLY(ctx, "fail\r\n");
	} else {
		WEB_CLIENT_REPLY(ctx, "success\r\n");
	}

	return 0;
}

static int fs_ls_dir(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	char *path = NULL, *rest = params;
	struct pico_fsstat_t stat;
	struct lfs_info linfo;
	int ret;
	int fd;

	UNUSED(cmd);
	UNUSED(user_data);

	if (!params || params[0] != ':' || strlen(params) < 2)
		path = "/";
	else
		path = strtok_r(rest, ":", &rest);

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(ctx->context.web.client_idx);

	if (!path) {
		hlog_info(FS_MODULE, "\tInvalid path parameter ...");
		goto out;
	}

	if (pico_fsstat(&stat) < 0) {
		hlog_info(FS_MODULE, "\tFailed to read file system status");
		goto out;
	}

	fd = pico_dir_open(path);
	if (fd < 0) {
		hlog_info(FS_MODULE, "\t[%s] directory does not exist ...", path);
		goto out;
	}

	hlog_info(FS_MODULE, "\t%s:", path);
	do {
		ret = pico_dir_read(fd, &linfo);
		if (ret == 0)
			break;
		if (ret < 0) {
			hlog_info(FS_MODULE, "\tFailed to read the directory: %d", ret);
			break;
		}
		hlog_info(FS_MODULE, "\t\t[%s] %d\t%s",
				  linfo.type == LFS_TYPE_REG ? "file" :
				  (linfo.type == LFS_TYPE_DIR ? "dir " : "uknown"),
				  linfo.type == LFS_TYPE_REG ? linfo.size : 0, linfo.name);
	} while (true);
	pico_dir_close(fd);

	hlog_info(FS_MODULE, "FS total blocks %d, block size %d, used %d",
			  stat.block_count, stat.block_size, stat.blocks_used);
out:
	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
	return 0;
}

#ifdef HAVE_CAT_COMMAND
#define BUFF_SIZE 512
static int fs_cat_file(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	char buff[BUFF_SIZE];
	char *path, *rest;
	int sz, fd = -1;

	UNUSED(cmd);
	UNUSED(user_data);

	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(ctx->context.web.client_idx);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_info(FS_MODULE, "\tInvalid path parameter ...");
		goto out;
	} else {
		path = strtok_r(params, ":", &rest);
	}
	fd = pico_open(path, LFS_O_RDONLY);
	if (fd < 0) {
		hlog_info(FS_MODULE, "\tFailed to open file [%s]: %d", path, fd);
		goto out;
	}
	sz = pico_read(fd, buff, BUFF_SIZE);
	if (fd < 0) {
		hlog_info(FS_MODULE, "\tFailed to read the file: %d", sz);
		goto out;
	}
	sz = pico_size(fd);
	hlog_info(FS_MODULE, "\t[%s] %d bytes:", path, sz);
	buff[sz] = 0;
	hlog_info(FS_MODULE, "\t\t[%s]", buff);

out:
	if (ctx->type == CMD_CTX_WEB)
		debug_log_forward(-1);
	if (fd >= 0)
		pico_close(fd);
	return 0;
}
#endif /* HAVE_CAT_COMMAND */

bool fs_is_mounted(void)
{
	if (fs_context_get())
		return true;
	return false;
}

static app_command_t fs_cmd_requests[] = {
	{"format", NULL, fs_format},
#ifdef HAVE_CAT_COMMAND
	{"cat", ":<path> - full path to a file", fs_cat_file},
#endif /* HAVE_CAT_COMMAND */
	{"ls", ":[<path>] - optional, full path to a directory", fs_ls_dir}
};

void sys_fs_register(void)
{
	struct fs_context_t *ctx = NULL;

	if (!sys_fs_init(&ctx))
		return;

	ctx->mod.name = FS_MODULE;
	ctx->mod.log = sys_fs_log_status;
	ctx->mod.debug = sys_fs_debug_set;
	ctx->mod.commands.hooks = fs_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(fs_cmd_requests);
	ctx->mod.commands.description = "File system";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
