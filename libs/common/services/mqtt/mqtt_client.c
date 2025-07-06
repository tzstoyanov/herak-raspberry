// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
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

#define MQTT_MODULE	"mqtt"
#define MQTT_KEEPALIVE_S	100
#define IP_TIMEOUT_MS	20000
#define SEND_TIMEOUT_MS	 2000
// send discovery + subscribe on every hour; 60*60*1000
#define CONFIG_INTERVAL_MSEC	3600000
#define COMMAND_TOPIC_TEMPLATE	"%s/command"

#define MSEC_INSEC	60000ULL

#define IS_DEBUG(C)	((C)->debug)

#define DEF_SERVER_PORT	1883
#define DF_MIN_PKT_DELAY_MS	5000ULL

#define MQTT_QOS		0
#define MQTT_RETAIN		1

#define STATUS_TOPIC_TEMPLATE	"%s/status"
#define ONLINE_MSG				"online"
#define OFFLINE_MSG				"offline"

//#define MQTT_CLIENT_LOCK(C)		do { if (C) mutex_enter_blocking(&((C)->lock)); } while (0)
//#define MQTT_CLIENTL_UNLOCK(C)	do { if (C) mutex_exit(&((C)->lock)); } while (0)

#define MQTT_CLIENT_LOCK(C)		{ (void)(C); }
#define MQTT_CLIENTL_UNLOCK(C)	{ (void)(C); }

#define MQTT_DISCOVERY_MAX_COUNT	256
#define MQTT_DISCOVERY_BUFF_SIZE	640
#define MQTT_MAX_TOPIC_SIZE	96

#define MAX_CONN_ERR		10
#define CONN_ERR_TIME_MSEC	120000 // 2 min

// MQTT_REQ_MAX_IN_FLIGHT

enum mqtt_client_state_t {
	MQTT_CLIENT_INIT = 0,
	MQTT_CLIENT_DISCONNECTED,
	MQTT_CLIENT_CONNECTING,
	MQTT_CLIENT_CONNECTED
};

typedef struct {
	char cmd_topic[MQTT_MAX_TOPIC_SIZE];
	int cmd_msg_size;
	bool cmd_msg_in_progress;
	bool cmd_msg_ready;
	char cmd_msg[MQTT_OUTPUT_RINGBUF_SIZE];
} mqtt_commads_t;

typedef struct {
	char buff[MQTT_DISCOVERY_BUFF_SIZE];
	char topic[MQTT_MAX_TOPIC_SIZE];
	uint16_t send_idx;
} mqtt_discovery_context_t;

typedef struct {
	uint64_t last_send;
	uint32_t discovery_send;
	uint32_t subscribe_send;
	uint32_t status_send;
	bool discovery_dev;
	bool discovery_comp;
	bool subscribe;
	bool status;
} mqtt_config_send_context_t;

 struct mqtt_context_t {
	sys_module_t mod;
	char *server_url;
	char *state_topic;
	char status_topic[MQTT_MAX_TOPIC_SIZE];
	cmd_run_context_t cmd_ctx;
	mqtt_commads_t	commands;
	mqtt_component_t *components[MQTT_DISCOVERY_MAX_COUNT];
	uint16_t cmp_count;
	mqtt_discovery_context_t discovery;
	mqtt_config_send_context_t config;
	int server_port;
	uint64_t mqtt_min_delay;
	uint32_t max_payload_size;
	enum mqtt_client_state_t state;
	ip_addr_t server_addr;
	ip_resolve_state_t sever_ip_state;
	mqtt_client_t *client;
	struct mqtt_connect_client_info_t client_info;
	bool data_send;
	int send_in_progress;
	uint64_t send_start;
	uint64_t last_send;
	mutex_t lock;
	uint32_t connect_count;
	uint32_t debug;
	uint8_t send_err_count;
};

static struct mqtt_context_t *__mqtt_context;

static struct mqtt_context_t *mqtt_context_get(void)
{
	return __mqtt_context;
}

static bool mqtt_is_connected_ctx(struct mqtt_context_t *ctx)
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

static void mqtt_incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	MQTT_CLIENT_LOCK(ctx);
	if ( ctx->commands.cmd_msg_in_progress ||
		 tot_len >= MQTT_OUTPUT_RINGBUF_SIZE)
		goto out;

	if (strcmp(topic, ctx->commands.cmd_topic))
		goto out;

	ctx->commands.cmd_msg_in_progress = true;
	ctx->commands.cmd_msg_ready = false;
	ctx->commands.cmd_msg_size = 0;

out:
	MQTT_CLIENTL_UNLOCK(ctx);
}

static void mqtt_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	MQTT_CLIENT_LOCK(ctx);

	if (!ctx->commands.cmd_msg_in_progress || ctx->commands.cmd_msg_ready)
		goto out;
	if ((ctx->commands.cmd_msg_size + len) >= (MQTT_OUTPUT_RINGBUF_SIZE-1))
		goto out;

	memcpy(ctx->commands.cmd_msg + ctx->commands.cmd_msg_size, data, len);
	ctx->commands.cmd_msg_size += len;

	if (flags & MQTT_DATA_FLAG_LAST) {
		ctx->commands.cmd_msg[ctx->commands.cmd_msg_size] = '\0';
		ctx->commands.cmd_msg_ready = true;
	}

out:
	MQTT_CLIENTL_UNLOCK(ctx);
}

static bool mqtt_incoming_ready(struct mqtt_context_t *ctx)
{
	bool ret = false;

	MQTT_CLIENT_LOCK(ctx);
	if (!ctx->commands.cmd_msg_ready)
		goto out;
#ifdef HAVE_COMMANDS
	MQTT_CLIENTL_UNLOCK(ctx);
	if (ctx->commands.cmd_msg_size >= 2)
		cmd_exec(&ctx->cmd_ctx, ctx->commands.cmd_msg);
	MQTT_CLIENT_LOCK(ctx);
#endif
	ctx->commands.cmd_msg_in_progress = false;
	ctx->commands.cmd_msg_ready = false;
	ctx->commands.cmd_msg_size = 0;
	ret = true;

out:
	MQTT_CLIENTL_UNLOCK(ctx);
	return ret;
}

static int mqtt_cmd_subscribe(struct mqtt_context_t *ctx)
{
	err_t ret = -1;

	MQTT_CLIENT_LOCK(ctx);
	if (ctx->state != MQTT_CLIENT_CONNECTED)
		goto out;

	MQTT_CLIENTL_UNLOCK(ctx);
	LWIP_LOCK_START;
		ret = mqtt_subscribe(ctx->client, ctx->commands.cmd_topic, MQTT_QOS, NULL, NULL);
	LWIP_LOCK_END;
	MQTT_CLIENT_LOCK(ctx);

	if (!ret && IS_DEBUG(ctx))
		hlog_info(MQTT_MODULE, "Subscribed to MQTT topic [%s]", ctx->commands.cmd_topic);

out:
	MQTT_CLIENTL_UNLOCK(ctx);
	return ret;
}

static void mqtt_publish_cb(void *arg, err_t result)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	UNUSED(result);
	MQTT_CLIENT_LOCK(ctx);
		ctx->send_in_progress--;
		if (result != ERR_OK)
			ctx->send_err_count++;
	MQTT_CLIENTL_UNLOCK(ctx);
}

static int mqtt_msg_send(struct mqtt_context_t *ctx, char *topic, char *message)
{
	err_t err;

	if (!mqtt_is_connected_ctx(ctx))
		return -1;

	LWIP_LOCK_START;
		err = mqtt_publish(ctx->client, topic, message, strlen(message),
						   MQTT_QOS, MQTT_RETAIN, mqtt_publish_cb, ctx);
	LWIP_LOCK_END;

	if (err == ERR_OK) {
		MQTT_CLIENT_LOCK(ctx);
			ctx->send_err_count = 0;
			ctx->send_in_progress++;
			ctx->send_start = time_ms_since_boot();
		MQTT_CLIENTL_UNLOCK(ctx);
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Published %d bytes to [%s]", strlen(message), topic);
	} else {
		MQTT_CLIENT_LOCK(ctx);
			ctx->send_err_count++;
		MQTT_CLIENTL_UNLOCK(ctx);
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Failed to publish the message: %d / %d", err, ctx->send_in_progress);
		return -1;
	}

	return 0;
}

#define ADD_STR(F, ...) {\
	int ret = snprintf(ctx->discovery.buff + count, size, F, __VA_ARGS__);\
	count += ret; size -= ret;\
	if (size < 0)	\
		return size; \
	}
// https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
// https://www.home-assistant.io/integrations/mqtt/#discovery-examples-with-component-discovery
static int mqtt_discovery_generate_component(struct mqtt_context_t *ctx, mqtt_component_t *component)
{
	int size = MQTT_DISCOVERY_BUFF_SIZE;
	int count = 0;
	int ret;

	ctx->discovery.buff[0] = 0;
	ret = snprintf(ctx->discovery.topic, MQTT_MAX_TOPIC_SIZE, "homeassistant/%s/%s_%s_%s/config",
				   component->platform, ctx->state_topic, component->module, component->name);
	if (ret <= 0)
		return -1;

	/* Device section */
	ADD_STR("%s", "{\"device\":{");
		ADD_STR("\"identifiers\": [\"%s\"]", ctx->client_info.client_id);
		ADD_STR(",\"name\": \"%s\"", ctx->client_info.client_id);
	ADD_STR("%s", "}");
	if (component->dev_class)
		ADD_STR(",\"device_class\": \"%s\"", component->dev_class);
	if (component->unit)
		ADD_STR(",\"unit_of_measurement\": \"%s\"", component->unit);
	ADD_STR(",\"value_template\": \"%s\"", component->value_template);
	ADD_STR(",\"name\": \"%s_%s\"", component->module, component->name);
	ADD_STR(",\"unique_id\": \"%s_%s_%s\"",
				ctx->client_info.client_id, component->module, component->name);
	ADD_STR(",\"state_topic\": \"%s\"", component->state_topic);
	ADD_STR(",\"json_attributes_topic\": \"%s/%s/%s/status\"", ctx->state_topic, component->module, component->name);
	ADD_STR(",\"json_attributes_template\": \"%s\"", "{{ value_json | tojson }}");
	if (component->payload_on)
		ADD_STR(",\"payload_on\": \"%s\"", component->payload_on);
	if (component->payload_off)
		ADD_STR(",\"payload_off\": \"%s\"", component->payload_off);
	ADD_STR("%s", "}");

	return size;
}

static int mqtt_discovery_generate_device(struct mqtt_context_t *ctx)
{
	int size = MQTT_DISCOVERY_BUFF_SIZE;
	int count = 0;
	int ret;

	ctx->discovery.buff[0] = 0;
	ret = snprintf(ctx->discovery.topic, MQTT_MAX_TOPIC_SIZE, "homeassistant/device/%s/config",
				   ctx->state_topic);
	if (ret <= 0)
		return -1;

	ADD_STR("%s", "{\"device\":{");
		ADD_STR("\"identifiers\": [\"%s\"]", ctx->client_info.client_id);
		ADD_STR(",\"name\": \"%s\"", ctx->client_info.client_id);
	ADD_STR("%s", "}");
	ADD_STR("%s", ",\"origin\":{");
		ADD_STR("\"name\": \"%s\"", ctx->client_info.client_id);
#ifdef HAVE_SYS_WEBSERVER
		if (webserv_port())
			ADD_STR(",\"url\": \"http://%s:%d/help\"",
					inet_ntoa(cyw43_state.netif[0].ip_addr), webserv_port());
#endif /* HAVE_SYS_WEBSERVER */
	ADD_STR("%s", "}");
	ADD_STR("%s", ",\"components\":{");
		ADD_STR("\"%s-%s\": {", ctx->client_info.client_id, "device");
			ADD_STR("\"platform\": \"%s\"", "binary_sensor");
			ADD_STR(",\"device_class\": \"%s\"", "connectivity");
			ADD_STR(",\"name\": \"%s_%s\"", ctx->client_info.client_id, "device_link");
			ADD_STR(",\"unique_id\": \"%s_%s\"", ctx->client_info.client_id, "device_link");
			ADD_STR(",\"payload_on\": \"%s\"", ONLINE_MSG);
			ADD_STR(",\"payload_off\": \"%s\"", OFFLINE_MSG);
		ADD_STR("%s", "}}");
	ADD_STR(",\"state_topic\": \"%s\"", ctx->status_topic);
	ADD_STR(",\"availability_topic\": \"%s\"", ctx->status_topic);
	ADD_STR(",\"payload_available\": \"%s\"", ONLINE_MSG);
	ADD_STR(",\"payload_not_available\": \"%s\"", OFFLINE_MSG);
	ADD_STR("%s", "}");

	return size;
}

static int mqtt_msg_discovery_send_device(struct mqtt_context_t *ctx)
{
	int ret = -1;
	int msize;

	msize = mqtt_discovery_generate_device(ctx);
	if (msize > 0)
		ret = mqtt_msg_send(ctx, ctx->discovery.topic, ctx->discovery.buff);

	if (!ret) {
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Send %d bytes device discovery message",
					  strlen(ctx->discovery.buff));
	} else {
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Failed to publish %d/%d bytes device discovery message",
					  strlen(ctx->discovery.buff), msize);
	}
	return ret;
}

static int mqtt_msg_discovery_send(struct mqtt_context_t *ctx)
{
	mqtt_component_t *comp;
	int ret = -1;
	int msize;

	if (ctx->discovery.send_idx >= ctx->cmp_count)
		return -1;

	MQTT_CLIENT_LOCK(ctx);
		comp = ctx->components[ctx->discovery.send_idx];
	MQTT_CLIENTL_UNLOCK(ctx);
	msize = mqtt_discovery_generate_component(ctx, comp);
	if (msize > 0)
		ret = mqtt_msg_send(ctx, ctx->discovery.topic, ctx->discovery.buff);

	if (!ret) {
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Send %d bytes discovery message of %s/%s",
					  strlen(ctx->discovery.buff), comp->module, comp->name);
	} else {
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Failed to publish %d/%d bytes discovery message",
					  strlen(ctx->discovery.buff), msize);
	}

	return ret;
}

static void mqtt_config_send(struct mqtt_context_t *ctx)
{
	int sent = 0;
	uint64_t now;
	int ret = -1;

	if (!mqtt_is_connected_ctx(ctx))
		return;

	now = time_ms_since_boot();
	MQTT_CLIENT_LOCK(ctx);
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
	MQTT_CLIENTL_UNLOCK(ctx);

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
	if (sent) {
		MQTT_CLIENT_LOCK(ctx);
			ctx->config.last_send = now;
		MQTT_CLIENTL_UNLOCK(ctx);
	}
}

static void mqtt_hook(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	UNUSED(client);
	MQTT_CLIENT_LOCK(ctx);
		ctx->send_in_progress = 0;
	MQTT_CLIENTL_UNLOCK(ctx);

	switch (status) {
	case MQTT_CONNECT_ACCEPTED:
		if (ctx->state != MQTT_CLIENT_CONNECTED) {
			MQTT_CLIENT_LOCK(ctx);
				ctx->connect_count++;
			MQTT_CLIENTL_UNLOCK(ctx);
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
	MQTT_CLIENT_LOCK(ctx);
		memcpy(&(ctx->server_addr), ipaddr, sizeof(ip_addr_t));
		ctx->sever_ip_state = IP_RESOLVED;
	MQTT_CLIENTL_UNLOCK(ctx);
}

bool mqtt_is_discovery_sent(void)
{
	struct mqtt_context_t *ctx = mqtt_context_get();
	bool ret = false;

	if (!ctx)
		return false;

	MQTT_CLIENT_LOCK(ctx);
		if (ctx->client &&
			ctx->config.discovery_send >= ctx->cmp_count)

			ret = true;
	MQTT_CLIENTL_UNLOCK(ctx);

	return ret;
}

#define LOG_STEP	2
static bool sys_mqtt_log_status(void *context)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t *)context;
	int i;

	if (!mqtt_is_connected_ctx(ctx)) {
		hlog_info(MQTT_MODULE, "Not connected to a server, looking for %s ... connect count %d ",
				ctx->server_url, ctx->connect_count);
		return true;
	}
	hlog_info(MQTT_MODULE, "Connected to server %s, publish rate limit %lldppm, connect count %d",
			ctx->server_url, MSEC_INSEC/ctx->mqtt_min_delay, ctx->connect_count);
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
	for (i = 0; i < ctx->cmp_count; i++) {
		hlog_info(MQTT_MODULE, "\t %s/%s %s\t[%s]",
				  ctx->components[i]->module,
				  ctx->components[i]->name,
				  ctx->components[i]->platform,
				  ctx->components[i]->state_topic);
	}

	return true;
}

int mqtt_msg_publish(char *topic, char *message, bool force)
{
	struct mqtt_context_t *ctx = mqtt_context_get();
	char *topic_str;
	uint64_t now;
	int ret;

	UNUSED(force);

	if (!ctx)
		return -1;

	if (ctx->state != MQTT_CLIENT_CONNECTED)
		return -1;

	topic_str = topic ? topic : ctx->state_topic;
	if (strlen(message) > ctx->max_payload_size) {
		hlog_info(MQTT_MODULE, "Message too big: %d, max payload is %d", strlen(message), ctx->max_payload_size);
		return -1;
	}

	ctx->data_send = true;
	/* Rate limit the packets */
	now = time_ms_since_boot();
	if ((now - ctx->last_send) < ctx->mqtt_min_delay)
		ctx->data_send = false;
	if (!ctx->data_send && ctx->last_send)
		return -1;

	ret = mqtt_msg_send(ctx, topic_str, message);
	if (!ret) {
		MQTT_CLIENT_LOCK(ctx);
			ctx->data_send = false;
			ctx->last_send = time_ms_since_boot();
		MQTT_CLIENTL_UNLOCK(ctx);
	}
	return ret;
}

int mqtt_msg_component_publish(mqtt_component_t *component, char *message)
{
	int ret;

	if (!mqtt_is_discovery_sent())
		return -1;

	ret = mqtt_msg_publish(component->state_topic, message, component->force);
	if (!ret) {
		component->force = false;
		component->last_send = time_ms_since_boot();
	}

	return ret;
}

static void sys_mqtt_reconnect(void *context)
{
	struct mqtt_context_t  *ctx = (struct mqtt_context_t  *)context;
	mqtt_client_t *clnt = NULL;

	MQTT_CLIENT_LOCK(ctx);
		ctx->send_err_count = 0;
		ctx->send_start = 0;
		if (ctx->state == MQTT_CLIENT_INIT) {
			MQTT_CLIENTL_UNLOCK(ctx);
			return;
		}
		ctx->state = MQTT_CLIENT_DISCONNECTED;
		ctx->sever_ip_state = IP_NOT_RESOLEVED;
		clnt = ctx->client;
		ctx->client = NULL;
	MQTT_CLIENTL_UNLOCK(ctx);
	if (clnt) {
		LWIP_LOCK_START;
			mqtt_disconnect(clnt);
			mqtt_client_free(clnt);
		LWIP_LOCK_END;
	}
	MQTT_CLIENT_LOCK(ctx);
		hlog_info(MQTT_MODULE, "Disconnected form %s", ctx->server_url);
	MQTT_CLIENTL_UNLOCK(ctx);
}

static bool mqtt_connect(struct mqtt_context_t *ctx)
{
	enum mqtt_client_state_t st;
	mqtt_client_t *clnt = NULL;
	ip_resolve_state_t res;
	uint64_t last_send;
	uint64_t now;
	int ret;

	if (!wifi_is_connected()) {
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

	MQTT_CLIENT_LOCK(ctx);
		st = ctx->state;
		last_send = ctx->last_send;
	MQTT_CLIENTL_UNLOCK(ctx);

	if (st == MQTT_CLIENT_CONNECTING) {
		if ((now - last_send) < IP_TIMEOUT_MS)
			return false;
		if (ctx->client) {
			LWIP_LOCK_START;
				mqtt_disconnect(ctx->client);
				mqtt_client_free(ctx->client);
			LWIP_LOCK_END;
			ctx->client = NULL;
		}
		MQTT_CLIENT_LOCK(ctx);
			ctx->state = MQTT_CLIENT_DISCONNECTED;
			ctx->sever_ip_state = IP_NOT_RESOLEVED;
		MQTT_CLIENTL_UNLOCK(ctx);

		hlog_info(MQTT_MODULE, "Connect to %s timeout", ctx->server_url);
	}

	MQTT_CLIENT_LOCK(ctx);
		res = ctx->sever_ip_state;
	MQTT_CLIENTL_UNLOCK(ctx);

	switch (res) {
	case IP_NOT_RESOLEVED:
		LWIP_LOCK_START;
			ret = dns_gethostbyname(ctx->server_url, &ctx->server_addr, mqtt_server_found, ctx);
		LWIP_LOCK_END;
		if (ret == ERR_INPROGRESS) {
			hlog_info(MQTT_MODULE, "Resolving %s ...", ctx->server_url);
			MQTT_CLIENT_LOCK(ctx);
				ctx->last_send = time_ms_since_boot();
				ctx->sever_ip_state = IP_RESOLVING;
			MQTT_CLIENTL_UNLOCK(ctx);
			return false;
		} else if (ret == ERR_OK) {
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "MQTT server resolved");
			MQTT_CLIENT_LOCK(ctx);
				ctx->sever_ip_state = IP_RESOLVED;
			MQTT_CLIENTL_UNLOCK(ctx);
		} else {
			return false;
		}
		break;
	case IP_RESOLVED:
		break;
	case IP_RESOLVING:
		if ((now - last_send) > IP_TIMEOUT_MS) {
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Server resolving timeout");
			MQTT_CLIENT_LOCK(ctx);
				ctx->sever_ip_state = IP_NOT_RESOLEVED;
			MQTT_CLIENTL_UNLOCK(ctx);
		}
		return false;
	default:
		return false;
	}

	MQTT_CLIENT_LOCK(ctx);
		if (ctx->state == MQTT_CLIENT_INIT)
			hlog_info(MQTT_MODULE, "Connecting to MQTT server %s (%s) ...", ctx->server_url, inet_ntoa(ctx->server_addr));
		clnt = ctx->client;
		ctx->client = NULL;
	MQTT_CLIENTL_UNLOCK(ctx);
	LWIP_LOCK_START;
		if (clnt) {
			mqtt_disconnect(clnt);
			mqtt_client_free(clnt);
		}
		clnt = mqtt_client_new();
	LWIP_LOCK_END;
	if (!clnt)
		return false;
	MQTT_CLIENT_LOCK(ctx);
		ctx->client = clnt;
		ctx->state = MQTT_CLIENT_CONNECTING;
	MQTT_CLIENTL_UNLOCK(ctx);

	LWIP_LOCK_START;
		ret = mqtt_client_connect(ctx->client, &ctx->server_addr,
								  ctx->server_port, mqtt_hook, ctx, &ctx->client_info);
	LWIP_LOCK_END;

	MQTT_CLIENT_LOCK(ctx);
		if (!ret) {
			ctx->last_send = time_ms_since_boot();
			if (IS_DEBUG(ctx))
				hlog_info(MQTT_MODULE, "Connected to server %s", ctx->server_url);
		} else {
			ctx->state = MQTT_CLIENT_DISCONNECTED;
			hlog_info(MQTT_MODULE, "Connecting to MQTT server %s (%s) failed: %d",
					  ctx->server_url, inet_ntoa(ctx->server_addr), ret);
		}
	MQTT_CLIENTL_UNLOCK(ctx);

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
		(*ctx)->mqtt_min_delay = MSEC_INSEC / res;
		free(str);
	} else {
		(*ctx)->mqtt_min_delay = DF_MIN_PKT_DELAY_MS;
	}

	return 0;
}

static bool sys_mqtt_init(struct mqtt_context_t  **ctx)
{
	if (mqtt_get_config(ctx))
		return false;

	mutex_init(&((*ctx)->lock));

	(*ctx)->state = MQTT_CLIENT_INIT;
	snprintf((*ctx)->status_topic, MQTT_MAX_TOPIC_SIZE, STATUS_TOPIC_TEMPLATE, (*ctx)->state_topic);
	snprintf((*ctx)->commands.cmd_topic, MQTT_MAX_TOPIC_SIZE, COMMAND_TOPIC_TEMPLATE, (*ctx)->state_topic);
	(*ctx)->client_info.client_id = USER_PRAM_GET(DEV_HOSTNAME);
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

int mqtt_msg_component_register(mqtt_component_t *component)
{
	struct mqtt_context_t *ctx = mqtt_context_get();
	int idx;

	if (!ctx)
		return -1;

	idx = ctx->cmp_count;
	if (idx >= MQTT_DISCOVERY_MAX_COUNT) {
		hlog_info(MQTT_MODULE, "Failed to registered discovery message for %s/%s: limit %d reached",
				  component->module, component->name, MQTT_DISCOVERY_MAX_COUNT);
		return -1;
	}

	MQTT_CLIENT_LOCK(ctx);
		component->force = true;
		component->id = idx;
		if (!component->state_topic)
			sys_asprintf(&component->state_topic, "%s/%s/%s/status",
						 ctx->state_topic, component->module, component->name);
		ctx->components[idx] = component;
		ctx->cmp_count++;
		ctx->config.last_send = 0;
	MQTT_CLIENTL_UNLOCK(ctx);

	if (IS_DEBUG(ctx))
		hlog_info(MQTT_MODULE, "Registered discovery message for %s/%s", component->module, component->name);
	return idx;
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
