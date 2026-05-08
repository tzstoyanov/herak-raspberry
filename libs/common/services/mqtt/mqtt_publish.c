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

static void mqtt_publish_cb(void *arg, err_t result)
{
	struct mqtt_context_t *ctx = (struct mqtt_context_t *)arg;

	UNUSED(result);
	ctx->send_in_progress--;
	if (result != ERR_OK)
		ctx->send_err_count++;
}

int mqtt_msg_send(struct mqtt_context_t *ctx, char *topic, char *message)
{
	err_t err;

	if (!mqtt_is_connected_ctx(ctx))
		return -1;

	LWIP_LOCK_START;
		err = mqtt_publish(ctx->client, topic, message, strlen(message),
						   MQTT_QOS, MQTT_RETAIN, mqtt_publish_cb, ctx);
	LWIP_LOCK_END;

	if (err == ERR_OK) {
		ctx->send_err_count = 0;
		ctx->send_in_progress++;
		ctx->send_start = time_ms_since_boot();
		if (IS_DEBUG(ctx))
			hlog_info(MQTT_MODULE, "Published %d bytes to [%s]", strlen(message), topic);
	} else {
		ctx->send_err_count++;
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

int mqtt_msg_discovery_send_device(struct mqtt_context_t *ctx)
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

int mqtt_msg_discovery_send(struct mqtt_context_t *ctx)
{
	mqtt_component_t *comp;
	int ret = -1;
	int msize;

	if (ctx->discovery.send_idx >= ctx->cmp_count)
		return -1;

	comp = ctx->components[ctx->discovery.send_idx];
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

/* API */

bool mqtt_is_discovery_sent(void)
{
	struct mqtt_context_t *ctx = mqtt_context_get();

	if (!ctx)
		return false;

	if (ctx->client && ctx->config.discovery_send >= ctx->cmp_count)
		return true;

	return false;
}

int mqtt_msg_publish(char *topic, char *message, bool force)
{
	struct mqtt_context_t *ctx = mqtt_context_get();
	bool reset_filter = false;
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

	/* Rate limit the packets */
	now = time_ms_since_boot();
	if (ctx->filter_pkt_count >= ctx->max_ppm) {
		if ((now - ctx->filter_pkt_send) >= MSEC2MIN)
			reset_filter = true;
		else if (ctx->last_send)
			return -1;
	}

	ret = mqtt_msg_send(ctx, topic_str, message);
	if (!ret) {
		ctx->last_send = now;
		if (reset_filter) {
			ctx->filter_pkt_count = 0;
			ctx->filter_pkt_send = now;
		}
	}
	ctx->filter_pkt_count++;
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

	component->force = true;
	component->id = idx;
	if (!component->state_topic)
		sys_asprintf(&component->state_topic, "%s/%s/%s/status",
					 ctx->state_topic, component->module, component->name);
	ctx->components[idx] = component;
	ctx->cmp_count++;
	ctx->config.last_send = 0;

	if (IS_DEBUG(ctx))
		hlog_info(MQTT_MODULE, "Registered discovery message for %s/%s", component->module, component->name);
	return idx;
}
