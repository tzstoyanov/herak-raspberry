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

#define TIME_STR	64
#define APPLY_RETRIES	3

static struct ota_context_t *__ota_context;

struct ota_update_t *ota_update_context_get(void)
{
	if (__ota_context)
		return &(__ota_context->update);
	return NULL;
}

struct ota_check_t *ota_check_context_get(void)
{
	if (__ota_context)
		return &(__ota_context->check);
	return NULL;
}

static void sys_ota_debug_set(uint32_t lvl, void *context)
{
	struct ota_context_t *ctx = (struct ota_context_t *)context;

	ctx->debug = lvl;
}

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
	ota_check_log(&ctx->check);
	hlog_info(OTA_MODULE, "Ready for update");
	return true;
}

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int ota_mqtt_send(struct ota_context_t *ctx)
{
	char *name = NULL, *ver = NULL, *commit = NULL;
	char *date = NULL, *time = NULL, *peer = NULL;
	int len = OTA_MQTT_DATA_LEN;
	char time_buff[TIME_STR];
	int count = 0;
	int ret = -1;

	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"current_version\": \"%s %s\"", SYS_VERSION_STR, SYS_BUILD_DATE);
		if (ota_update_get_new(&ctx->check, &name, &ver, &commit, &date, &time, &peer)) {
			ADD_MQTT_MSG_VAR(",\"update\": \"%d\"", 0);
		} else {
			ADD_MQTT_MSG_VAR(",\"update\": \"%d\"", 1);
			ADD_MQTT_MSG_VAR(",\"new_version\": \"%s %s-%s %s-%s from %s\"",
							 name ? name : "N/A", ver ? ver : "N/A", commit ? commit : "N/A",
							 date ? date : "N/A", time ? time : "N/A", peer ? peer : "N/A");
		}
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[OTA_MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&(ctx->mqtt_comp[0]), ctx->mqtt_payload);

	return ret;
}

static void sys_ota_run(void *context)
{
	struct ota_context_t *ctx = (struct ota_context_t *)context;
	uint64_t now;

	if (ctx->update.started || ctx->update.apply) {
		ota_update_run(&ctx->update);
		return;
	}
	if (ctx->check.started) {
		ota_check_run(&ctx->check);
		return;
	}

	now = time_ms_since_boot();
	if (ctx->mqtt_comp[0].force || now - ctx->mqtt_last_send > OTA_MQTT_INTERVAL_MS) {
		ota_mqtt_send(ctx);
		ctx->mqtt_last_send = now;
	}

	if (ctx->check.new_version && ctx->check.apply) {
		ctx->check.apply--;
		ota_update_apply(&ctx->check);
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

static int ota_udate_check_run(struct ota_context_t *ctx, char *params, bool apply)
{
	char *fname = NULL;
	int ret = -1;

	if (ctx->update.started || ctx->check.started) {
		hlog_warning(OTA_MODULE, "Update is running already.");
		return -1;
	}
	if (!params || strlen(params) < 1 || params[0] != ':')
		goto out;

	ota_check_reset(&ctx->check);
	if (!ctx->check.param_cache ||
		strlen(params) != strlen(ctx->check.param_cache) ||
		strcmp(params, ctx->check.param_cache)) {

		free(ctx->check.param_cache);
		ctx->check.param_cache = strdup(params);
		free(ctx->check.file.fname);
		free(ctx->check.file.peer);
		memset(&(ctx->check.file), 0, sizeof(ctx->check.file));
		if (tftp_url_parse(params + 1, &(ctx->check.file)))
			goto out;

		if (!ctx->check.file.fname) {
			ctx->check.file.fname = strdup(ctx->check.meta_file_name);
		} else if (ctx->check.file.fname[strlen(ctx->check.file.fname)] == '/') {
			sys_asprintf(&fname, "%s%s", ctx->check.file.fname, ctx->check.meta_file_name);
			if (!fname)
				goto out;
			free(ctx->check.file.fname);
			ctx->check.file.fname = fname;
		}
	}
	if (apply)
		ctx->check.apply = APPLY_RETRIES;
	else
		ctx->check.apply = 0;
	ret = ota_check_start(&(ctx->check));

out:
	if (ret) {
		hlog_warning(OTA_MODULE, "Wrong parameters");
		free(ctx->check.param_cache);
		ctx->check.param_cache = NULL;
		ota_check_reset(&ctx->check);
	}
	return ret;
}

static int ota_udate_check_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	return ota_udate_check_run(wctx, params, false);
}

static int ota_udate_check_apply_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;

	UNUSED(cmd);
	UNUSED(ctx);
	return ota_udate_check_run(wctx, params, true);
}

#define CHECK_NEW	"new"
#define CHECK_VER	"version"
#define CHECK_TIME	"time"
static int ota_udate_strategy_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;
	char *tok, *rest;
	bool new = false;
	bool ver = false;
	bool time = false;

	UNUSED(ctx);
	UNUSED(cmd);

	if (!params || strlen(params) < 1 || params[0] != ':')
		goto out_err;

	rest = params + 1;
	if (rest[0] == ':') {
		rest++;
	} else {
		tok = strtok_r(rest, ":", &rest);
		if (tok && strlen(tok) == strlen(CHECK_NEW) && !strcmp(tok, CHECK_NEW))
			new = true;
	}

	if (rest[0] == ':') {
		rest++;
	} else {
		tok = strtok_r(rest, ":", &rest);
		if (tok && strlen(tok) == strlen(CHECK_VER) && !strcmp(tok, CHECK_VER))
			ver = true;
	}

	if (rest[0] == ':') {
		rest++;
	} else {
		tok = strtok_r(rest, ":", &rest);
		if (tok && strlen(tok) == strlen(CHECK_TIME) && !strcmp(tok, CHECK_TIME))
			time = true;
	}

	ota_check_set_strategy(&wctx->check, new, ver, time);
	hlog_info(OTA_MODULE, "Set auto update strategy: %s, %s, %s",
			  new ? "latest" : "any",
			  ver ? "check version" : "does not check version",
			  time ? "check built time" : "does not check build time");

	return 0;

out_err:
	hlog_warning(OTA_MODULE, "Wrong parameters");
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
	wctx->update.apply = 0;
	return 0;
}

static int ota_udate_apply_cmd(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct ota_context_t *wctx = (struct ota_context_t *)user_data;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(params);

	if (wctx->check.new_version) {
		ota_update_apply(&wctx->check);
	} else {
		if (IS_DEBUG(wctx))
			hlog_info(OTA_MODULE, "No pending update");
	}

	return 0;
}

static app_command_t ota_cmd_requests[] = {
	{"update", ":tftp://<server>[:<port>]/<file> - Update the firmware using <file> from tftp <server> on [port]",
	  ota_udate_start_cmd},
	{"check", ":tftp://<server>[:<port>]/[<file>] - Check for updates on tftp <server>:[port], looking for [<file>] meta file",
	  ota_udate_check_cmd},
	{"apply", " - Apply pending update", ota_udate_apply_cmd},
	{"check_apply", ":tftp://<server>[:<port>]/[<file>] - Check and apply update from tftp <server>:[port], looking for [<file>] meta file",
	  ota_udate_check_apply_cmd},
	{"check_strategy", ":new:version:time - Logic used to check for new version", ota_udate_strategy_cmd},
	{"cancel", " - Cancel update in progress", ota_udate_cancel_cmd},
};

static void ota_mqtt_init(struct ota_context_t *ctx)
{
	/* Current version */
	ctx->mqtt_comp[0].module = OTA_MODULE;
	ctx->mqtt_comp[0].platform = "sensor";
	ctx->mqtt_comp[0].value_template = "{{ value_json['current_version'] }}";
	ctx->mqtt_comp[0].name = "current_version";
	mqtt_msg_component_register(&(ctx->mqtt_comp[0]));

	/* Update detected */
	ctx->mqtt_comp[1].module = OTA_MODULE;
	ctx->mqtt_comp[1].platform = "binary_sensor";
	ctx->mqtt_comp[1].payload_on = "1";
	ctx->mqtt_comp[1].payload_off = "0";
	ctx->mqtt_comp[1].value_template = "{{ value_json['update'] }}";
	ctx->mqtt_comp[1].name = "update";
	ctx->mqtt_comp[1].state_topic = ctx->mqtt_comp[0].state_topic;
	mqtt_msg_component_register(&(ctx->mqtt_comp[1]));

	/* New version */
	ctx->mqtt_comp[2].module = OTA_MODULE;
	ctx->mqtt_comp[2].platform = "sensor";
	ctx->mqtt_comp[2].value_template = "{{ value_json['new_version'] }}";
	ctx->mqtt_comp[2].name = "new_version";
	ctx->mqtt_comp[2].state_topic = ctx->mqtt_comp[0].state_topic;
	mqtt_msg_component_register(&(ctx->mqtt_comp[2]));
}

int sys_ota_init(struct ota_context_t  **ctx)
{
	*ctx = calloc(1, sizeof(struct ota_context_t));
	if (!(*ctx))
		return -1;

	(*ctx)->update.ota = *ctx;
	(*ctx)->check.ota = *ctx;

	if (sys_asprintf(&(*ctx)->check.meta_file_name, "%s.meta", IMAGE_NAME) <= 0)
		goto out_err;

	ota_update_reset(&(*ctx)->update);
	ota_check_reset(&(*ctx)->check);
	ota_mqtt_init(*ctx);

	/* Check for images with new version */
	ota_check_set_strategy(&(*ctx)->check, true, true, false);

	__ota_context = *ctx;

	return 0;

out_err:
	free((*ctx)->check.meta_file_name);
	free(*ctx);
	(*ctx) = NULL;
	return -1;
}

void sys_ota_register(void)
{
	struct ota_context_t  *ctx = NULL;

	if (sys_ota_init(&ctx))
		return;

	ctx->mod.name = OTA_MODULE;
	ctx->mod.run = sys_ota_run;
	ctx->mod.log = sys_ota_log_status;
	ctx->mod.debug = sys_ota_debug_set;
	ctx->mod.job_flags = OTA_JOB;
	ctx->mod.commands.hooks = ota_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(ota_cmd_requests);
	ctx->mod.commands.description = "OTA update";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
