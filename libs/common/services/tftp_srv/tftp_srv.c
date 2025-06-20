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

#define TFTP_SRV_MODULE		"tftp"
#define MAX_FILE_PATH		LFS_NAME_MAX

#define IS_DEBUG(C)	((C) && (C)->debug)

struct tftp_srv_context_t {
	sys_module_t mod;
	uint32_t debug;
};

static struct tftp_srv_context_t *__tftp_srv_context;

static struct tftp_srv_context_t *tftp_srv_context_get(void)
{
	return __tftp_srv_context;
}

static bool sys_tftp_srv_log_status(void *context)
{
	struct tftp_srv_context_t *ctx = (struct tftp_srv_context_t *)context;

	UNUSED(ctx);
	hlog_info(TFTP_SRV_MODULE, "TFTP Server is running at port %d", TFTP_PORT);

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
				hlog_warning(TFTP_SRV_MODULE, "Create directory [%s]: %s", fcreate, fs_get_err_msg(ret));
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
	int fd;

	UNUSED(mode);
	if (is_write) {
		if (tftp_dirs_create(ctx, fname)) {
			if (IS_DEBUG(ctx))
				hlog_warning(TFTP_SRV_MODULE, "Failed to create directories for [%s]", fname);
			return NULL;
		}
		fd = fs_open((char *)fname, LFS_O_WRONLY|LFS_O_TRUNC|LFS_O_CREAT);
	} else {
		fd = fs_open((char *)fname, LFS_O_RDONLY);
	}
	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to open [%s]", fname);
		return NULL;
	}
	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Opened [%s] for %s: fd %d",
				  fname, is_write ? "writing" : "reading", fd);
	return (void *)fd;
}

static void tftp_close(void *handle)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = (int)handle;

	if (!ctx || fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to close file, invalid fd %d", fd);
		return;
	}

	fs_close(fd);

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Closing fd %d", fd);
}

static int tftp_read(void *handle, void *buf, int bytes)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = (int)handle;
	int ret;

	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to read file, invalid fd %d", fd);
		return -1;
	}

	ret = fs_read(fd, buf, bytes);
	if (ret <= 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to read file: %d", ret);
		return -1;
	}

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_SRV_MODULE, "Read %d bytes from fd %d", ret, fd);

	return ret;
}

static int tftp_write(void *handle, struct pbuf *p)
{
	struct tftp_srv_context_t *ctx = tftp_srv_context_get();
	int fd = (int)handle;
	int ret, bytes = 0;

	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(TFTP_SRV_MODULE, "Failed to write file, invalid fd %d", fd);
		return -1;
	}

	while (p != NULL) {
		ret = fs_write(fd, p->payload, p->len);
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

	if (!ctx)
		return;

	fs_close(fd);

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

	if (!fs_is_mounted())
		return false;

	(*ctx) = (struct tftp_srv_context_t *)calloc(1, sizeof(struct tftp_srv_context_t));
	if (!(*ctx))
		return false;
	if (tftp_init_server(&tftp_hooks) != ERR_OK)
		goto out_err;
	__tftp_srv_context = *ctx;

	return true;
out_err:
	free(*ctx);
	(*ctx) = NULL;
	return false;
}

void sys_tftp_srv_register(void)
{
	struct tftp_srv_context_t *ctx = NULL;

	if (!sys_tftp_srv_init(&ctx))
		return;

	ctx->mod.name = TFTP_SRV_MODULE;
	ctx->mod.log = sys_tftp_srv_log_status;
	ctx->mod.debug = sys_tftp_srv_debug_set;
	ctx->mod.commands.description = "TFTP Server";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

