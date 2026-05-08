// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"
#include "string.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#include "mqtt_internal.h"

#define MQTT_KEEPALIVE_S	100
#define IP_TIMEOUT_MS	20000
#define SEND_TIMEOUT_MS	 2000
// send discovery + subscribe on every hour; 60*60*1000
#define CONFIG_INTERVAL_MSEC	3600000
#define COMMAND_TOPIC_TEMPLATE	"%s/command"

#define DEF_SERVER_PORT	1883
#define DF_MAX_PPM	12

#define STATUS_TOPIC_TEMPLATE	"%s/status"

#define MAX_CONN_ERR		10
#define CONN_ERR_TIME_MSEC	120000 // 2 min

// MQTT_REQ_MAX_IN_FLIGHT

static struct mqtt_context_t *__mqtt_context;

struct mqtt_context_t *mqtt_context_get(void)
{
	return __mqtt_context;
}

bool mqtt_is_connected_ctx(struct mqtt_context_t *ctx)
{
	uint8_t ret;

	if (!ctx || !ctx->client)
		return false;

	LWIP_LOCK_START;
		ret = mqtt_client_is_connected(ctx->client);
	LWIP_LOCK_END;

	return ret;
}

bool mqtt_is_connected(void)
{
	return mqtt_is_connected_ctx(mqtt_context_get());
}

static void mqtt_config_send(struct mqtt_context_t *ctx)
{
	int sent = 0;
	uint64_t now;
	int ret = -1;

	if (!mqtt_is_connected_ctx(ctx))
		return;

	now = time_ms_since_boot();
	if (ctx->config.last_send == 0 ||
	    (now - ctx->config.last_send) > CONFIG_INTERVAL_MSEC) {
		ctx->config.discovery_dev = true;
		if (ctx->cmp_count >  0) {
			ctx->config.discovery_comp = true;
			ctx->discovery.send_idx = 0;
		}
		if (ctx->status_topic[0])
			ctx->config.status = true;
		if (ctx->commands.cmd_topic[0])
			ctx->config.subscribe = true;
	}

	if (ctx->config.status) {
		ret = mqtt_msg_send(ctx, ctx->status_topic, ONLINE_MSG);
		if (!ret) {
			ctx->config.status_send++;
			ctx->config.status = false;
			sent++;
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Send status message [%s] on [%s]", ONLINE_MSG, ctx->status_topic);
		}
		goto out;
	}
	if (ctx->config.discovery_dev) {
		ret = mqtt_msg_discovery_send_device(ctx);
		if (!ret)
			ctx->config.discovery_dev = false;
		goto out;
	}
	if (ctx->config.discovery_comp) {

		ret = mqtt_msg_discovery_send(ctx);
		if (!ret) {
			sent++;
			ctx->config.discovery_send++;
			ctx->discovery.send_idx++;
			if (ctx->config.discovery_send == ctx->cmp_count)
				hlog_info(MQTT_MODULE, "Send all %d discovery messages", ctx->config.discovery_send);
		}
		if (ctx->discovery.send_idx >= ctx->cmp_count) {
			ctx->discovery.send_idx = 0;
			ctx->config.discovery_comp = false;
		}
		goto out;
	}
	if (ctx->config.subscribe) {
		ret = mqtt_cmd_subscribe(ctx);
		if (!ret) {
			sent++;
			ctx->config.subscribe_send++;
			ctx->config.subscribe = false;
		}
	}
out:
	if (sent)
		ctx->config.last_send = now;
}

static void mqtt_hook(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	UNUSED(client);
	ctx->send_in_progress = 0;

	switch (status) {
	case MQTT_CONNECT_ACCEPTED:
		if (ctx->state != MQTT_CLIENT_CONNECTED) {
			ctx->connect_count++;
			LWIP_LOCK_START;
				mqtt_set_inpub_callback(client, mqtt_incoming_publish, mqtt_incoming_data, ctx);
			LWIP_LOCK_END;
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Connected to server %s", ctx->server_url);
		}
		ctx->state = MQTT_CLIENT_CONNECTED;
		ctx->config.discovery_send = 0;
		ctx->config.last_send = 0;
		ctx->send_err_count = 0;
		break;
	case MQTT_CONNECT_DISCONNECTED:
		if (ctx->state != MQTT_CLIENT_DISCONNECTED) {
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Disconnected from server %s", ctx->server_url);
		}
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		ctx->send_err_count = 0;
		break;
	case MQTT_CONNECT_TIMEOUT:
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Server timeout %s", ctx->server_url);
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		ctx->send_err_count = 0;
		break;
	case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
	case MQTT_CONNECT_REFUSED_IDENTIFIER:
	case MQTT_CONNECT_REFUSED_SERVER:
	case MQTT_CONNECT_REFUSED_USERNAME_PASS:
	case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
		hlog_info(MQTT_MODULE, "Connection refused from server %s -> %d", ctx->server_url, status);
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		ctx->send_err_count = 0;
		break;
	default:
		hlog_info(MQTT_MODULE, "Unknown state of the server %s -> %d", ctx->server_url, status);
		break;
	}
}

static void mqtt_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t  *)arg;

	UNUSED(hostname);
	memcpy(&(ctx->server_addr), ipaddr, sizeof(ip_addr_t));
	ctx->sever_ip_state = IP_RESOLVED;
}

#define LOG_STEP	20
static bool sys_mqtt_log_status(void *context)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t *)context;
	static int log_idx;
	int i;

	if (!mqtt_is_connected_ctx(ctx)) {
		hlog_info(MQTT_MODULE, "Not connected to a server, looking for %s ... connect count %d ",
				ctx->server_url, ctx->connect_count);
		return true;
	}

	if (!log_idx) {
		hlog_info(MQTT_MODULE, "Connected to server %s, publish rate limit %lldppm, connect count %d",
				ctx->server_url, ctx->max_ppm, ctx->connect_count);
		if (ctx->commands.cmd_topic[0])
			hlog_info(MQTT_MODULE, "Listen to topic [%s]", ctx->commands.cmd_topic);

		if (ctx->status_topic[0]) {
			hlog_info(MQTT_MODULE, "Sending status to [%s], sent %d",
					ctx->status_topic, ctx->config.status_send);
		} else {
			hlog_info(MQTT_MODULE, "No status is send.");
		}

		if (ctx->commands.cmd_topic[0]) {
			hlog_info(MQTT_MODULE, "Listen for commands on [%s], subscribed %d",
					ctx->commands.cmd_topic, ctx->config.subscribe_send);
		} else {
			hlog_info(MQTT_MODULE, "Do not listen for commands");
		}
		hlog_info(MQTT_MODULE, "Registered %d devices", ctx->cmp_count);
		hlog_info(MQTT_MODULE, "Sent %d discovery messages", ctx->config.discovery_send);
	}
	for (i = log_idx; i < ctx->cmp_count; i++) {
		hlog_info(MQTT_MODULE, "\t %s/%s %s\t[%s]",
				  ctx->components[i]->module,
				  ctx->components[i]->name,
				  ctx->components[i]->platform,
				  ctx->components[i]->state_topic);
		if ((i - log_idx) >= LOG_STEP)
			break;
	}

	if (i < ctx->cmp_count) {
		log_idx = i;
		return false;
	}

	log_idx = 0;
	return true;
}

static void sys_mqtt_reconnect(void *context)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t  *)context;

	ctx->send_err_count = 0;
	ctx->send_start = 0;
	if (ctx->state == MQTT_CLIENT_INIT)
		return;
	ctx->state = MQTT_CLIENT_DISCONNECTED;
	ctx->sever_ip_state = IP_NOT_RESOLEVED;

	if (ctx->client) {
		LWIP_LOCK_START;
			mqtt_disconnect(ctx->client);
		LWIP_LOCK_END;
	}
	hlog_info(MQTT_MODULE, "Disconnected form %s", ctx->server_url);
}

static bool mqtt_connect(struct mqtt_context_t *ctx)
{
	uint64_t now;
	int ret;

	if (!WIFI_IS_CONNECTED) {
		if (mqtt_is_connected_ctx(ctx)) {
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "No WiFi, force reconnection");
			sys_mqtt_reconnect(ctx);
		}
		return false;
	}

	now = time_ms_since_boot();
	if (mqtt_is_connected_ctx(ctx)) {
		if (ctx->send_err_count >= MAX_CONN_ERR &&
			(now - ctx->send_start) >= CONN_ERR_TIME_MSEC) {
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "%d packet send errors in %s sec , force reconnection",
						  ctx->send_err_count, (now - ctx->send_start)/1000);
			sys_mqtt_reconnect(ctx);
			return false;
		}
		return true;
	}

	if (ctx->state == MQTT_CLIENT_CONNECTING) {
		if ((now - ctx->last_send) < IP_TIMEOUT_MS)
			return false;
		if (ctx->client) {
			LWIP_LOCK_START;
				mqtt_disconnect(ctx->client);
			LWIP_LOCK_END;
		}
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		ctx->sever_ip_state = IP_NOT_RESOLEVED;
		hlog_info(MQTT_MODULE, "Connect to %s timeout", ctx->server_url);
	}

	switch (ctx->sever_ip_state) {
	case IP_NOT_RESOLEVED:
		LWIP_LOCK_START;
			ret = dns_gethostbyname(ctx->server_url, &ctx->server_addr, mqtt_server_found, ctx);
		LWIP_LOCK_END;
		if (ret == ERR_INPROGRESS) {
			ctx->last_send = time_ms_since_boot();
			ctx->sever_ip_state = IP_RESOLVING;
			hlog_info(MQTT_MODULE, "Resolving %s ...", ctx->server_url);
			return false;
		} else if (ret == ERR_OK) {
			ctx->sever_ip_state = IP_RESOLVED;
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "MQTT server resolved");
		} else {
			return false;
		}
		break;
	case IP_RESOLVED:
		break;
	case IP_RESOLVING:
		if ((now - ctx->last_send) > IP_TIMEOUT_MS) {
			ctx->sever_ip_state = IP_NOT_RESOLEVED;
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Server resolving timeout");
		}
		return false;
	default:
		return false;
	}

	if (ctx->state == MQTT_CLIENT_INIT)
		hlog_info(MQTT_MODULE, "Connecting to MQTT server %s (%s) ...", ctx->server_url, inet_ntoa(ctx->server_addr));

	LWIP_LOCK_START;
		if (ctx->client)
			mqtt_disconnect(ctx->client);
		else
			ctx->client = mqtt_client_new();
	LWIP_LOCK_END;
	if (!ctx->client)
		return false;
	ctx->state = MQTT_CLIENT_CONNECTING;

	LWIP_LOCK_START;
		ret = mqtt_client_connect(ctx->client, &ctx->server_addr,
								  ctx->server_port, mqtt_hook, ctx, &ctx->client_info);
	LWIP_LOCK_END;
	if (!ret) {
		ctx->last_send = time_ms_since_boot();
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Connected to server %s", ctx->server_url);
	} else {
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		hlog_info(MQTT_MODULE, "Connecting to MQTT server %s (%s) failed: %d",
				  ctx->server_url, inet_ntoa(ctx->server_addr), ret);
	}

	return false;
}

static void sys_mqtt_run(void *context)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t  *)context;

	if (!mqtt_connect(ctx))
		return;
	if (mqtt_incoming_ready(ctx))
		return;
	mqtt_config_send(ctx);
}

static int mqtt_get_config(struct mqtt_context_t  **ctx)
{
	char *endp, *topc, *usr;
	char *str, *tok, *rest;
	int res;

	topc = USER_PRAM_GET(MQTT_TOPIC);
	endp = USER_PRAM_GET(MQTT_SERVER_ENDPOINT);
	usr = USER_PRAM_GET(MQTT_USER);
	if (!endp || !topc || !usr) {
		free(topc);
		free(endp);
		free(usr);
		return -1;
	}
	(*ctx) = (struct mqtt_context_t  *)calloc(1, sizeof(struct mqtt_context_t));
	if (!(*ctx))
		return -1;

	(*ctx)->state_topic = topc;

	str = endp;
	rest = str;
	tok = strtok_r(rest, ":", &rest);
	(*ctx)->server_url = strdup(tok);
	if (rest)
		(*ctx)->server_port = atoi(rest);
	else
		(*ctx)->server_port = DEF_SERVER_PORT;
	free(str);

	rest = usr;
	tok = strtok_r(rest, ";", &rest);
	(*ctx)->client_info.client_user = tok;
	(*ctx)->client_info.client_pass = rest;

	if (MQTT_RATE_PPM_len > 1) {
		str = USER_PRAM_GET(MQTT_RATE_PPM);
		res = (int)strtol(str, NULL, 10);
		(*ctx)->max_ppm = res;
		free(str);
	} else {
		(*ctx)->max_ppm = DF_MAX_PPM;
	}

	return 0;
}

static bool sys_mqtt_init(struct mqtt_context_t  **ctx)
{
	if (mqtt_get_config(ctx))
		return false;

	(*ctx)->state = MQTT_CLIENT_INIT;
	snprintf((*ctx)->status_topic, MQTT_MAX_TOPIC_SIZE, STATUS_TOPIC_TEMPLATE, (*ctx)->state_topic);
	snprintf((*ctx)->commands.cmd_topic, MQTT_MAX_TOPIC_SIZE, COMMAND_TOPIC_TEMPLATE, (*ctx)->state_topic);
	(*ctx)->client_info.client_id = system_get_hostname();
	(*ctx)->client_info.keep_alive = MQTT_KEEPALIVE_S;
	(*ctx)->client_info.will_topic = (*ctx)->status_topic;
	(*ctx)->client_info.will_msg = OFFLINE_MSG;
	(*ctx)->client_info.will_qos = 1;
	(*ctx)->client_info.will_retain = 1;
	(*ctx)->send_in_progress = 0;
	(*ctx)->max_payload_size = MQTT_OUTPUT_RINGBUF_SIZE - MQTT_MAX_TOPIC_SIZE;
	(*ctx)->cmd_ctx.type = CMD_CTX_MQTT;

	__mqtt_context = (*ctx);
	return true;
}

static void sys_mqtt_debug_set(uint32_t lvl, void *context)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)context;

	ctx->debug = lvl;
}

void sys_mqtt_register(void)
{
	struct mqtt_context_t  *ctx = NULL;

	if (!sys_mqtt_init(&ctx))
		return;

	ctx->mod.name = MQTT_MODULE;
	ctx->mod.run = sys_mqtt_run;
	ctx->mod.log = sys_mqtt_log_status;
	ctx->mod.debug = sys_mqtt_debug_set;
	ctx->mod.reconnect = sys_mqtt_reconnect;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
