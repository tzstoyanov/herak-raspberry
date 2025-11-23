// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/binary_info.h"
#include "lwip/apps/tftp_client.h"
#include "hardware/watchdog.h"
#include "mbedtls/sha256.h"

#include <pico_fota_bootloader/core.h>
#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define OTA_MODULE	"ota"
#define UPDATE_TIMEOUT_MS	1800000 // 30 min
#define TFTP_MODE	"octet"

#define IS_DEBUG(C)	((C) && (C)->debug)
#define DEBUG_DUMP_MS		1000
#define APPLY_DELAY_MS		2000

/* Must be multiple of 256 */
#define BUFF_SIZE		256

#define SHA_BUFF_STR	65

struct ota_update_t {
	bool in_progress;
	struct tftp_file_t file;
	mbedtls_sha256_context sha;
	char sha_verify[SHA_BUFF_STR];
	uint8_t buff[BUFF_SIZE];
	uint32_t buff_p;
	uint32_t flash_offset;
	uint64_t started;
	bool ready;
	uint64_t apply;
	int web_idx;
};

struct ota_context_t {
	sys_module_t mod;
	uint32_t debug;
	uint64_t debug_last_dump;
	struct ota_update_t update;
};

static struct ota_context_t *__ota_context;

static struct ota_context_t *ota_context_get(void)
{
	return __ota_context;
}

static void ota_reset(struct ota_update_t *update)
{
	update->in_progress = false;
	update->started = 0;
	update->buff_p = 0;
	update->ready = false;
	free(update->file.fname);
	free(update->file.peer);
	memset(update->buff, 0, BUFF_SIZE);
	memset(update->sha_verify, 0, SHA_BUFF_STR);
	memset(&(update->sha), 0, sizeof(update->sha));
	update->flash_offset = 0;
	if (!update->apply) {
		pfb_mark_download_slot_as_invalid();
		pfb_initialize_download_slot();
		sys_job_state_clear(OTA_JOB);
	}
#ifdef HAVE_SYS_WEBSERVER
	if (update->web_idx >= 0)
		webserv_client_close(update->web_idx);
#endif
	update->web_idx = -1;
}

static void sys_ota_debug_set(uint32_t lvl, void *context)
{
	struct ota_context_t *ctx = (struct ota_context_t *)context;

	ctx->debug = lvl;
}

#define TIME_STR	64
static bool sys_ota_log_status(void *context)
{
	struct ota_context_t  *ctx = (struct ota_context_t *)context;
	char time_buff[TIME_STR];
	struct tm date;
	uint64_t now;

	sys_state_log_version();

	if (ctx->update.started) {
		now = time_ms_since_boot();
		time_msec2datetime(&date, now - ctx->update.started);
		hlog_info(OTA_MODULE, "Update in progress, running %s",
				  time_date2str(time_buff, TIME_STR, &date));
		hlog_info(OTA_MODULE, "Downloading %s from %s:%d, got %d bytes ...",
				  ctx->update.file.fname, ctx->update.file.peer,
				  ctx->update.file.port, ctx->update.flash_offset);
		return true;
	}

	hlog_info(OTA_MODULE, "Ready for update");
	return true;
}

static void *ota_tftp_open(const char *fname, const char *mode, u8_t is_write)
{
	struct ota_context_t *ctx = ota_context_get();

	UNUSED(mode);

	if (!is_write || !ctx || !ctx->update.started)
		return NULL;

	hlog_info(OTA_MODULE, "Updating .... %s", fname);
	sys_job_state_set(OTA_JOB);
	pfb_mark_download_slot_as_invalid();
	pfb_initialize_download_slot();
	mbedtls_sha256_init(&ctx->update.sha);
	mbedtls_sha256_starts(&ctx->update.sha, 0);
	ctx->update.buff_p = 0;
	ctx->update.flash_offset = 0;
	memset(ctx->update.buff, 0, BUFF_SIZE);
	ctx->update.in_progress = true;

	return ctx;
}

static int ota_buff_commit(struct ota_context_t *ctx, int size)
{
	if (pfb_write_to_flash_aligned_256_bytes(ctx->update.buff, ctx->update.flash_offset, BUFF_SIZE))
		return -1;

	if (mbedtls_sha256_update(&ctx->update.sha, ctx->update.buff, size))
		return -1;

	ctx->update.flash_offset += size;
	ctx->update.buff_p = 0;
	memset(ctx->update.buff, 0, BUFF_SIZE);
	return 0;
}

static void ota_tftp_close(void *handle)
{
	struct ota_context_t *ctx = (struct ota_context_t *)handle;

	if (!ctx)
		return;
	if (!ctx->update.in_progress) {
		ota_reset(&ctx->update);
		return;
	}

	if (ctx->update.buff_p)
		ota_buff_commit(ctx, ctx->update.buff_p);
	ctx->update.ready = true;
}

static int ota_tftp_write(void *handle, struct pbuf *p)
{
	struct ota_context_t *ctx = (struct ota_context_t *)handle;
	int bsize, csize, wp = 0;
	int wsize = p->len;

	if (!ctx)
		return -1;

	if (!ctx->update.in_progress)
		ota_tftp_open(ctx->update.file.fname, TFTP_MODE, true);
	if (!ctx->update.in_progress)
		return -1;

	do {
		bsize = BUFF_SIZE - ctx->update.buff_p;
		csize = ((wsize <= bsize) ? wsize : bsize);
		memcpy(ctx->update.buff + ctx->update.buff_p, p->payload + wp, csize);
		ctx->update.buff_p += csize;
		wsize -= csize;
		wp += csize;

		if (ctx->update.buff_p == BUFF_SIZE) {
			if (ota_buff_commit(ctx, ctx->update.buff_p)) {
				hlog_warning(OTA_MODULE, "Failed to save the image chunk");
				return -1;
			}
		}
	} while (wsize > 0);

	return 0;
}

static int ota_tftp_read(void *handle, void *buff, int bytes)
{
	UNUSED(buff);
	UNUSED(bytes);
	UNUSED(handle);

	hlog_warning(OTA_MODULE, "Read not supported");
	return -1;
}

static void ota_tftp_error(void *handle, int err, const char *msg, int size)
{
	struct ota_context_t *ctx = (struct ota_context_t *)handle;

	if (!ctx)
		return;

	hlog_warning(OTA_MODULE, "Failed to get new firmware: %d [%s]",
				 err, (size > 1 && msg) ? msg : "");
	ota_reset(&ctx->update);
}

static const struct tftp_context ota_tftp = {
	ota_tftp_open,
	ota_tftp_close,
	ota_tftp_read,
	ota_tftp_write,
	ota_tftp_error
};

static int ota_validate(struct ota_context_t *ctx)
{
	char sha_buff[SHA_BUFF_STR] = {0};
	uint8_t sha[32] = {0};
	int res, ret = -1;
	int i;

	mbedtls_sha256_finish(&ctx->update.sha, sha);
	for (i = 0; i < 32; i++)
		sprintf(sha_buff+(2*i), "%02x", sha[i]);

	hlog_info(OTA_MODULE, "Got %d bytes", ctx->update.flash_offset);
	hlog_info(OTA_MODULE, "File SHA: %s", sha_buff);

	if (ctx->update.sha_verify[0]) {
		if (strncmp(ctx->update.sha_verify, sha_buff, SHA_BUFF_STR)) {
			hlog_warning(OTA_MODULE, "Invalid image");
			goto out;
		}
	}
	res = pfb_firmware_sha256_check(ctx->update.flash_offset);
	if (res) {
		hlog_warning(OTA_MODULE, "Invalid image");
	} else {
		hlog_info(OTA_MODULE, "Valid image, going to boot it ... ");
		pfb_mark_download_slot_as_valid();
		ctx->update.apply = time_ms_since_boot();
		ret = 0;
	}

out:
	ota_reset(&ctx->update);
	return ret;
}

static void sys_ota_run(void *context)
{
	struct ota_context_t *ctx = (struct ota_context_t *)context;
	uint64_t now;

	if (ctx->update.apply) {
		now = time_ms_since_boot();
		if ((now - ctx->debug_last_dump) > APPLY_DELAY_MS)
			pfb_perform_update();
		return;
	}

	if (!ctx->update.started)
		return;

	if (ctx->update.ready) {
		ota_validate(ctx);
		return;
	}

	now = time_ms_since_boot();
	if ((now - ctx->update.started) > UPDATE_TIMEOUT_MS) {
		hlog_info(OTA_MODULE, "Timeout reading file %s from server %s:%d.",
				  ctx->update.file.fname, ctx->update.file.peer, ctx->update.file.port);
		ota_reset(&ctx->update);
	}
	if ((now - ctx->debug_last_dump) > DEBUG_DUMP_MS) {
		ctx->debug_last_dump = now;
		hlog_info(OTA_MODULE, "Updating %s from %s: %d bytes",
				  ctx->update.file.fname, ctx->update.file.peer, ctx->update.flash_offset);
	}
}

static int ota_udate_start_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;

	UNUSED(cmd);

	if (wctx->update.started) {
		hlog_warning(OTA_MODULE, "Update is running already.");
		return -1;
	}

	if (!params || strlen(params) < 1 || params[0] != ':')
		goto out_err;
	if (tftp_url_parse(params + 1, &(wctx->update.file)))
		goto out_err;

	if (tftp_file_get(&ota_tftp, &(wctx->update.file), wctx))
		goto out_err;

	wctx->update.started = time_ms_since_boot();
	WEBCTX_SET_KEEP_OPEN(ctx, true);
	WEBCTX_SET_KEEP_SILENT(ctx, true);
	wctx->update.web_idx = WEB_CLIENT_GET(ctx);

	if (IS_DEBUG(wctx))
		hlog_info(OTA_MODULE, "Starting update %s from %s:%d",
				  wctx->update.file.fname, wctx->update.file.peer, wctx->update.file.port);

	return 0;

out_err:
	hlog_warning(OTA_MODULE, "Wrong parameters");
	ota_reset(&wctx->update);
	return -1;
}

static int ota_udate_cancel_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);

	if (IS_DEBUG(wctx) && wctx->update.started)
		hlog_info(OTA_MODULE, "Cancel update %s from %s:%d",
				  wctx->update.file.fname, wctx->update.file.peer, wctx->update.file.port);
	ota_reset(&wctx->update);
	wctx->update.apply = false;
	return 0;
}

static app_command_t ota_cmd_requests[] = {
	{"update", ":tftp://<server>[:<port>]/<file> - Update the firmware using <file> from tftp <server> on [port]",
	  ota_udate_start_cmd},
	{"cancel", " - Cancel update in progress", ota_udate_cancel_cmd},
};

void sys_ota_register(void)
{
	struct ota_context_t  *ctx = NULL;

	ctx = calloc(1, sizeof(struct ota_context_t));

	__ota_context = ctx;
	ctx->mod.name = OTA_MODULE;
	ctx->mod.run = sys_ota_run;
	ctx->mod.log = sys_ota_log_status;
	ctx->mod.debug = sys_ota_debug_set;
	ctx->mod.job_flags = OTA_JOB;
	ctx->mod.commands.hooks = ota_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(ota_cmd_requests);
	ctx->mod.commands.description = "OTA update";
	ctx->mod.context = ctx;

	ota_reset(&ctx->update);

	sys_module_register(&ctx->mod);
}
