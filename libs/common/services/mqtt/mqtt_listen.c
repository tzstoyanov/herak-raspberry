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

	if (ctx->commands.cmd_msg_in_progress ||
		tot_len >= MQTT_OUTPUT_RINGBUF_SIZE)
		return;

	if (strcmp(topic, ctx->commands.cmd_topic))
		return;

	ctx->commands.cmd_msg_in_progress = true;
	ctx->commands.cmd_msg_ready = false;
	ctx->commands.cmd_msg_size = 0;
}

void mqtt_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	if (!ctx->commands.cmd_msg_in_progress || ctx->commands.cmd_msg_ready)
		return;
	if ((ctx->commands.cmd_msg_size + len) >= (MQTT_OUTPUT_RINGBUF_SIZE-1))
		return;

	memcpy(ctx->commands.cmd_msg + ctx->commands.cmd_msg_size, data, len);
	ctx->commands.cmd_msg_size += len;

	if (flags & MQTT_DATA_FLAG_LAST) {
		ctx->commands.cmd_msg[ctx->commands.cmd_msg_size] = '\0';
		ctx->commands.cmd_msg_ready = true;
	}

}

bool mqtt_incoming_ready(struct mqtt_context_t *ctx)
{

	if (!ctx->commands.cmd_msg_ready)
		return false;
#ifdef HAVE_COMMANDS
	if (ctx->commands.cmd_msg_size >= 2)
		cmd_exec(&ctx->cmd_ctx, ctx->commands.cmd_msg);
#endif
	ctx->commands.cmd_msg_in_progress = false;
	ctx->commands.cmd_msg_ready = false;
	ctx->commands.cmd_msg_size = 0;

	return true;
}

int mqtt_cmd_subscribe(struct mqtt_context_t *ctx)
{
	err_t ret = -1;

	if (ctx->state != MQTT_CLIENT_CONNECTED)
		goto out;

	LWIP_LOCK_START;
		ret = mqtt_subscribe(ctx->client, ctx->commands.cmd_topic, MQTT_QOS, NULL, NULL);
	LWIP_LOCK_END;

	if (!ret && IS_DEBUG(ctx))
		hlog_info(MQTT_MODULE, "Subscribed to MQTT topic [%s]", ctx->commands.cmd_topic);

out:
	return ret;
}
