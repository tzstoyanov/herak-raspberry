// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/inet.h"
#include "string.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "mqtt_internal.h"

void mqtt_incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;
	int i, j;

	for (i = 0; i < MQTT_LISTEN_BUFFERS; i++)
		if (!ctx->listen.buffers[i].in_progress)
			break;
	if (i >= MQTT_LISTEN_BUFFERS || tot_len >= MQTT_OUTPUT_RINGBUF_SIZE)
		return;
	j = 0;
	while (ctx->listen.topics[j] && j < MQTT_MAX_TOPICS) {
		if (strlen(topic) == strlen(ctx->listen.topics[j]->topic) &&
			!strcmp(topic, ctx->listen.topics[j]->topic))
			break;
		j++;
	}
	if (j >= MQTT_MAX_TOPICS || !ctx->listen.topics[j])
		return;
	ctx->listen.buffers[i].in_progress = true;
	ctx->listen.buffers[i].ready = false;
	ctx->listen.buffers[i].size = 0;
	ctx->listen.buffers[i].tot_len = tot_len;
	ctx->listen.buffers[i].topic = ctx->listen.topics[j];
	ctx->listen.current = &(ctx->listen.buffers[i]);
}

void mqtt_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	if (!ctx->listen.current || !ctx->listen.current->in_progress)
		return;

	if ((ctx->listen.current->size + len) < (MQTT_OUTPUT_RINGBUF_SIZE-1)) {
		memcpy(ctx->listen.current->msg + ctx->listen.current->size, data, len);
		ctx->listen.current->size += len;
	}

	if (flags & MQTT_DATA_FLAG_LAST) {
		ctx->listen.current->msg[ctx->listen.current->size] = '\0';
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Got topic [%s] %d bytes: [%s]",
					  ctx->listen.current->topic->topic,
					  ctx->listen.current->size, ctx->listen.current->msg);
		ctx->listen.current->ready = true;
		ctx->listen.current = NULL;
	}
}

bool mqtt_incoming_ready(struct mqtt_context_t *ctx)
{
	struct mqtt_topic_cb_t *cb;
	bool json_parsed = false;
	bool json_init = false;
	lwjsonr_t ret;
	int i;

	for (i = 0; i < MQTT_LISTEN_BUFFERS; i++)
		if (ctx->listen.buffers[i].ready)
			break;

	if (i >= MQTT_LISTEN_BUFFERS)
		return false;
	if (ctx->listen.buffers[i].topic->json) {
		ret = lwjson_init(&(ctx->listen.buffers[i].lwjson), ctx->listen.buffers[i].json_tokens,
					LWJSON_ARRAYSIZE(ctx->listen.buffers[i].json_tokens));
		if (ret == lwjsonOK) {
			json_init = true;
			ret = lwjson_parse_ex(&(ctx->listen.buffers[i].lwjson),
									ctx->listen.buffers[i].msg, ctx->listen.buffers[i].size);
			if (ret == lwjsonOK)
				json_parsed = true;
		}
	}
	cb = ctx->listen.buffers[i].topic->hooks;
	while (cb) {
		cb->func(cb->arg, ctx->listen.buffers[i].topic->topic,
				 ctx->listen.buffers[i].msg, ctx->listen.buffers[i].size,
				 json_parsed ? &(ctx->listen.buffers[i].lwjson) : NULL);
		cb = cb->next;
	}

	ctx->listen.buffers[i].in_progress = false;
	ctx->listen.buffers[i].ready = false;
	ctx->listen.buffers[i].tot_len = 0;
	ctx->listen.buffers[i].size = 0;
	ctx->listen.buffers[i].topic = NULL;

	if (json_init)
		lwjson_free(&(ctx->listen.buffers[i].lwjson));

	return true;
}

void mqtt_subscribe_all(struct mqtt_context_t *ctx)
{
	int i;

	i = 0;
	while (ctx->listen.topics[i] && i <= MQTT_MAX_TOPICS) {
		ctx->listen.topics[i]->subscribed = false;
		i++;
	}
	ctx->config.subscribe = 0;
}

int mqtt_send_subscribe(struct mqtt_context_t *ctx)
{
	err_t ret = -1;
	int i;

	if (ctx->state != MQTT_CLIENT_CONNECTED)
		goto out;

	i = 0;
	while (ctx->listen.topics[i] && i <= MQTT_MAX_TOPICS) {
		if (!ctx->listen.topics[i]->subscribed)
			break;
		i++;
	}
	if (i >= MQTT_MAX_TOPICS || !ctx->listen.topics[i] || strlen(ctx->listen.topics[i]->topic) < 1)
		goto out;

	LWIP_LOCK_START;
		ret = mqtt_subscribe(ctx->client, ctx->listen.topics[i]->topic, MQTT_QOS, NULL, NULL);
	LWIP_LOCK_END;

	if (!ret) {
		ctx->listen.topics[i]->subscribed = true;
		ctx->config.subscribe++;
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Subscribed to MQTT topic [%s]", ctx->listen.topics[i]->topic);
	}

out:
	return ret;
}

/* API */
int mqtt_topic_listen(char *topic, mqtt_topic_cb_t func, void *context, bool json)
{
	struct mqtt_context_t *ctx = mqtt_context_get();
	struct mqtt_topic_cb_t *hook;
	int i;

	if (!ctx)
		return -1;
	i = 0;
	while (ctx->listen.topics[i] && i < MQTT_MAX_TOPICS) {
		if (strlen(topic) == strlen(ctx->listen.topics[i]->topic) &&
			!strcmp(topic, ctx->listen.topics[i]->topic))
			break;
		i++;
	}
	if (i >= MQTT_MAX_TOPICS || !ctx->listen.topics[i]) {
		i = ctx->listen.count;
		if (i >= MQTT_MAX_TOPICS || ctx->listen.topics[i])
			return -1;
		ctx->listen.topics[i] = calloc(1, sizeof(mqtt_topic_t));
		if (!ctx->listen.topics[i])
			return -1;
		strncpy(ctx->listen.topics[i]->topic, topic, MQTT_MAX_TOPIC_SIZE - 1);
		ctx->listen.count++;
	}
	hook = calloc(1, sizeof(struct mqtt_topic_cb_t));
	if (!hook)
		return -1;
	hook->func = func;
	hook->arg = context;
	hook->next = ctx->listen.topics[i]->hooks;
	if (json)
		ctx->listen.topics[i]->json = true;
	ctx->listen.topics[i]->hooks = hook;
	return 0;
}
