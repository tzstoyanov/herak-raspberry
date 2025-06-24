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
#include "params.h"

#define SCRIPTS_MODULE "scripts"
#define SCRIPTS_DIR     "/scripts"
#define MAX_VARS     50
#define BUFF_SIZE    300
#define MAX_LINE	256
#define TIME_STR	64
#define MAX_CMD_HANDLERS		64
#define MQTT_DATA_LEN   512
#define WH_SEND_DELAY_MS	60000
#define CRON_CHECK_MS		30000
#define	WH_PAYLOAD_MAX_SIZE	128
#define WH_HTTP_CMD		"POST"
#define WH_HTTP_TYPE	"application/json"
#define	WH_PAYLOAD_TEMPLATE "{ \"script\": %s, \"status\": \"%s\"}"

#define IS_DEBUG(C)	((C)->debug != 0)

#define COMMENT_CHAR	'#'
#define SPEC_CHAR		'@'
static __in_flash() char *script_name_str = "@name";
static __in_flash() char *script_desc_str = "@desc";
static __in_flash() char *script_cron_str = "@cron";
static __in_flash() char *script_ext_run_str = ".run";
static uint32_t script_namelen;
static uint32_t script_desclen;
static uint32_t script_cronlen;
static uint32_t script_ext_run_len;

struct script_t {
	char *name;
	char *desc;
	char *cron;
	char *file;
	bool run;
	bool notify;
	int fd;
	uint64_t last_run;
	uint64_t mqtt_last_send;
	mqtt_component_t mqtt_comp;
};

struct scripts_context_t {
	sys_module_t mod;
	uint32_t debug;
	int count;
	struct script_t *scripts;
	struct script_t *run;
	cmd_run_context_t cmd_ctx;
	int idx;
	char line[MAX_LINE];
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static struct scripts_context_t *__scripts_context;

static struct scripts_context_t *scripts_context_get(void)
{
	return __scripts_context;
}

static bool sys_scripts_log_status(void *context)
{
	struct scripts_context_t *ctx = (struct scripts_context_t *)context;
	static char time_buff[TIME_STR];
	datetime_t dt = {0};
	int i;

	if (ctx->count) {
		hlog_info(SCRIPTS_MODULE, "Loaded scripts:");
		for (i = 0; i < ctx->count; i++) {
			hlog_info(SCRIPTS_MODULE, "\t%s:\t[%s]\t%s",
					   ctx->scripts[i].file, ctx->scripts[i].name, ctx->scripts[i].desc);
			if (ctx->scripts[i].last_run) {
				time_msec2datetime(&dt, time_ms_since_boot() - ctx->scripts[i].last_run);
				hlog_info(SCRIPTS_MODULE, "\t Last run: %s", time_date2str(time_buff, TIME_STR, &dt));
			} else {
				hlog_info(SCRIPTS_MODULE, "\t Last run: N/A");
			}
		}
	} else {
		hlog_info(SCRIPTS_MODULE, "No scripts are loaded.");
	}

	return true;
}

static void sys_scripts_debug_set(uint32_t lvl, void *context)
{
	struct scripts_context_t *ctx = (struct scripts_context_t *)context;

	ctx->debug = lvl;
}

static int script_load(struct scripts_context_t *ctx, char *fname, struct script_t *script)
{
	char *ldata;
	int fd = -1;
	int ret;
	int n;

	if (sys_asprintf(&script->file, "%s/%s", SCRIPTS_DIR, fname) <= 0)
		goto out_err;

	fd = fs_open(script->file, LFS_O_RDONLY);
	if (fd < 0)
		goto out_err;
	do {
		ret = fs_gets(fd, ctx->line, MAX_LINE);
		if (ret <= 0)
			break;
		ldata = ctx->line;
		ldata += strspn(ldata, " \t");
		if (ldata[0] == COMMENT_CHAR)
			continue;
		if (strlen(ldata) > script_namelen && !strncmp(ldata, script_name_str, script_namelen)) {
			n = strspn(ldata + script_namelen, " \t");
			script->name = strdup(ldata + script_namelen + n);

		}
		if (strlen(ldata) > script_desclen && !strncmp(ldata, script_desc_str, script_desclen)) {
			n = strspn(ldata + script_desclen, " \t");
			script->desc = strdup(ldata + script_desclen + n);
		}
		if (strlen(ldata) > script_cronlen && !strncmp(ldata, script_cron_str, script_cronlen)) {
			n = strspn(ldata + script_desclen, " \t");
			script->cron = strdup(ldata + script_cronlen + n);
		}
		if (script->name && script->desc && script->cron)
			break;
	} while (true);

	if (!script->name) {
		script->name = strdup(fname);
		if (!script->name)
			goto out_err;
		script->name[strlen(fname) - script_ext_run_len] = 0;
	}
	script->fd = -1;
	fs_close(fd);

	if (IS_DEBUG(ctx))
		hlog_info(SCRIPTS_MODULE, "Loaded script [%s]\t%s", script->name, script->desc ? script->desc : "");

	return 0;

out_err:
	free(script->file);
	free(script->name);
	free(script->desc);
	free(script->cron);
	if (fd >= 0)
		fs_close(fd);
	return -1;
}

static void scripts_init(struct scripts_context_t *ctx)
{
	struct lfs_info linfo;
	uint32_t slen;
	int i, count;
	int fd = -1;

	count = fs_get_files_count(SCRIPTS_DIR, script_ext_run_str);
	if (count <= 0)
		return;
	fd = pico_dir_open(SCRIPTS_DIR);
	if (fd < 0)
		return;
	if (ctx->scripts) {
		for (i = 0; i < ctx->count; i++) {
			free(ctx->scripts[i].name);
			free(ctx->scripts[i].desc);
			free(ctx->scripts[i].cron);
			free(ctx->scripts[i].file);
		}
		free(ctx->scripts);
		ctx->scripts = NULL;
	}

	ctx->count = 0;
	ctx->scripts = calloc(count, sizeof(struct script_t));
	if (!ctx->scripts)
		goto out;
	do {
		if (pico_dir_read(fd, &linfo) <= 0)
			break;
		if (linfo.type != LFS_TYPE_REG)
			continue;
		slen = strlen(linfo.name);
		if (slen <= script_ext_run_len)
			continue;
		slen -= script_ext_run_len;
		if (strncmp(linfo.name + slen, script_ext_run_str, script_ext_run_len))
			continue;
		if (!script_load(ctx, linfo.name, &ctx->scripts[ctx->count]))
			ctx->count++;
		if (ctx->count >= count)
			break;
	} while (true);

out:
	if (fd >= 0)
		pico_dir_close(fd);
}

static void script_run(struct scripts_context_t *ctx)
{
	char *ldata;
	int ret;

	if (ctx->run->fd < 0)
		ctx->run->fd = fs_open(ctx->run->file, LFS_O_RDONLY);
	if (ctx->run->fd < 0)
		goto out_end;

	do {
		ret = fs_gets(ctx->run->fd, ctx->line, MAX_LINE);
		if (ret < 0)
			goto out_end;
		if (!ret)
			continue;
		ldata = ctx->line;
		ldata += strspn(ldata, " \t");
		if (ldata[0] == COMMENT_CHAR || ldata[0] == SPEC_CHAR)
			continue;
		ret = cmd_exec(&(ctx->cmd_ctx), ldata);
		if (IS_DEBUG(ctx))
			hlog_info(SCRIPTS_MODULE, "Executed command [%s]: %d", ldata, ret);
		break;
	} while (true);

	return;

out_end:
	if (ctx->run->fd >= 0) {
		fs_close(ctx->run->fd);
		ctx->run->fd = -1;
		ctx->run->last_run = time_ms_since_boot();
	}
	ctx->run = NULL;
}

static void script_notify(struct script_t *script)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	if (!webhook_connected() || !script->notify)
		return;
	snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
			 script->name, "running");
	if (!webhook_send(notify_buff, strlen(notify_buff), WH_HTTP_CMD, WH_HTTP_TYPE))
		script->notify = false;
}

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int script_mqtt_send(struct scripts_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	datetime_t dt = {0};
	int count = 0;
	int ret;

	if (!ctx->count || idx < 0 || idx >= ctx->count)
		return 0;

	if (ctx->scripts[idx].mqtt_last_send && (now - ctx->scripts[idx].mqtt_last_send) < WH_SEND_DELAY_MS)
		return 0;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR))
	ADD_MQTT_MSG_VAR(",\"name\": \"%s\"", ctx->scripts[idx].name);
	if (ctx->scripts[idx].last_run) {
		time_msec2datetime(&dt, time_ms_since_boot() - ctx->scripts[idx].last_run);
		ADD_MQTT_MSG_VAR(",\"last_run\":\"%s\"", time_date2str(time_buff, TIME_STR, &dt));
	} else {
		ADD_MQTT_MSG(",\"last_run\":\"N/A\"");
	}
	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->scripts[idx].mqtt_comp, ctx->mqtt_payload);

	if (!ret)
		ctx->scripts[idx].mqtt_last_send = now;
	return ret;
}

static void sys_scripts_run(void *context)
{
	struct scripts_context_t *ctx = (struct scripts_context_t *)context;

	if (ctx->run) {
		script_run(ctx);
		return;
	}

	if (ctx->idx >= ctx->count)
		ctx->idx = 0;
	if (ctx->scripts[ctx->idx].run) {
		ctx->run = &ctx->scripts[ctx->idx];
		ctx->scripts[ctx->idx].run = false;
		ctx->scripts[ctx->idx].notify = true;
		if (IS_DEBUG(ctx))
			hlog_info(SCRIPTS_MODULE, "Run script [%s]", ctx->run->name);
	}
	if (ctx->scripts[ctx->idx].notify)
		script_notify(&ctx->scripts[ctx->idx]);
	script_mqtt_send(ctx, ctx->idx);
	ctx->idx++;
}

static void scripts_mqtt_init(struct scripts_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->scripts[i].mqtt_comp.module = SCRIPTS_MODULE;
		ctx->scripts[i].mqtt_comp.platform = "sensor";
		ctx->scripts[i].mqtt_comp.value_template = "{{ value_json.name }}";
		ctx->scripts[i].mqtt_comp.name = ctx->scripts[i].name;
		mqtt_msg_component_register(&ctx->scripts[i].mqtt_comp);
	}
}

static bool sys_scripts_init(struct scripts_context_t **ctx)
{
	int fd;

	if (!fs_is_mounted())
		return false;

	(*ctx) = (struct scripts_context_t *)calloc(1, sizeof(struct scripts_context_t));
	if (!(*ctx))
		return false;

	fd = pico_dir_open(SCRIPTS_DIR);
	if (fd < 0)
		pico_mkdir(SCRIPTS_DIR);
	else
		pico_dir_close(fd);

	scripts_init(*ctx);
	scripts_mqtt_init(*ctx);
	(*ctx)->cmd_ctx.type = CMD_CTX_SCRIPT;

	__scripts_context = *ctx;

	return true;
}

static int scripts_cmd_run(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct scripts_context_t *wctx = (struct scripts_context_t *)user_data;
	char *name, *rest;
	int i;

	UNUSED(cmd);
	UNUSED(ctx);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_info(SCRIPTS_MODULE, "Invalid name parameter ...");
		return 0;
	}

	name = strtok_r(params, ":", &rest);
	for (i = 0; i < wctx->count; i++) {
		if (strlen(name) != strlen(wctx->scripts[i].name))
			continue;
		if (strncmp(name, wctx->scripts[i].name, strlen(name)))
			continue;
		wctx->scripts[i].run = true;
		break;
	}

	if (i >= wctx->count)
		hlog_info(SCRIPTS_MODULE, "Cannot find script with name [%s]", name);

	return 0;
}

static app_command_t scripts_cmd_requests[] = {
	{"run", ":<name> - run the script with given name", scripts_cmd_run}
};

void sys_scripts_register(void)
{
	struct scripts_context_t *ctx = NULL;

	script_namelen = strlen(script_name_str);
	script_desclen = strlen(script_desc_str);
	script_cronlen = strlen(script_cron_str);
	script_ext_run_len = strlen(script_ext_run_str);

	if (!sys_scripts_init(&ctx))
		return;

	ctx->mod.name = SCRIPTS_MODULE;
	ctx->mod.run = sys_scripts_run;
	ctx->mod.log = sys_scripts_log_status;
	ctx->mod.debug = sys_scripts_debug_set;
	ctx->mod.commands.hooks = scripts_cmd_requests;
	ctx->mod.commands.count = ARRAY_SIZE(scripts_cmd_requests);
	ctx->mod.commands.description = "Scripts";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
