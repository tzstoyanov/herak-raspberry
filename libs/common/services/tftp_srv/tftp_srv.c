// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico_hal.h"
#include "lwip/apps/tftp_server.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define TFTP_SRV_MODULE "tftp"
#define MAX_OPENED_FILES	5
#define MAX_FILE_PATH		LFS_NAME_MAX

#define IS_DEBUG(C)	((C) && (C)->debug)

struct tftp_srv_context_t {
	sys_module_t mod;
	uint32_t debug;
	int files[MAX_OPENED_FILES];
};

static struct tftp_srv_context_t *__tftp_srv_context;

static struct tftp_srv_context_t *tftp_srv_context_get(void)
{
	return __tftp_srv_context;
}

static bool sys_tftp_srv_log_status(void *context)
{
	struct tftp_srv_context_t *ctx = (struct tftp_srv_context_t *)context;
	int i, cnt = 0;

	for (i = 0; i < MAX_OPENED_FILES; i++)
		if (ctx->files[i] >= 0)
			cnt++;

	hlog_info(TFTP_SRV_MODULE, "TFTP Server is running at port %d, opened files %d",
			  TFTP_PORT, cnt);

	return true;
}

static void sys_tftp_srv_debug_set(uint32_t lvl, void *context)
{
	struct tftp_srv_context_t *ctx = (struct tftp_srv_context_t *)context;

	ctx->debug = lvl;
}

static int tftp_dirs_create(struct tftp_srv_context_t *ctx, const char *fname)
{
	char fcreate[MAX_FILE_PATH] = {0};
	int i = 0;
	int ret;

	if (fname[0] != '/')
		return 0;
	fcreate[i] = fname[i];
	i++;
	while (fname[i] && i < MAX_FILE_PATH) {
		if (fname[i] == '/') {
			ret = pico_mkdir(fcreate);
			if (IS_DEBUG(ctx))
				hlog_warning(TFTP_SRV_MODULE, "Create directory [%s]: %d", fcreate, ret);
			if (ret && ret != LFS_ERR_EXIST)
				return -1;
		}
		fcreate[i] = fname[i];
		i++;
	}
	return 0;
}

static void *tftp_open(const char *fname, const char *mode, u8_t is_write)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int i;

	UNUSED(mode);

	if (!ctx || !fname || strlen(fname) >= MAX_FILE_PATH)
		return NULL;

	for (i = 0; i < MAX_OPENED_FILES; i++)
		if (ctx->files[i] < 0)
			break;
	if (i >= MAX_OPENED_FILES) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to open [%s]: too many opened files", fname);
		return NULL;
	}

	if (is_write) {
		if (tftp_dirs_create(ctx, fname)) {
			if (IS_DEBUG(ctx))
				hlog_warning(TFTP_SRV_MODULE, "Failed to create directories for [%s]", fname);
			return NULL;
		}
		ctx->files[i] = pico_open(fname, LFS_O_WRONLY|LFS_O_TRUNC|LFS_O_CREAT);
	} else {
		ctx->files[i] = pico_open(fname, LFS_O_RDONLY);
	}
	if (ctx->files[i] < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to open [%s]: error %d", fname, ctx->files[i]);
		return NULL;
	}
	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Opened [%s] for %s: fd %d",
				  fname, is_write ? "writing" : "reading", ctx->files[i]);
	return (void *)ctx->files[i];
}

static int tftp_get_fd(void *handle)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = (int) handle;
	int i;

	if (!ctx || fd < 0)
		return -1;
	for (i = 0; i < MAX_OPENED_FILES; i++)
		if (ctx->files[i] == fd)
			return fd;
	return -1;
}

static void tftp_close(void *handle)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = tftp_get_fd(handle);
	int i;

	if (!ctx || fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to close file, invalid fd %d", fd);
		return;
	}

	pico_close(fd);

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Closing fd %d", fd);


	for (i = 0; i < MAX_OPENED_FILES; i++) {
		if (ctx->files[i] == fd) {
			ctx->files[i] = -1;
			break;
		}
	}
}

static void tftp_close_all(struct tftp_srv_context_t *ctx)
{
	int i;

	for (i = 0; i < MAX_OPENED_FILES; i++) {
		if (ctx->files[i] >= 0) {
			pico_close(ctx->files[i]);
			if (IS_DEBUG(ctx))
				hlog_info(TFTP_SRV_MODULE, "Closing fd %d", ctx->files[i]);
			ctx->files[i] = -1;
		}
	}
}

static int tftp_read(void *handle, void *buf, int bytes)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = tftp_get_fd(handle);
	int ret;

	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to read file, invalid fd %d", fd);
		return -1;
	}

	ret = pico_read(fd, buf, bytes);
	if (ret <= 0)
		return -1;

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Read %d bytes from fd %d", ret, fd);

	return ret;
}

static int tftp_write(void *handle, struct pbuf *p)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = tftp_get_fd(handle);
	int ret, bytes = 0;

	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to write file, invalid fd %d", fd);
		return -1;
	}

	while (p != NULL) {
		ret = pico_write(fd, p->payload, p->len);
		if (ret != p->len) {
			if (IS_DEBUG(ctx))
				hlog_warning(TFTP_SRV_MODULE, "Failed to write file, error %d", ret);
			return -1;
		}
		bytes += ret;
		p = p->next;
	}

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Wrote %d bytes to fd %d", bytes, fd);

	return 0;
}

#define MAX_MSG	100
static void tftp_error(void *handle, int err, const char *msg, int size)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = (int) handle;
	int i;

	if (!ctx)
		return;

	if (fd >= 0) {
		for (i = 0; i < MAX_OPENED_FILES; i++) {
			if (ctx->files[i] == fd) {
				pico_close(ctx->files[i]);
				ctx->files[i] = -1;
				break;
			}
		}
	}

	if (IS_DEBUG(ctx)) {
		char message[MAX_MSG] = {0};

		if (msg && size > 0)
			memcpy(message, msg, size < (MAX_MSG - 1) ? size : MAX_MSG - 1);

		hlog_warning(TFTP_SRV_MODULE, "Error processing fd %d: %d [%s]", fd, err, msg);
	}
}

static bool sys_tftp_srv_init(struct tftp_srv_context_t **ctx)
{
	static const struct tftp_context tftp_hooks = {
		tftp_open,
		tftp_close,
		tftp_read,
		tftp_write,
		tftp_error
	};
	int i;

	if (!fs_is_mounted())
		return false;

	(*ctx) = (struct tftp_srv_context_t *)calloc(1, sizeof(struct tftp_srv_context_t));
	if (!(*ctx))
		return false;
	if (tftp_init_server(&tftp_hooks) != ERR_OK)
		goto out_err;
	for (i = 0; i < MAX_OPENED_FILES; i++)
		(*ctx)->files[i] = -1;
	__tftp_srv_context = *ctx;

	return true;
out_err:
	free(*ctx);
	(*ctx) = NULL;
	return false;
}

static int tftp_srv_close_all_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct tftp_srv_context_t *wctx = (struct tftp_srv_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(params);

	WEB_CLIENT_REPLY(ctx, "Close all opened files\r\n");
	tftp_close_all(wctx);
	return 0;
}

static app_command_t tftp_srv_cmd_requests[] = {
	{"close_all", " - close all opened files", tftp_srv_close_all_cmd},
};

void sys_tftp_srv_register(void)
{
	struct tftp_srv_context_t *ctx = NULL;

	if (!sys_tftp_srv_init(&ctx))
		return;

	ctx->mod.name = TFTP_SRV_MODULE;
	ctx->mod.log = sys_tftp_srv_log_status;
	ctx->mod.debug = sys_tftp_srv_debug_set;
	ctx->mod.commands.hooks = tftp_srv_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(tftp_srv_cmd_requests);
	ctx->mod.commands.description = "TFTP Server";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

