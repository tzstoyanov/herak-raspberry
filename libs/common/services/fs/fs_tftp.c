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
#include "fs_internal.h"

#define TFTP_MODE	"octet"

static int tftp_dirs_create(struct fs_context_t *ctx, const char *fname)
{
	char fcreate[FS_MAX_FILE_PATH] = {0};
	int i = 0;
	int ret;

	if (fname[0] != '/')
		return 0;
	fcreate[i] = fname[i];
	i++;
	while (fname[i] && i < FS_MAX_FILE_PATH) {
		if (fname[i] == '/') {
			ret = pico_mkdir(fcreate);
			if (IS_DEBUG(ctx))
				hlog_warning(FS_MODULE, "Create directory [%s]: %s", fcreate, fs_get_err_msg(ret));
			if (ret && ret != LFS_ERR_EXIST)
				return -1;
		}
		fcreate[i] = fname[i];
		i++;
	}
	return 0;
}

static void *fs_tftp_open(const char *fname, const char *mode, u8_t is_write)
{
	struct fs_context_t *ctx = fs_context_get();
	char *local_file = NULL;
	char *tftp_file = NULL;

	if (!ctx || !fname)
		return NULL;

	if (!ctx->copy_job.started)
		return NULL;

	if (ctx->copy_job.src.peer) {
		tftp_file = ctx->copy_job.src.fname;
		local_file = ctx->copy_job.dst.fname;
		if (!is_write)
			return NULL;
	} else {
		tftp_file = ctx->copy_job.dst.fname;
		local_file = ctx->copy_job.src.fname;
		if (is_write)
			return NULL;
	}

	if (!tftp_file)
		return NULL;
	if (strlen(tftp_file) != strlen(fname))
		return NULL;
	if (strcmp(tftp_file, fname))
		return NULL;

	UNUSED(mode);
	if (is_write) {
		if (tftp_dirs_create(ctx, local_file)) {
			if (IS_DEBUG(ctx))
				hlog_warning(FS_MODULE, "Failed to create directories for [%s]", local_file);
			return NULL;
		}
		ctx->copy_job.local_fd = fs_open((char *)local_file, LFS_O_WRONLY|LFS_O_TRUNC|LFS_O_CREAT);
	} else {
		ctx->copy_job.local_fd = fs_open((char *)local_file, LFS_O_RDONLY);
	}
	if (ctx->copy_job.local_fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_warning(FS_MODULE, "Failed to open [%s]", local_file);
		return NULL;
	}
	if (IS_DEBUG(ctx))
		hlog_info(FS_MODULE, "Tftp open [%s] for %s: fd %d",
				  local_file, is_write ? "writing" : "reading", ctx->copy_job.local_fd);
	return &ctx->copy_job;
}

static void fs_tftp_close(void *handle)
{
	struct fs_file_copy_t *ctx = (struct fs_file_copy_t *)handle;

	if (!ctx)
		return;

	if (IS_DEBUG(ctx->fs_ctx)) {
		if (!ctx->started)
			hlog_warning(FS_MODULE, "Copy not running");
		else
			hlog_info(FS_MODULE, "Closing fd %d", ctx->local_fd);
	}

	hlog_info(FS_MODULE, "Completed");
	fs_cp_reset(ctx);
}

static int fs_tftp_read(void *handle, void *buf, int bytes)
{
	struct fs_file_copy_t *ctx = (struct fs_file_copy_t *)handle;
	char *fname;
	int ret;

	if (!ctx)
		return -1;
	if (!ctx->started) {
		if (IS_DEBUG(ctx->fs_ctx))
			hlog_warning(FS_MODULE, "Failed to read file, copy is not requested");
		return -1;
	}
	if (ctx->local_fd < 0) {
		fname = ctx->dst.peer ? ctx->dst.fname : ctx->src.fname;
		if (!fs_tftp_open(fname, TFTP_MODE, false)) {
			if (IS_DEBUG(ctx->fs_ctx))
				hlog_warning(FS_MODULE, "Failed to open file %s for reading", fname);
			return -1;
		}
	}

	ret = fs_read(ctx->local_fd, buf, bytes);
	if (ret <= 0) {
		if (IS_DEBUG(ctx->fs_ctx))
			hlog_warning(FS_MODULE, "Failed to read file: %d", ret);
		return -1;
	}

	if (IS_DEBUG(ctx->fs_ctx))
		hlog_info(FS_MODULE, "Read %d bytes from fd %d", ret, ctx->local_fd);

	return ret;
}

static int fs_tftp_write(void *handle, struct pbuf *p)
{
	struct fs_file_copy_t *ctx = (struct fs_file_copy_t *)handle;
	int ret, bytes = 0;
	char *fname;

	if (!ctx)
		return -1;

	if (!ctx->started) {
		if (IS_DEBUG(ctx->fs_ctx))
			hlog_warning(FS_MODULE, "Failed to write file, copy is not requested");
		return -1;
	}

	if (ctx->local_fd < 0) {
		fname = ctx->dst.peer ? ctx->dst.fname : ctx->src.fname;
		if (!fs_tftp_open(fname, TFTP_MODE, true)) {
			if (IS_DEBUG(ctx->fs_ctx))
				hlog_warning(FS_MODULE, "Failed to open file %s for reading", fname);
			return -1;
		}
	}

	while (p != NULL) {
		ret = fs_write(ctx->local_fd, p->payload, p->len);
		if (ret != p->len) {
			if (IS_DEBUG(ctx->fs_ctx))
				hlog_warning(FS_MODULE, "Failed to write file, error %d", ret);
			return -1;
		}
		bytes += ret;
		p = p->next;
	}

	if (IS_DEBUG(ctx->fs_ctx))
		hlog_info(FS_MODULE, "Wrote %d bytes to fd %d", bytes, ctx->local_fd);

	return 0;
}

#define MAX_MSG	100
static void fs_tftp_error(void *handle, int err, const char *msg, int size)
{
	struct fs_file_copy_t *ctx = (struct fs_file_copy_t *)handle;
	char message[MAX_MSG] = {0};

	if (!ctx)
		return;

	if (msg && size > 0)
		memcpy(message, msg, size < (MAX_MSG - 1) ? size : MAX_MSG - 1);
	hlog_warning(FS_MODULE, "Error processing fd %d: %d [%s]", ctx->local_fd, err, msg);
	fs_cp_reset(ctx);
}

static const struct tftp_context fs_tftp_hooks = {
	fs_tftp_open,
	fs_tftp_close,
	fs_tftp_read,
	fs_tftp_write,
	fs_tftp_error
};

const struct tftp_context *fs_tftp_hooks_get(void)
{
	return &fs_tftp_hooks;
}
