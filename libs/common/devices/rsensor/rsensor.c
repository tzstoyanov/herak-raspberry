// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/float.h"
#include "lwjson/lwjson.h"
#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define RSENS_MODULE	"rsensor"
#define RSENS_MAX		10

#define MAX_JSON_TOKENS	20

#define IS_DEBUG(C)	((C)->debug)

struct rsens_context_t;

struct rsensor_t {
	char *name;
	char *topic;
	char *key;
	float val;
	bool valid;
	bool subscribed;
	uint64_t last_data;
	struct rsens_context_t *ctx;
};

struct rsens_context_t {
	sys_module_t mod;
	int count;
	struct rsensor_t *sensors[RSENS_MAX];
	uint32_t debug;
};

static struct rsens_context_t *__rsens_context;

struct rsens_context_t *rsens_context_get(void)
{
	return __rsens_context;
}

#define TIME_STR	64
static bool rsens_log(void *context)
{
	struct rsens_context_t *ctx = (struct rsens_context_t *)context;
	char time_buff[TIME_STR];
	struct tm date;
	uint64_t now;
	int i;

	now = time_ms_since_boot();
	for (i = 0; i < ctx->count && ctx->sensors[i]; i++) {
		if (ctx->sensors[i]->last_data) {
			time_msec2datetime(&date, now - ctx->sensors[i]->last_data);
			time_date2str(time_buff, TIME_STR, &date);
			hlog_info(RSENS_MODULE, "Sensor %s (%d):  %3.2f%s last data [%s]",
					  ctx->sensors[i]->name, i, ctx->sensors[i]->val,
					  ctx->sensors[i]->valid ? "," : " (invalid),",
					  time_buff);
		} else {
			hlog_info(RSENS_MODULE, "Sensor %s (%d), No valid reading yet",
					  ctx->sensors[i]->name, i);
		}
		hlog_info(RSENS_MODULE, "\ttopic: %s", ctx->sensors[i]->topic);
		if (ctx->sensors[i]->key)
			hlog_info(RSENS_MODULE, "\tkey: %s", ctx->sensors[i]->key);
	}
	return true;
}

static int rsens_json_parse(struct rsensor_t *sens, lwjson_t *lwjson)
{
	const lwjson_token_t *tok;
	float val = 0;
	int ret = -1;

	if (!lwjson)
		goto out;

	/* Find sensor key in JSON */
	tok = lwjson_find(lwjson, sens->key);
	if (!tok) {
		if (IS_DEBUG(sens->ctx))
			hlog_info(RSENS_MODULE, "No valid JSON key [%s]", sens->key);
		return ret;
	}
	switch (tok->type) {
	case LWJSON_TYPE_STRING:
		if (!tok->u.str.token_value || tok->u.str.token_value_len < 1)
			goto out;
		if (sys_strtof(tok->u.str.token_value, &val))
			goto out;
		sens->val = val;
		break;
	case LWJSON_TYPE_NUM_INT:
		sens->val = tok->u.num_int;
		break;
	case LWJSON_TYPE_NUM_REAL:
		if (isnan(tok->u.num_real) || isinf(tok->u.num_real))
			goto out;
		sens->val = tok->u.num_real;
		break;
	default:
		goto out;
	}
	ret = 0;

out:
	if (IS_DEBUG(sens->ctx)) {
		if (!ret)
			hlog_info(RSENS_MODULE, "Found JSON key [%s], type %d, val %f", sens->key, (int)tok->type, sens->val);
		else
			hlog_info(RSENS_MODULE, "No valid JSON value for key [%s]", sens->key);
	}
	return ret;
}

static void rsens_data_cb(void *ctx, char *topic, char *data, int size, lwjson_t *lwjson)
{
	struct rsensor_t *sens = (struct rsensor_t *)ctx;
	int ret = -1;

	UNUSED(topic);

	if (!sens || !data || size < 1)
		return;

	if (sens->key) {
		ret = rsens_json_parse(sens, lwjson);
	} else {
		ret = sys_strtof(data, &sens->val);
	}

	if (!ret) {
		sens->last_data = time_ms_since_boot();
		sens->valid = true;
		if (IS_DEBUG(sens->ctx))
			hlog_info(RSENS_MODULE, "Got %d bytes data for %s on topic %s: [%s] %3.2f",
					  size, sens->name, sens->topic, data, sens->val);
	} else {
		sens->valid = false;
		if (IS_DEBUG(sens->ctx))
			hlog_info(RSENS_MODULE, "Got %d bytes data for %s on topic %s: [%s]: failed to parse data",
					  size, sens->name, sens->topic, data);
	}
}

static void rsens_run(void *context)
{
	struct rsens_context_t *ctx = (struct rsens_context_t *)context;
	int i;

	for (i = 0; i < ctx->count; i++) {
		if (!ctx->sensors[i])
			break;
		if (!ctx->sensors[i]->subscribed) {
			if (!mqtt_topic_listen(ctx->sensors[i]->topic, rsens_data_cb, ctx->sensors[i], ctx->sensors[i]->key ? true : false))
				ctx->sensors[i]->subscribed = true;
			return;
		}
	}
	if (IS_DEBUG(ctx))
		hlog_info(RSENS_MODULE, "Init completed");

	ctx->mod.run = NULL;
}

static bool rsens_init(struct rsens_context_t **ctx)
{
	char *name = NULL, *topic = NULL, *key = NULL;
	char *sensors_cfg = param_get(REMOTE_SENSOR);
	char *rest, *rest1, *tok, *ptok;
	int i;

	(*ctx) = NULL;
	if (!sensors_cfg || strlen(sensors_cfg) < 1)
		goto out_error;

	(*ctx) = calloc(1, sizeof(struct rsens_context_t));
	if ((*ctx) == NULL)
		goto out_error;

	rest = sensors_cfg;
	while ((tok = strtok_r(rest, ";", &rest))) {
		ptok = strtok_r(tok, ":", &rest1);
		if (!ptok || !rest1)
			continue;
		name = strdup(ptok);
		if (!name)
			goto out_error;
		ptok = strtok_r(rest1, ":", &rest1);
		if (!ptok) {
			free(name);
			continue;
		}
		topic = strdup(ptok);
		if (!topic)
			goto out_error;
		key = NULL;
		if (rest1) {
			key = strdup(rest1);
			if (!key)
				goto out_error;
		}

		(*ctx)->sensors[(*ctx)->count] = calloc(1, sizeof(struct rsensor_t));
		if (!(*ctx)->sensors[(*ctx)->count])
			goto out_error;
		(*ctx)->sensors[(*ctx)->count]->name = name;
		(*ctx)->sensors[(*ctx)->count]->topic = topic;
		(*ctx)->sensors[(*ctx)->count]->key = key;
		(*ctx)->sensors[(*ctx)->count]->ctx = (*ctx);
		name = NULL;
		topic = NULL;
		key = NULL;
		(*ctx)->count++;
		if ((*ctx)->count >= RSENS_MAX)
			break;
	}
	__rsens_context = (*ctx);
	hlog_info(RSENS_MODULE, "%d remote sensors initialized", (*ctx)->count);
	return true;

out_error:
	free(sensors_cfg);
	if ((*ctx)) {
		for (i = 0; i < (*ctx)->count; i++) {
			if (!(*ctx)->sensors[i])
				break;
			free((*ctx)->sensors[i]->name);
			free((*ctx)->sensors[i]->topic);
			free((*ctx)->sensors[i]->key);
			free((*ctx)->sensors[i]);
		}
		free((*ctx));
	}
	free(topic);
	free(name);
	free(key);
	return false;
}

static void rsens_debug_set(uint32_t debug, void *context)
{
	struct rsens_context_t *ctx = (struct rsens_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void rsens_register(void)
{
	struct rsens_context_t *ctx = NULL;

	if (!rsens_init(&ctx))
		return;

	ctx->mod.name = RSENS_MODULE;
	ctx->mod.run = rsens_run;
	ctx->mod.log = rsens_log;
	ctx->mod.debug = rsens_debug_set;
	ctx->mod.commands.description = "Remote Sensor";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}

/* API */
int rsensor_get_index(char *name)
{
	struct rsens_context_t *ctx = rsens_context_get();
	int i;

	if (!ctx)
		return -1;

	for (i = 0; i < ctx->count; i++) {
		if (!ctx->sensors[i])
			break;
		if (strlen(name) != strlen(ctx->sensors[i]->name))
			continue;
		if (strcmp(name, ctx->sensors[i]->name))
			continue;
		return i;
	}

	return -1;
}

int rsensor_get_value(int index, float *val)
{
	struct rsens_context_t *ctx = rsens_context_get();

	if (!ctx)
		return -1;

	if (index < 0 || index >= RSENS_MAX)
		return -1;
	if (!ctx->sensors[index])
		return -1;
	if (!ctx->sensors[index]->valid)
		return -1;
	if (val)
		*val = ctx->sensors[index]->val;
	return 0;
}
