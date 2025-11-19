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
#include "fs_internal.h"

#define HAVE_CAT_COMMAND	1

static __in_flash() struct {
	enum lfs_error err;
	char *desc;
} fs_error_msg[] = {
	{ LFS_ERR_OK, "ok" },
	{ LFS_ERR_IO, " Error during device operation" },
	{ LFS_ERR_CORRUPT, "Corrupted" },
	{ LFS_ERR_NOENT, "No directory entry" },
	{ LFS_ERR_EXIST, "Entry already exists" },
	{ LFS_ERR_NOTDIR, "Entry is not a dir" },
	{ LFS_ERR_ISDIR, "Entry is a dir" },
	{ LFS_ERR_NOTEMPTY, "Dir is not empty" },
	{ LFS_ERR_BADF, "Bad file number" },
	{ LFS_ERR_FBIG, "File too large" },
	{ LFS_ERR_INVAL, "Invalid parameter" },
	{ LFS_ERR_NOSPC, "No space left on device" },
	{ LFS_ERR_NOMEM, "No more memory available" },
	{ LFS_ERR_NOATTR, "No data/attr available" },
	{ LFS_ERR_NAMETOOLONG, "File name too long" }
};

static struct fs_context_t *__fs_context;

struct fs_context_t *fs_context_get(void)
{
	return __fs_context;
}

static bool sys_fs_log_status(void *context)
{
	struct fs_context_t *ctx = (struct fs_context_t *)context;
	struct pico_fsstat_t stat;
	int i, cnt = 0;

	UNUSED(ctx);

	if (pico_fsstat(&stat) < 0) {
		hlog_info(FS_MODULE, "Failed to read file system status");
		return true;
	}
	for (i = 0; i < MAX_OPENED_FILES; i++) {
		if (ctx->open_fd[i] >= 0)
			cnt++;
	}
	hlog_info(FS_MODULE, "blocks %d, block size %d, used %d, opened files %d",
			  stat.block_count, stat.block_size, stat.blocks_used, cnt);
	return true;
}

static void sys_fs_debug_set(uint32_t lvl, void *context)
{
	struct fs_context_t *ctx = (struct fs_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_fs_init(struct fs_context_t **ctx)
{
	int i;

	(*ctx) = (struct fs_context_t *)calloc(1, sizeof(struct fs_context_t));
	if (!(*ctx))
		return false;

	if (pico_mount(false) < 0) {
		hlog_info(FS_MODULE, "Fromatting new FS in flash.");
		if (pico_mount(true) < 0)
			goto out_err;
	}
	(*ctx)->copy_job.local_fd = -1;
	(*ctx)->copy_job.web_idx = -1;
	for (i = 0; i < MAX_OPENED_FILES; i++)
		(*ctx)->open_fd[i] = -1;

	__fs_context = (*ctx);
	return true;

 out_err:
	free(*ctx);
	hlog_info(FS_MODULE, "Failed to init FS in flash.");
	return false;
}

static void fs_close_all(struct fs_context_t *ctx)
{
	int i;

	for (i = 0; i < MAX_OPENED_FILES; i++) {
		if (ctx->open_fd[i] >= 0) {
			pico_close(ctx->open_fd[i]);
			if (IS_DEBUG(ctx))
				hlog_info(FS_MODULE, "Closing fd %d", ctx->open_fd[i]);
			ctx->open_fd[i] = -1;
		}
	}
}

static int fs_format(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;
	int ret;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);

	hlog_info(FS_MODULE, "Formatting file system ...");

	ret = pico_unmount();
	if (!ret)
		ret = pico_mount(true);

	if (IS_DEBUG(wctx))
		hlog_info(FS_MODULE, "\tFormatted new FS: [%s]", fs_get_err_msg(ret));

	return 0;
}

static int fs_rm_path(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;
	char *path, *rest;
	int ret;

	UNUSED(cmd);
	UNUSED(ctx);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_info(FS_MODULE, "\tInvalid path parameter.");
		goto out;
	} else {
		path = strtok_r(params, ":", &rest);
	}
	ret = pico_remove(path);
	if (ret < 0)
		hlog_info(FS_MODULE, "\tDeletion of [%s] failed with [%s]", path, fs_get_err_msg(ret));

	if (IS_DEBUG(wctx))
		hlog_info(FS_MODULE, "\tDeleting [%s]: [%s]", path, fs_get_err_msg(ret));
out:
	return 0;
}

void fs_cp_reset(struct fs_file_copy_t *copy)
{
	if (!copy)
		return;

	free(copy->src.fname);
	free(copy->src.peer);
	free(copy->dst.fname);
	free(copy->dst.peer);
	memset(&copy->src, 0, sizeof(copy->src));
	memset(&copy->dst, 0, sizeof(copy->dst));
	if (copy->local_fd >= 0)
		fs_close(copy->local_fd);
	copy->local_fd = -1;
	if (copy->web_idx >= 0)
		webserv_client_close(copy->web_idx);
	copy->web_idx = -1;
	copy->started = 0;
}

static int fs_cp_params_parse(char *params, struct fs_file_copy_t *copy)
{
	char *src = NULL;
	char *dst = NULL;
	unsigned int i;
	int fd;

	fs_cp_reset(copy);
	src = strtok_r(params, "?", &dst);
	if (!src || !dst)
		return -1;

	if (src[0] == '/') {
		fd = pico_open(src, LFS_O_RDONLY);
		if (fd < 0)
			goto out_err;
		pico_close(fd);
		copy->src.fname = strdup(src);
	} else if (tftp_url_parse(src, &(copy->src))) {
		goto out_err;
	}

	tftp_url_parse(dst, &(copy->dst));

	if (!copy->src.fname)
		goto out_err;
	if (copy->src.peer && copy->dst.peer)
		goto out_err;

	if (!copy->dst.fname || strlen(copy->dst.fname) < 1 ||
		 copy->dst.fname[strlen(copy->dst.fname) - 1] == '/') {
			i = strlen(copy->src.fname);
			while (i) {
				if (copy->src.fname[i] == '/')
					break;
				i--;
			}
			if (i == strlen(copy->src.fname))
				goto out_err;
			src = copy->src.fname + i + 1;
			if (src[0] == '/')
				src++;
			sys_asprintf(&dst, "%s%s", copy->dst.fname ? copy->dst.fname : "/", src);
			free(copy->dst.fname);
			copy->dst.fname = dst;
	}
	if (!copy->dst.fname)
		goto out_err;

	return 0;

out_err:
	return -1;
}

#define COPY_BUFF	64
static int fs_cp_local(char *src, char *dst)
{
	char buff[COPY_BUFF];
	int sfd = -1;
	int dfd = -1;
	int ret = -1;
	int c;

	sfd = fs_open(src, LFS_O_RDONLY);
	if (sfd < 0)
		goto out;

	dfd = fs_open(dst, LFS_O_WRONLY|LFS_O_TRUNC|LFS_O_CREAT);
	if (dfd < 0)
		goto out;

	do {
		c = fs_read(sfd, buff, COPY_BUFF);
		if (c > 0) {
			ret = fs_write(dfd, buff, c);
			if (ret != c)
				goto out;
		}
	} while (c > 0);

	if (!c)
		ret = 0;
out:
	if (sfd >= 0)
		fs_close(sfd);
	if (dfd >= 0)
		fs_close(dfd);
	if (ret < 0)
		pico_remove(dst);
	return ret;
}

static int fs_mv_file(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;
	struct fs_file_copy_t copy;
	int ret = -1;

	UNUSED(ctx);
	UNUSED(cmd);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_warning(FS_MODULE, "\tMissing parameters.");
		return -1;
	}
	memset(&copy, 0, sizeof(copy));
	copy.web_idx = -1;
	copy.local_fd = -1;
	if (fs_cp_params_parse(params + 1, &copy)) {
		hlog_warning(FS_MODULE, "\tInvalid parameters.");
		goto out;
	}
	if (!copy.src.fname || !copy.dst.fname) {
		hlog_warning(FS_MODULE, "\tNo files are specified.");
		goto out;
	}

	if (copy.src.peer || copy.dst.peer) {
		hlog_warning(FS_MODULE, "\tOnly local files can be moved.");
		goto out;
	}

	ret = pico_rename(copy.src.fname, copy.dst.fname);
	if (ret != LFS_ERR_OK) {
		hlog_warning(FS_MODULE, "\tFailed to move files: %s", fs_get_err_msg(ret));
		ret = -1;
		goto out;
	}
	hlog_info(FS_MODULE, "Completed");
	if (IS_DEBUG(wctx))
		hlog_info(FS_MODULE, "Moved %s to %s", copy.src.fname, copy.dst.fname);

	ret = 0;
out:
	free(copy.dst.fname);
	free(copy.dst.peer);
	free(copy.src.fname);
	free(copy.src.peer);
	return ret;
}

static int fs_cp_file(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;
	int ret = -1;

	UNUSED(cmd);

	if (wctx->copy_job.started) {
		hlog_warning(FS_MODULE, "\tAnother copy is running already.");
		goto out_close;
	}
	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_warning(FS_MODULE, "\tMissing parameters.");
		goto out_close;
	}
	if (fs_cp_params_parse(params + 1, &(wctx->copy_job))) {
		hlog_warning(FS_MODULE, "\tInvalid parameters.");
		goto out_close;
	}

	wctx->copy_job.started = time_ms_since_boot();
	if (wctx->copy_job.dst.peer) {
		// local -> remote
		if (tftp_file_put(fs_tftp_hooks_get(), &wctx->copy_job.dst, &wctx->copy_job)) {
			hlog_warning(FS_MODULE, "\tFail to get file from tftp");
			goto out_close;
		}
		goto out_open;
	}

	if (wctx->copy_job.src.peer) {
		// remote -> local
		if (tftp_file_get(fs_tftp_hooks_get(), &wctx->copy_job.src, &wctx->copy_job)) {
			hlog_warning(FS_MODULE, "\tFail to put file to tftp");
			goto out_close;
		}
		goto out_open;
	}

	// local -> local
	if (fs_cp_local(wctx->copy_job.src.fname, wctx->copy_job.dst.fname)) {
		hlog_warning(FS_MODULE, "\tCopy %s to %s failed.",
					 wctx->copy_job.src.fname, wctx->copy_job.dst.fname);
		goto out_close;
	}
	hlog_info(FS_MODULE, "Completed");
	ret = 0;
out_close:
	fs_cp_reset(&(wctx->copy_job));
	return ret;

out_open:
	WEBCTX_SET_KEEP_OPEN(ctx, true);
	WEBCTX_SET_KEEP_SILENT(ctx, true);
	wctx->copy_job.web_idx = WEB_CLIENT_GET(ctx);
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
	UNUSED(ctx);
	UNUSED(user_data);

	if (!params || params[0] != ':' || strlen(params) < 2)
		path = "/";
	else
		path = strtok_r(rest, ":", &rest);

	if (!path) {
		hlog_info(FS_MODULE, "\tInvalid path parameter.");
		goto out;
	}

	ret = pico_fsstat(&stat);
	if (ret < 0) {
		hlog_info(FS_MODULE, "\tFailed to read file system status: %s");
		goto out;
	}

	fd = pico_dir_open(path);
	if (fd < 0) {
		hlog_info(FS_MODULE, "\t[%s] directory does not exist.", path);
		goto out;
	}

	hlog_info(FS_MODULE, "\t%s:", path);
	do {
		ret = pico_dir_read(fd, &linfo);
		if (ret == 0)
			break;
		if (ret < 0) {
			hlog_info(FS_MODULE, "\tFailed to read the directory: [%s]", fs_get_err_msg(ret));
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
	return 0;
}

static int fs_close_all_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct fs_context_t *wctx = (struct fs_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	UNUSED(params);

	hlog_info(FS_MODULE, "Close all opened files");
	fs_close_all(wctx);
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
	UNUSED(ctx);
	UNUSED(user_data);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_info(FS_MODULE, "\tInvalid path parameter.");
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
	hlog_info(FS_MODULE, "%s", buff);

out:
	if (fd >= 0)
		pico_close(fd);
	return 0;
}
#endif /* HAVE_CAT_COMMAND */

/***************** API *****************/

bool fs_is_mounted(void)
{
	if (fs_context_get())
		return true;
	return false;
}

int fs_get_files_count(char *dir_path, char *ext)
{
	struct fs_context_t *ctx = fs_context_get();
	struct lfs_info linfo;
	int slen, elen;
	int count = 0;
	int fd;

	if (!ctx)
		return -1;

	fd = pico_dir_open(dir_path);
	if (fd < 0)
		return -1;

	do {
		if (pico_dir_read(fd, &linfo) <= 0)
			break;
		if (linfo.type != LFS_TYPE_REG)
			continue;
		if (ext) {
			slen = strlen(linfo.name);
			elen = strlen(ext);
			if (elen >= slen)
				continue;
			slen -= elen;
			if (strncmp(linfo.name + slen, ext, elen))
				continue;
		}
		count++;
	} while (true);
	pico_dir_close(fd);

	return count;
}

#define FS_UKNOWN_STR	32
char *fs_get_err_msg(int err)
{
	int err_count = ARRAY_SIZE(fs_error_msg);
	static char unkown[FS_UKNOWN_STR];
	int i;

	for (i = 0; i < err_count; i++) {
		if (err == fs_error_msg[i].err)
			return fs_error_msg[i].desc;
	}

	snprintf(unkown, FS_UKNOWN_STR, "error %d", err);
	return unkown;
}

int fs_open(char *path, enum lfs_open_flags flags)
{
	struct fs_context_t *ctx = fs_context_get();
	int fd, i;

	if (!ctx)
		return -1;

	for (i = 0; i < MAX_OPENED_FILES; i++) {
		if (ctx->open_fd[i] < 0)
			break;
	}
	if (i >= MAX_OPENED_FILES) {
		if (IS_DEBUG(ctx))
			hlog_info(FS_MODULE, "Fail to open [%s]: too many opened files");
		return -1;
	}

	fd = pico_open(path, flags);
	if (fd < 0) {
		if (IS_DEBUG(ctx))
			hlog_info(FS_MODULE, "Fail to open [%s]: [%s]", path, fs_get_err_msg(fd));
		return -1;
	}
	ctx->open_fd[i] = fd;
	if (IS_DEBUG(ctx))
		hlog_info(FS_MODULE, "Open file [%s]: %d %d", path, fd, i);

	return i;
}

void fs_close(int fd)
{
	struct fs_context_t *ctx = fs_context_get();
	int ret;

	if (!ctx)
		return;

	if (fd < 0 || fd >= MAX_OPENED_FILES || ctx->open_fd[fd] == -1) {
		if (IS_DEBUG(ctx))
			hlog_info(FS_MODULE, "Cannot close [%d]: invalid descriptor", fd);
		return;
	}
	ret = pico_close(ctx->open_fd[fd]);
	if (IS_DEBUG(ctx))
		hlog_info(FS_MODULE, "Close %d %d: [%s]", ctx->open_fd[fd], fd, fs_get_err_msg(ret));
	ctx->open_fd[fd] = -1;
}

static int fs_read_check(int fd, char *buff, int buff_size, char *stops, int count_stops)
{
	struct fs_context_t *ctx = fs_context_get();
	int count = 0;
	char byte;
	int i, ret;

	if (!ctx)
		return -1;

	if (fd < 0 || fd >= MAX_OPENED_FILES || ctx->open_fd[fd] == -1) {
		if (IS_DEBUG(ctx))
			hlog_info(FS_MODULE, "Cannot read [%d]: invalid descriptor", fd);
		return -1;
	}

	if (stops && count_stops > 0) {
		do {
			ret = pico_read(ctx->open_fd[fd], &byte, 1);
			if (ret != 1) {
				if (ret <= 0)
					count = -1;
				break;
			}
			for (i = 0; i < count_stops; i++) {
				if (byte == stops[i])
					break;
			}
			if (i < count_stops)
				break;
			buff[count++] = byte;
			if (count >= buff_size)
				break;
		} while (true);
	} else {
		ret = pico_read(ctx->open_fd[fd], buff, buff_size);
		if (ret < 0)
			count = -1;
		else
			count = ret;
	}
	if (IS_DEBUG(ctx))
		hlog_info(FS_MODULE, "Read %d bytes from %d: %s", count, fd,
				  count < 0 ? fs_get_err_msg(ret) : fs_get_err_msg(LFS_ERR_OK));

	return count;
}

int fs_gets(int fd, char *buff, int buff_size)
{
	char new_line[] = {'\n', '\r'};
	int ret;

	buff[0] = 0;
	ret = fs_read_check(fd, buff, buff_size, new_line, ARRAY_SIZE(new_line));
	if (ret > 0 && ret < buff_size)
		buff[ret] = 0;
	else
		buff[buff_size - 1] = 0;

	return ret;
}

int fs_read(int fd, char *buff, int buff_size)
{
	return fs_read_check(fd, buff, buff_size, NULL, 0);
}

int fs_write(int fd, char *buff, int buff_size)
{
	struct fs_context_t *ctx = fs_context_get();
	int ret;

	if (!ctx)
		return -1;

	if (fd < 0 || fd >= MAX_OPENED_FILES || ctx->open_fd[fd] == -1) {
		if (IS_DEBUG(ctx))
			hlog_info(FS_MODULE, "Cannot read [%d]: invalid descriptor", fd);
		return -1;
	}

	ret = pico_write(ctx->open_fd[fd], buff, buff_size);

	if (IS_DEBUG(ctx))
		hlog_info(FS_MODULE, "Write %d bytes to %d: %d %s", buff_size, fd, ret,
				  ret < 0 ? fs_get_err_msg(ret) : fs_get_err_msg(LFS_ERR_OK));
	return ret;
}

static app_command_t fs_cmd_requests[] = {
	{"format", " - format the file system", fs_format},
#ifdef HAVE_CAT_COMMAND
	{"cat", ":<path> - full path to a file", fs_cat_file},
#endif /* HAVE_CAT_COMMAND */
	{"ls", ":[<path>] - optional, full path to a directory", fs_ls_dir},
	{"rm", ":<path> - delete file or directory (the directory must be empty)", fs_rm_path},
	{"cp", ":<src>?<dst> - copy file, src and dst can be local or tftp files", fs_cp_file},
	{"mv", ":<src>?<dst> - move file", fs_mv_file},
	{"close_all", " - close all opened files", fs_close_all_cmd},
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
