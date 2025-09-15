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
#include "ccronexpr.h"

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
#define	WH_PAYLOAD_TEMPLATE "Scripts [%s] is running"

#define IS_DEBUG(C)	((C)->debug)

#define COMMENT_CHAR	'#'
#define SPEC_CHAR		'@'
#define SCRIPT_EXTENSION			".run"

#define SCRIPT_PARAM_NAME			"@name"
#define SCRIPT_PARAM_DESC			"@desc"
#define SCRIPT_PARAM_CRON			"@cron"
#define SCRIPT_PARAM_CRON_ENABLE	"@cron_enable"
#define SCRIPT_PARAM_NOTIFY			"@notify"
enum script_params_id {
	SCRIPT_CFG_NAME = 0,
	SCRIPT_CFG_DESC = 1,
	SCRIPT_CFG_CRON_ENABLE = 2,
	SCRIPT_CFG_CRON = 3,
	SCRIPT_CFG_NOTIFY = 4,
	SCRIPT_CFG_MAX	= 5,
};
static __in_flash() char *script_configs[SCRIPT_CFG_MAX] = {
	 SCRIPT_PARAM_NAME,
	 SCRIPT_PARAM_DESC,
	 SCRIPT_PARAM_CRON_ENABLE,
	 SCRIPT_PARAM_CRON,
	 SCRIPT_PARAM_NOTIFY
};

struct script_cron_t {
	bool valid;
	bool enable;
	cron_expr schedule;
	time_t next;
};

struct script_mqtt_t {
	uint64_t last_send;
	mqtt_component_t script;
	mqtt_component_t last_run;
	mqtt_component_t next_run;
	mqtt_component_t corn;
};

struct script_t {
	char *name;
	char *desc;
	char *file;
	bool run;
	bool notify;
	int exec_count;
	int fd;
	bool notify_enable;
	uint64_t last_run;
	time_t last_run_date;
	struct script_cron_t cron;
	struct script_mqtt_t mqtt;
};

struct scripts_context_t {
	sys_module_t mod;
	uint32_t debug;
	int count;
	struct script_t *scripts;
	struct script_t *run;
	uint64_t last_cron;
	cmd_run_context_t cmd_ctx;
	int idx;
	char line[MAX_LINE];
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static bool sys_scripts_log_status(void *context)
{
	struct scripts_context_t *ctx = (struct scripts_context_t *)context;
	static char time_buff[TIME_STR];
	datetime_t dt = {0};
	int i;

	if (ctx->count) {
		hlog_info(SCRIPTS_MODULE, "Loaded scripts:");
		for (i = 0; i < ctx->count; i++) {
			hlog_info(SCRIPTS_MODULE, "\t%s:\t[%s] %s",
					   ctx->scripts[i].file, ctx->scripts[i].name, ctx->scripts[i].desc);
		   hlog_info(SCRIPTS_MODULE, "\t  Executed %d times", ctx->scripts[i].exec_count);
			if (ctx->scripts[i].last_run_date) {
				if (time_to_datetime(ctx->scripts[i].last_run_date, &dt)) {
					datetime_to_str(time_buff, TIME_STR, &dt);
					hlog_info(SCRIPTS_MODULE, "\t  Last run: %s", time_buff);
				}
			} else {
				hlog_info(SCRIPTS_MODULE, "\t  Last run: N/A");
			}
			if (ctx->scripts[i].cron.valid) {
				if (ctx->scripts[i].cron.enable) {
					if (ctx->scripts[i].cron.next > 0) {
						if (time_to_datetime(ctx->scripts[i].cron.next, &dt)) {
							datetime_to_str(time_buff, TIME_STR, &dt);
							hlog_info(SCRIPTS_MODULE, "\t  Next run: %s", time_buff);
						}
					} else {
						hlog_info(SCRIPTS_MODULE, "\t  Next run: N/A");
					}
				} else {
					hlog_info(SCRIPTS_MODULE, "\t  Cron is disabled");
				}
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

static void script_cron_set_next(struct scripts_context_t *ctx, struct script_t *script)
{
	datetime_t date;
	time_t tm;

	if (!ntp_time_valid() || !script->cron.valid)
		return;
	if (!tz_datetime_get(&date))
		return;
	if (!datetime_to_time(&date, &tm))
		return;
	script->cron.next = cron_next(&script->cron.schedule, tm);
	script->mqtt.script.force = true;
	if (IS_DEBUG(ctx)) {
		char time_buff[TIME_STR];

		time_to_datetime(script->cron.next, &date);
		datetime_to_str(time_buff, TIME_STR, &date);
		hlog_info(SCRIPTS_MODULE, "[%s] set next run to [%s]\n\r", script->name, time_buff);
	}

}

static int script_param_load (struct script_t *script, char *param)
{
	const char *err;
	char *data;
	int n, i;

	n = strspn(param, " \t");
	for (i = 0; i < SCRIPT_CFG_MAX; i++) {
		if (strlen(param + n) > strlen(script_configs[i]) && !strncmp(param + n, script_configs[i], strlen(script_configs[i])))
			break;
	}
	if (i >= SCRIPT_CFG_MAX)
		return 0;
	data = param + strlen(script_configs[i]) + n;
	data += strspn(data, " \t");
	if (strlen(data) < 1)
		return 0;
	switch (i) {
	case SCRIPT_CFG_NAME:
		script->name = strdup(data);
		break;
	case SCRIPT_CFG_DESC:
		script->desc = strdup(data);
		break;
	case SCRIPT_CFG_CRON:
		err = NULL;
		cron_parse_expr(data, &script->cron.schedule, &err);
		if (err)
			hlog_info(SCRIPTS_MODULE, "Invalid cron [%s]: %s", data, err ? err : "N/A");
		else
			script->cron.valid = true;
		break;
	case SCRIPT_CFG_CRON_ENABLE:
		script->cron.enable = (bool)strtol(data, NULL, 0);
		break;
	case SCRIPT_CFG_NOTIFY:
		script->notify_enable = (bool)strtol(data, NULL, 0);
		break;
	default:
		return 0;
	}

	return 1;
}

static int script_load(struct scripts_context_t *ctx, char *fname, struct script_t *script)
{
	int elen = strlen(SCRIPT_EXTENSION);
	int params = 0;
	char *ldata;
	int fd = -1;
	int ret;

	if (sys_asprintf(&script->file, "%s/%s", SCRIPTS_DIR, fname) <= 0)
		goto out_err;

	fd = fs_open(script->file, LFS_O_RDONLY);
	if (fd < 0)
		goto out_err;
	do {
		ret = fs_gets(fd, ctx->line, MAX_LINE);
		if (ret < 0)
			break;
		if (!ret)
			continue;
		ldata = ctx->line;
		ldata += strspn(ldata, " \t");
		if (ldata[0] == COMMENT_CHAR)
			continue;

		params += script_param_load(script, ldata);
		if (params >= SCRIPT_CFG_MAX)
			break;
	} while (true);

	if (!script->name) {
		script->name = strdup(fname);
		if (!script->name)
			goto out_err;
		script->name[strlen(fname) - elen] = 0;
	}

	if (script->cron.valid && script->cron.enable)
		script_cron_set_next(ctx, script);
	script->fd = -1;
	fs_close(fd);

	if (IS_DEBUG(ctx))
		hlog_info(SCRIPTS_MODULE, "Loaded script [%s]\t%s", script->name, script->desc ? script->desc : "");

	return 0;

out_err:
	free(script->file);
	script->file = NULL;
	free(script->name);
	script->name = NULL;
	free(script->desc);
	script->desc = NULL;
	if (fd >= 0)
		fs_close(fd);
	return -1;
}

static void scripts_init(struct scripts_context_t *ctx)
{
	uint32_t elen = strlen(SCRIPT_EXTENSION);
	struct lfs_info linfo;
	uint32_t slen;
	int i, count;
	int fd = -1;

	count = fs_get_files_count(SCRIPTS_DIR, SCRIPT_EXTENSION);
	if (count <= 0)
		return;
	fd = pico_dir_open(SCRIPTS_DIR);
	if (fd < 0)
		return;
	if (ctx->scripts) {
		for (i = 0; i < ctx->count; i++) {
			free(ctx->scripts[i].name);
			free(ctx->scripts[i].desc);
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
		if (slen <= elen)
			continue;
		slen -= elen;
		if (strncmp(linfo.name + slen, SCRIPT_EXTENSION, elen))
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
	datetime_t date;
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
		if (tz_datetime_get(&date))
			datetime_to_time(&date, &ctx->run->last_run_date);
		ctx->run->exec_count++;
		ctx->run->mqtt.script.force = true;
	}
	ctx->run = NULL;
}

static void script_notify(struct script_t *script)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	if (!webhook_connected())
		return;
	snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE, script->name);
	if (!webhook_send(notify_buff))
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
	if (!ctx->scripts[idx].mqtt.script.force &&
	     ctx->scripts[idx].mqtt.last_send && (now - ctx->scripts[idx].mqtt.last_send) < WH_SEND_DELAY_MS)
		return 0;

	ADD_MQTT_MSG("{");
	ADD_MQTT_MSG_VAR("\"timestamp\": \"%s\"", get_current_time_str(time_buff, TIME_STR))
	ADD_MQTT_MSG_VAR(",\"name\": \"%s\"", ctx->scripts[idx].name);
	ADD_MQTT_MSG_VAR(",\"exec_count\": \"%d\"", ctx->scripts[idx].exec_count);
	ADD_MQTT_MSG_VAR(",\"cron_enabled\": \"%d\"", ctx->scripts[idx].cron.enable);
	if (ctx->scripts[idx].last_run_date) {
		time_to_datetime(ctx->scripts[idx].last_run_date, &dt);
		datetime_to_str(time_buff, TIME_STR, &dt);
		ADD_MQTT_MSG_VAR(",\"last_run\":\"%s\"", time_buff);
	} else {
		ADD_MQTT_MSG(",\"last_run\":\"N/A\"");
	}
	if (ctx->scripts[idx].cron.next > 0) {
		time_to_datetime(ctx->scripts[idx].cron.next, &dt);
		datetime_to_str(time_buff, TIME_STR, &dt);
		ADD_MQTT_MSG_VAR(",\"next_run\":\"%s\"", time_buff);
	} else {
		ADD_MQTT_MSG(",\"next_run\":\"N/A\"");
	}

	ADD_MQTT_MSG("}")
	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->scripts[idx].mqtt.script, ctx->mqtt_payload);

	if (!ret)
		ctx->scripts[idx].mqtt.last_send = now;
	return ret;
}

static void script_cron_check(struct scripts_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	datetime_t date_now;
	time_t time_now;
	int i;

	if (!ntp_time_valid())
		return;
	if ((now - ctx->last_cron) < CRON_CHECK_MS)
		return;
	if (!tz_datetime_get(&date_now))
		return;
	if (!datetime_to_time(&date_now, &time_now))
		return;

	for (i = 0; i < ctx->count; i++) {
		if (!ctx->scripts[i].cron.valid)
			continue;
		if (!ctx->scripts[i].cron.enable)
			continue;
		if (ctx->scripts[i].cron.next <= 0) {
			script_cron_set_next(ctx, &ctx->scripts[i]);
			continue;
		}
		if (ctx->scripts[i].cron.next <= time_now) {
			ctx->scripts[i].run = true;
			script_cron_set_next(ctx, &ctx->scripts[i]);
		}
	}

	ctx->last_cron = now;
}

static void sys_scripts_run(void *context)
{
	struct scripts_context_t *ctx = (struct scripts_context_t *)context;

	if (ctx->count < 1)
		return;

	if (ctx->run) {
		script_run(ctx);
		return;
	}

	if (ctx->idx >= ctx->count)
		ctx->idx = 0;
	if (ctx->scripts[ctx->idx].run) {
		ctx->run = &ctx->scripts[ctx->idx];
		ctx->scripts[ctx->idx].run = false;
		if (ctx->scripts[ctx->idx].notify_enable)
			ctx->scripts[ctx->idx].notify = true;
		if (IS_DEBUG(ctx))
			hlog_info(SCRIPTS_MODULE, "Run script [%s]", ctx->run->name);
	}
	if (ctx->scripts[ctx->idx].notify)
		script_notify(&ctx->scripts[ctx->idx]);
	script_cron_check(ctx);
	script_mqtt_send(ctx, ctx->idx);
	ctx->idx++;
}

static void scripts_mqtt_init(struct scripts_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->scripts[i].mqtt.script.module = SCRIPTS_MODULE;
		ctx->scripts[i].mqtt.script.platform = "sensor";
		ctx->scripts[i].mqtt.script.value_template = "{{ value_json.name }}";
		sys_asprintf(&ctx->scripts[i].mqtt.script.name, "%s_script", ctx->scripts[i].name);
		mqtt_msg_component_register(&ctx->scripts[i].mqtt.script);

		ctx->scripts[i].mqtt.last_run.module = SCRIPTS_MODULE;
		ctx->scripts[i].mqtt.last_run.platform = "sensor";
		ctx->scripts[i].mqtt.last_run.value_template = "{{ value_json.last_run }}";
		sys_asprintf(&ctx->scripts[i].mqtt.last_run.name, "%s_last_run", ctx->scripts[i].name);
		ctx->scripts[i].mqtt.last_run.state_topic = ctx->scripts[i].mqtt.script.state_topic;
		mqtt_msg_component_register(&ctx->scripts[i].mqtt.last_run);
		ctx->scripts[i].mqtt.last_run.force = false;

		ctx->scripts[i].mqtt.next_run.module = SCRIPTS_MODULE;
		ctx->scripts[i].mqtt.next_run.platform = "sensor";
		ctx->scripts[i].mqtt.next_run.value_template = "{{ value_json.next_run }}";
		sys_asprintf(&ctx->scripts[i].mqtt.next_run.name, "%s_next_run", ctx->scripts[i].name);
		ctx->scripts[i].mqtt.next_run.state_topic = ctx->scripts[i].mqtt.script.state_topic;
		mqtt_msg_component_register(&ctx->scripts[i].mqtt.next_run);
		ctx->scripts[i].mqtt.next_run.force = false;

		ctx->scripts[i].mqtt.corn.module = SCRIPTS_MODULE;
		ctx->scripts[i].mqtt.corn.platform = "binary_sensor";
		ctx->scripts[i].mqtt.corn.payload_on = "1";
		ctx->scripts[i].mqtt.corn.payload_off = "0";
		ctx->scripts[i].mqtt.corn.value_template = "{{ value_json.cron_enabled }}";
		sys_asprintf(&ctx->scripts[i].mqtt.corn.name, "%s_cron_enabled", ctx->scripts[i].name);
		ctx->scripts[i].mqtt.corn.state_topic = ctx->scripts[i].mqtt.script.state_topic;
		mqtt_msg_component_register(&ctx->scripts[i].mqtt.corn);
		ctx->scripts[i].mqtt.corn.force = false;
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
	if ((*ctx)->count < 1) {
		hlog_info(SCRIPTS_MODULE, "No scripts detected on the file system.");
		return false;
	}
	scripts_mqtt_init(*ctx);
	(*ctx)->cmd_ctx.type = CMD_CTX_SCRIPT;

	return true;
}

static int scripts_cmd_auto_run(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct scripts_context_t *wctx = (struct scripts_context_t *)user_data;
	char *tok, *rest;
	int i, k;

	UNUSED(cmd);
	UNUSED(ctx);

	if (!params || params[0] != ':' || strlen(params) < 2) {
		hlog_info(SCRIPTS_MODULE, "Invalid name parameter ...");
		return 0;
	}

	tok = strtok_r(params, ":", &rest);
	if (!tok)
		return -1;
	for (i = 0; i < wctx->count; i++) {
		if (strlen(tok) != strlen(wctx->scripts[i].name))
			continue;
		if (strncmp(tok, wctx->scripts[i].name, strlen(tok)))
			continue;
		break;
	}
	if (i >= wctx->count) {
		hlog_info(SCRIPTS_MODULE, "Cannot find script with name [%s]", tok);
		return -1;
	}
	if (!wctx->scripts[i].cron.valid) {
		hlog_info(SCRIPTS_MODULE, "Script [%s] has no configured cron schedule", wctx->scripts[i].name);
		return -1;
	}
	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		return -1;
	k = (int)strtol(tok, NULL, 0);
	if (k) {
		wctx->scripts[i].cron.enable = true;
		script_cron_set_next(wctx, &wctx->scripts[i]);
	} else {
		wctx->scripts[i].cron.enable = false;
		wctx->scripts[i].cron.next = 0;
	}

	wctx->scripts[i].mqtt.script.force = true;

	hlog_info(SCRIPTS_MODULE, "%s autorun of script [%s]",
			  wctx->scripts[i].cron.enable ? "Enabled" : "Disabled",
			  wctx->scripts[i].name);

	return 0;
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
	{"run", ":<name> - run the script with given name", scripts_cmd_run},
	{"auto_run", ":<name>:<0/1> - Disable / Enable auto run of the script with given name ", scripts_cmd_auto_run}
};

void sys_scripts_register(void)
{
	struct scripts_context_t *ctx = NULL;

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
