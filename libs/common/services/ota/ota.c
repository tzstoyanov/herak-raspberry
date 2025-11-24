// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include <pico_fota_bootloader/core.h>
#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"
#include "ota_internal.h"

#define UPDATE_TIMEOUT_MS	300000 // 5 min

#define DEBUG_DUMP_MS		1000
#define APPLY_DELAY_MS		2000

static struct ota_context_t *__ota_context;

struct ota_update_t *ota_update_context_get(void)
{
	if (__ota_context)
		return &(__ota_context->update);
	return NULL;
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

static void sys_ota_run(void *context)
{
	struct ota_context_t *ctx = (struct ota_context_t *)context;
	uint64_t now;

	if (ctx->update.apply) {
		now = time_ms_since_boot();
		if ((now - ctx->update.apply) > APPLY_DELAY_MS)
			pfb_perform_update();
		return;
	}

	if (!ctx->update.started)
		return;

	if (ctx->update.ready) {
		ota_update_validate(&ctx->update);
		return;
	}

	now = time_ms_since_boot();
	if ((now - ctx->update.started) > UPDATE_TIMEOUT_MS) {
		hlog_info(OTA_MODULE, "Timeout reading file %s from server %s:%d.",
				  ctx->update.file.fname, ctx->update.file.peer, ctx->update.file.port);
		ota_update_reset(&ctx->update);
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
	if (ota_update_start(&(wctx->update)))
		goto out_err;

	WEBCTX_SET_KEEP_OPEN(ctx, true);
	WEBCTX_SET_KEEP_SILENT(ctx, true);
	wctx->update.web_idx = WEB_CLIENT_GET(ctx);

	if (IS_DEBUG(wctx))
		hlog_info(OTA_MODULE, "Starting update %s from %s:%d",
				  wctx->update.file.fname, wctx->update.file.peer, wctx->update.file.port);

	return 0;

out_err:
	hlog_warning(OTA_MODULE, "Wrong parameters");
	ota_update_reset(&wctx->update);
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
	ota_update_reset(&wctx->update);
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
	if (!ctx)
		return;

	ctx->update.ota = ctx;
	ctx->mod.name = OTA_MODULE;
	ctx->mod.run = sys_ota_run;
	ctx->mod.log = sys_ota_log_status;
	ctx->mod.debug = sys_ota_debug_set;
	ctx->mod.job_flags = OTA_JOB;
	ctx->mod.commands.hooks = ota_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(ota_cmd_requests);
	ctx->mod.commands.description = "OTA update";
	ctx->mod.context = ctx;
	__ota_context = ctx;
	ota_update_reset(&ctx->update);

	sys_module_register(&ctx->mod);
}
