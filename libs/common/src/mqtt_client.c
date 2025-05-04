// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/cyw43_arch.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"
#include "string.h"

#include "base64.h"
#include "params.h"

#define MQTTLOG	"mqtt"
#define MQTT_KEEPALIVE_S	100
#define IP_TIMEOUT_MS	20000
#define SEND_TIMEOUT_MS	 2000
// send discovery + subscribe on every hour; 60*60*1000
#define CONFIG_INTERVAL_MSEC	3600000
#define COMMAND_TOPIC_TEMPLATE	"%s/command"
#define MAX_CMD_HANDLERS		64

#define MSEC_INSEC	60000ULL

#define IS_DEBUG	(mqtt_context.debug != 0)

/* Send a packet each 60s at least, even if the data is the same */
#define MQTT_SEND_MAX_TIME_MSEC	60000	/* 60s */
/* Limit the rate of the packets to*/
#define MQTT_SEND_MIN_TIME_MSEC	60000	/* 60s */

#define DEF_SERVER_PORT	1883
#define DF_MAX_PKT_DELAY_MS	60000ULL
#define DF_MIN_PKT_DELAY_MS	5000ULL

#define MQTT_QOS		0
#define MQTT_RETAIN		1

#define STATUS_TOPIC_TEMPLATE	"%s/status"
#define ONLINE_MSG				"online"
#define OFFLINE_MSG				"offline"

#define MQTT_CLIENT_LOCK	mutex_enter_blocking(&mqtt_context.lock)
#define MQTT_CLIENTL_UNLOCK	mutex_exit(&mqtt_context.lock)

#define MQTT_DISCOVERY_MAX_COUNT	256
#define MQTT_DISCOVERY_BUFF_SIZE	640
#define MQTT_MAX_TOPIC_SIZE	96

enum mqtt_client_state_t {
	MQTT_CLIENT_INIT = 0,
	MQTT_CLIENT_DISCONNECTED,
	MQTT_CLIENT_CONNECTING,
	MQTT_CLIENT_CONNECTED
};

typedef struct {
	int count;
	char *url;
	char *description;
	void *user_data;
	app_command_t *user_hook;
} mqtt_cmd_t;

typedef struct {
	char *cmd_topic;
	void *cmd_context;
	int cmd_handler;
	mqtt_cmd_t hooks[MAX_CMD_HANDLERS];
	int cmd_msg_size;
	bool cmd_msg_in_progress;
	char cmd_msg[MQTT_OUTPUT_RINGBUF_SIZE];
	mqtt_msg_receive_cb_t cmd_hook;
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

static struct {
	char *server_url;
	char *state_topic;
	char status_topic[MQTT_MAX_TOPIC_SIZE];
	mqtt_commads_t	commands;
	mqtt_component_t *components[MQTT_DISCOVERY_MAX_COUNT];
	uint16_t cmp_count;
	mqtt_discovery_context_t discovery;
	mqtt_config_send_context_t config;
	int server_port;
	uint64_t mqtt_max_delay;
	uint64_t mqtt_min_delay;
	uint32_t max_payload_size;
	enum mqtt_client_state_t state;
	ip_addr_t server_addr;
	ip_resolve_state_t sever_ip_state;
	mqtt_client_t *client;
	struct mqtt_connect_client_info_t client_info;
	bool data_send;
	bool send_in_progress;
	uint64_t send_start;
	uint64_t last_send;
	mutex_t lock;
	uint32_t connect_count;
	uint32_t debug;
} mqtt_context;

static void mqtt_incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
	UNUSED(arg);

	MQTT_CLIENT_LOCK;
	if (!mqtt_context.commands.cmd_topic ||
		 mqtt_context.commands.cmd_msg_in_progress ||
		 tot_len >= MQTT_OUTPUT_RINGBUF_SIZE)
		goto out;

	if (strcmp(topic, mqtt_context.commands.cmd_topic))
		goto out;

	mqtt_context.commands.cmd_msg_in_progress = true;
	mqtt_context.commands.cmd_msg_size = 0;

out:
	MQTT_CLIENTL_UNLOCK;
}

static void mqtt_call_user_cb(void)
{
	cmd_run_context_t ctx = {0};
	app_command_t *cmd;
	int data_offset;
	char *url;
	int i, j;

	if (mqtt_context.commands.cmd_msg_size < 2)
		return;

	ctx.type = CMD_CTX_MQTT;
	for (i = 0; i < mqtt_context.commands.cmd_handler; i++) {
		url = mqtt_context.commands.hooks[i].url;
		if (!url)
			continue;
		if (strlen(url) > strlen(mqtt_context.commands.cmd_msg))
			continue;
		if (strncmp(mqtt_context.commands.cmd_msg, url, strlen(url)))
			continue;
		data_offset = strlen(url) + 1;
		for (j = 0; j < mqtt_context.commands.hooks[i].count; j++) {
			cmd = &mqtt_context.commands.hooks[i].user_hook[j];
			if (!cmd->cb)
				continue;
			if (strlen(cmd->command) > strlen(mqtt_context.commands.cmd_msg + data_offset))
				continue;
			if (strncmp(mqtt_context.commands.cmd_msg + data_offset, cmd->command, strlen(cmd->command)))
				continue;
			if (strlen(mqtt_context.commands.cmd_msg + data_offset) > strlen(cmd->command)) {
				if (mqtt_context.commands.cmd_msg[data_offset + strlen(cmd->command)] != ':')
					continue;
			}
			data_offset += strlen(cmd->command);
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "User command [%s]", mqtt_context.commands.cmd_msg);
			cmd->cb(&ctx, cmd->command, mqtt_context.commands.cmd_msg + data_offset,
					mqtt_context.commands.hooks[i].user_data);
			break;
		}
	}
}

static void mqtt_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	UNUSED(arg);

	MQTT_CLIENT_LOCK;

	if (!mqtt_context.commands.cmd_msg_in_progress)
		goto out;
	if ((mqtt_context.commands.cmd_msg_size + len) >= (MQTT_OUTPUT_RINGBUF_SIZE-1))
		goto out;

	memcpy(mqtt_context.commands.cmd_msg + mqtt_context.commands.cmd_msg_size, data, len);
	mqtt_context.commands.cmd_msg_size += len;

	if (flags & MQTT_DATA_FLAG_LAST) {
		mqtt_context.commands.cmd_msg[mqtt_context.commands.cmd_msg_size] = '\0';
		MQTT_CLIENTL_UNLOCK;
			mqtt_call_user_cb();
		MQTT_CLIENT_LOCK;
		mqtt_context.commands.cmd_msg_in_progress = false;
		mqtt_context.commands.cmd_msg_size = 0;
	}

out:
	MQTT_CLIENTL_UNLOCK;
}

static int mqtt_cmd_subscribe(void)
{
	err_t ret = -1;

	MQTT_CLIENT_LOCK;
	if (!mqtt_context.commands.cmd_topic) {
		ret = 1;
		goto out;
	}
	if (mqtt_context.state != MQTT_CLIENT_CONNECTED)
		goto out;

	LWIP_LOCK_START;
		ret = mqtt_subscribe(mqtt_context.client, mqtt_context.commands.cmd_topic, MQTT_QOS, NULL, NULL);
	LWIP_LOCK_END;

	if (!ret && IS_DEBUG)
		hlog_info(MQTTLOG, "Subscribed to MQTT topic [%s]", mqtt_context.commands.cmd_topic);

out:
	MQTT_CLIENTL_UNLOCK;
	return ret;
}

static void mqtt_publish_cb(void *arg, err_t result)
{
	UNUSED(result);
	UNUSED(arg);
	MQTT_CLIENT_LOCK;
		mqtt_context.send_in_progress = false;
	MQTT_CLIENTL_UNLOCK;
}

static int mqtt_msg_send(char *topic, char *message)
{
	uint64_t now, last;
	bool in_progress;
	err_t err;

	if (!mqtt_is_connected())
		return -1;

	now = time_ms_since_boot();
	MQTT_CLIENT_LOCK;
		in_progress = mqtt_context.send_in_progress;
		last = mqtt_context.send_start;
	MQTT_CLIENTL_UNLOCK;

	if (in_progress) {
		if ((now - last) < SEND_TIMEOUT_MS) {
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Cannot publish: connected %d, send in progress %d", mqtt_is_connected(), in_progress);
			return -1;
		}
		MQTT_CLIENT_LOCK;
			mqtt_context.send_in_progress = false;
		MQTT_CLIENTL_UNLOCK;
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Timeout sending MQTT packet");
	}

	LWIP_LOCK_START;
		err = mqtt_publish(mqtt_context.client, topic, message,
						   strlen(message), MQTT_QOS, MQTT_RETAIN, mqtt_publish_cb, NULL);
	LWIP_LOCK_END;

	if (err == ERR_OK) {
		MQTT_CLIENT_LOCK;
			mqtt_context.send_in_progress = true;
			mqtt_context.send_start = time_ms_since_boot();
		MQTT_CLIENTL_UNLOCK;
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Published %d bytes to [%s]", strlen(message), topic);
	} else {
		hlog_info(MQTTLOG, "Failed to publish the message: %d", err);
		return -1;
	}

	return 0;
}

#define ADD_STR(F, ...) {\
	int ret = snprintf(mqtt_context.discovery.buff + count, size, F, __VA_ARGS__);\
	count += ret; size -= ret;\
	if (size < 0)	\
		return size; \
	}
// https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
// https://www.home-assistant.io/integrations/mqtt/#discovery-examples-with-component-discovery
static int mqtt_discovery_generate_component(mqtt_component_t *component)
{
	int size = MQTT_DISCOVERY_BUFF_SIZE;
	int count = 0;
	int ret;

	mqtt_context.discovery.buff[0] = 0;
	ret = snprintf(mqtt_context.discovery.topic, MQTT_MAX_TOPIC_SIZE, "homeassistant/%s/%s_%s_%s/config",
				   component->platform, mqtt_context.state_topic, component->module, component->name);
	if (ret <= 0)
		return -1;

	/* Device section */
	ADD_STR("%s", "{\"device\":{");
		ADD_STR("\"identifiers\": [\"%s\"]", mqtt_context.client_info.client_id);
		ADD_STR(",\"name\": \"%s\"", mqtt_context.client_info.client_id);
	ADD_STR("%s", "}");
	if (component->dev_class)
		ADD_STR(",\"device_class\": \"%s\"", component->dev_class);
	if (component->unit)
		ADD_STR(",\"unit_of_measurement\": \"%s\"", component->unit);
	ADD_STR(",\"value_template\": \"%s\"", component->value_template);
	ADD_STR(",\"name\": \"%s_%s\"", component->module, component->name);
	ADD_STR(",\"unique_id\": \"%s_%s_%s\"",
				mqtt_context.client_info.client_id, component->module, component->name);
	ADD_STR(",\"state_topic\": \"%s\"", component->state_topic);
	ADD_STR(",\"json_attributes_topic\": \"%s/%s/%s/status\"", mqtt_context.state_topic, component->module, component->name);
	ADD_STR(",\"json_attributes_template\": \"%s\"", "{{ value_json | tojson }}");
	if (component->payload_on)
		ADD_STR(",\"payload_on\": \"%s\"", component->payload_on);
	if (component->payload_off)
		ADD_STR(",\"payload_off\": \"%s\"", component->payload_off);
	ADD_STR("%s", "}");

	return size;
}

static int mqtt_discovery_generate_device(void)
{
	int size = MQTT_DISCOVERY_BUFF_SIZE;
	int count = 0;
	int ret;

	mqtt_context.discovery.buff[0] = 0;
	ret = snprintf(mqtt_context.discovery.topic, MQTT_MAX_TOPIC_SIZE, "homeassistant/device/%s/config",
				   mqtt_context.state_topic);
	if (ret <= 0)
		return -1;

	ADD_STR("%s", "{\"device\":{");
		ADD_STR("\"identifiers\": [\"%s\"]", mqtt_context.client_info.client_id);
		ADD_STR(",\"name\": \"%s\"", mqtt_context.client_info.client_id);
	ADD_STR("%s", "}");
	ADD_STR("%s", ",\"origin\":{");
		ADD_STR("\"name\": \"%s\"", mqtt_context.client_info.client_id);
		if (webserv_port())
			ADD_STR(",\"url\": \"http://%s:%d/help\"",
					inet_ntoa(cyw43_state.netif[0].ip_addr), webserv_port());
	ADD_STR("%s", "}");
	ADD_STR("%s", ",\"components\":{");
		ADD_STR("\"%s-%s\": {", mqtt_context.client_info.client_id, "device");
			ADD_STR("\"platform\": \"%s\"", "binary_sensor");
			ADD_STR(",\"device_class\": \"%s\"", "connectivity");
			ADD_STR(",\"name\": \"%s_%s\"", mqtt_context.client_info.client_id, "device_link");
			ADD_STR(",\"unique_id\": \"%s_%s\"", mqtt_context.client_info.client_id, "device_link");
			ADD_STR(",\"payload_on\": \"%s\"", ONLINE_MSG);
			ADD_STR(",\"payload_off\": \"%s\"", OFFLINE_MSG);
		ADD_STR("%s", "}}");
	ADD_STR(",\"state_topic\": \"%s\"", mqtt_context.status_topic);
	ADD_STR(",\"availability_topic\": \"%s\"", mqtt_context.status_topic);
	ADD_STR(",\"payload_available\": \"%s\"", ONLINE_MSG);
	ADD_STR(",\"payload_not_available\": \"%s\"", OFFLINE_MSG);
	ADD_STR("%s", "}");

	return size;
}

static int mqtt_msg_discovery_send_device(void)
{
	int msize;
	int ret = -1;

	msize = mqtt_discovery_generate_device();
	if (msize > 0)
		ret = mqtt_msg_send(mqtt_context.discovery.topic, mqtt_context.discovery.buff);

	if (!ret) {
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Send %d bytes device discovery message",
					  strlen(mqtt_context.discovery.buff));
	} else {
		hlog_info(MQTTLOG, "Failed to publish %d/%d bytes device discovery message",
				  strlen(mqtt_context.discovery.buff), msize);
	}
	return ret;
}

static int mqtt_msg_discovery_send(void)
{
	mqtt_component_t *comp;
	int ret = -1;
	int msize;

	if (mqtt_context.discovery.send_idx >= mqtt_context.cmp_count)
		return -1;

	MQTT_CLIENT_LOCK;
		comp = mqtt_context.components[mqtt_context.discovery.send_idx];
	MQTT_CLIENTL_UNLOCK;
	msize = mqtt_discovery_generate_component(comp);
	if (msize > 0)
		ret = mqtt_msg_send(mqtt_context.discovery.topic, mqtt_context.discovery.buff);

	if (!ret) {
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Send %d bytes discovery message of %s/%s",
					  strlen(mqtt_context.discovery.buff), comp->module, comp->name);
	} else {
		hlog_info(MQTTLOG, "Failed to publish %d/%d bytes discovery message",
				  strlen(mqtt_context.discovery.buff), msize);
	}

	return ret;
}

static void mqtt_config_send(void)
{
	int sent = 0;
	uint64_t now;
	int ret = -1;

	if (mqtt_context.state != MQTT_CLIENT_CONNECTED)
		return;

	now = time_ms_since_boot();
	MQTT_CLIENT_LOCK;
		if (mqtt_context.config.last_send == 0 ||
		    (now - mqtt_context.config.last_send) > CONFIG_INTERVAL_MSEC) {
			mqtt_context.config.discovery_dev = true;
			if (mqtt_context.cmp_count >  0) {
				mqtt_context.config.discovery_comp = true;
				mqtt_context.discovery.send_idx = 0;
			}
			if (mqtt_context.status_topic[0])
				mqtt_context.config.status = true;
			if (mqtt_context.commands.cmd_topic)
				mqtt_context.config.subscribe = true;
		}
	MQTT_CLIENTL_UNLOCK;
	if (!mqtt_is_connected())
		return;
	if (mqtt_context.config.status) {
		ret = mqtt_msg_send(mqtt_context.status_topic, ONLINE_MSG);
		if (!ret) {
			mqtt_context.config.status_send++;
			mqtt_context.config.status = false;
			sent++;
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Send status message [%s] on [%s]", ONLINE_MSG, mqtt_context.status_topic);
		}
		goto out;
	}
	if (mqtt_context.config.discovery_dev) {
		ret = mqtt_msg_discovery_send_device();
		if (!ret)
			mqtt_context.config.discovery_dev = false;
		goto out;
	}
	if (mqtt_context.config.discovery_comp) {

		ret = mqtt_msg_discovery_send();
		if (!ret) {
			sent++;
			mqtt_context.config.discovery_send++;
			mqtt_context.discovery.send_idx++;
		}
		if (mqtt_context.discovery.send_idx >= mqtt_context.cmp_count) {
			mqtt_context.discovery.send_idx = 0;
			mqtt_context.config.discovery_comp = false;
		}
		goto out;
	}
	if (mqtt_context.config.subscribe) {
		ret = mqtt_cmd_subscribe();
		if (!ret) {
			sent++;
			mqtt_context.config.subscribe_send++;
			mqtt_context.config.subscribe = false;
		}
	}
out:
	if (sent) {
		MQTT_CLIENT_LOCK;
			mqtt_context.config.last_send = now;
		MQTT_CLIENTL_UNLOCK;
	}
}

static void mqtt_hook(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	UNUSED(client);
	UNUSED(arg);
	MQTT_CLIENT_LOCK;
		mqtt_context.send_in_progress = false;
	MQTT_CLIENTL_UNLOCK;

	switch (status) {
	case MQTT_CONNECT_ACCEPTED:
		if (mqtt_context.state != MQTT_CLIENT_CONNECTED) {
			system_log_status();
			MQTT_CLIENT_LOCK;
				mqtt_context.connect_count++;
				LWIP_LOCK_START;
					mqtt_set_inpub_callback(client, mqtt_incoming_publish, mqtt_incoming_data, NULL);
				LWIP_LOCK_END;
			MQTT_CLIENTL_UNLOCK;
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Connected to server %s", mqtt_context.server_url);
		}
		mqtt_context.state = MQTT_CLIENT_CONNECTED;
		mqtt_context.config.last_send = 0;
		mqtt_config_send();
		break;
	case MQTT_CONNECT_DISCONNECTED:
		if (mqtt_context.state != MQTT_CLIENT_DISCONNECTED) {
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Disconnected from server %s", mqtt_context.server_url);
		}
		mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
		break;
	case MQTT_CONNECT_TIMEOUT:
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Server timeout %s", mqtt_context.server_url);
		mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
		break;
	case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
	case MQTT_CONNECT_REFUSED_IDENTIFIER:
	case MQTT_CONNECT_REFUSED_SERVER:
	case MQTT_CONNECT_REFUSED_USERNAME_PASS:
	case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
		hlog_info(MQTTLOG, "Connection refused from server %s -> %d", mqtt_context.server_url, status);
		mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
		break;
	default:
		hlog_info(MQTTLOG, "Unknown state of the server %s -> %d", mqtt_context.server_url, status);
		break;
	}
}

static void mqtt_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	UNUSED(hostname);
	UNUSED(arg);
	MQTT_CLIENT_LOCK;
		memcpy(&(mqtt_context.server_addr), ipaddr, sizeof(ip_addr_t));
		mqtt_context.sever_ip_state = IP_RESOLVED;
	MQTT_CLIENTL_UNLOCK;
}

bool mqtt_is_connected(void)
{
	uint8_t ret;

	if (!mqtt_context.client)
		return false;

	LWIP_LOCK_START;
		ret = mqtt_client_is_connected(mqtt_context.client);
	LWIP_LOCK_END;

	return ret;
}

#define LOG_STEP	2
static bool mqtt_log_status(void *context)
{
	static int progress;
	int i, j, k;

	UNUSED(context);

	if (!progress) {
		if (!mqtt_is_connected()) {
			hlog_info(MQTTLOG, "Not connected to a server, looking for %s ... connect count %d ",
					mqtt_context.server_url, mqtt_context.connect_count);
			return true;
		}
		hlog_info(MQTTLOG, "Connected to server %s, publish rate limit between %lldppm and %lldppm, connect count %d",
				mqtt_context.server_url, MSEC_INSEC/mqtt_context.mqtt_max_delay,
				MSEC_INSEC/mqtt_context.mqtt_min_delay, mqtt_context.connect_count);
		if (mqtt_context.commands.cmd_topic)
			hlog_info(MQTTLOG, "Listen to topic [%s]", mqtt_context.commands.cmd_topic);
		progress++;
		return false;
	}

	if (progress <= mqtt_context.commands.cmd_handler) {
		k = 0;
		for (i = progress - 1; i < mqtt_context.commands.cmd_handler; i++) {
			hlog_info(MQTTLOG, "  Serving user commands [%s]:", mqtt_context.commands.hooks[i].description);
			for (j = 0; j < mqtt_context.commands.hooks[i].count; j++) {
				hlog_info(MQTTLOG, "    %s/%s%s",
						mqtt_context.commands.hooks[i].url,
						mqtt_context.commands.hooks[i].user_hook[j].command,
						mqtt_context.commands.hooks[i].user_hook[j].help?mqtt_context.commands.hooks[i].user_hook[j].help:"");
			}
			k++;
			progress++;
			if (k >= LOG_STEP)
				break;
		}
	}
	if (progress <= mqtt_context.commands.cmd_handler)
		return false;

	hlog_info(MQTTLOG, "Registered %d devices", mqtt_context.cmp_count);
	hlog_info(MQTTLOG, "Sent %d discovery messages", mqtt_context.config.discovery_send);

	if (mqtt_context.status_topic[0]) {
		hlog_info(MQTTLOG, "Sending status to [%s], sent %d",
				  mqtt_context.status_topic, mqtt_context.config.status_send);
	} else {
		hlog_info(MQTTLOG, "No status is send.");
	}

	if (mqtt_context.commands.cmd_topic) {
		hlog_info(MQTTLOG, "Listen for commands on [%s], subscribed %d",
				  mqtt_context.commands.cmd_topic, mqtt_context.config.subscribe_send);
	} else {
		hlog_info(MQTTLOG, "Do not listen for commands");
	}
	progress = 0;
	return true;
}

int mqtt_msg_publish(char *topic, char *message, bool force)
{
	char *topic_str = topic ? topic : mqtt_context.state_topic;
	uint64_t now;
	int ret;

	if (mqtt_context.state != MQTT_CLIENT_CONNECTED)
		return -1;

	if (strlen(message) > mqtt_context.max_payload_size) {
		hlog_info(MQTTLOG, "Message too big: %d, max payload is %d", strlen(message), mqtt_context.max_payload_size);
		return -1;
	}

	mqtt_context.data_send = force;
	/* Rate limit the packets between mqtt_min_delay and mqtt_max_delay */
	now = time_ms_since_boot();
	if ((now - mqtt_context.last_send) > mqtt_context.mqtt_max_delay)
		mqtt_context.data_send = true;
	else if ((now - mqtt_context.last_send) < mqtt_context.mqtt_min_delay)
		mqtt_context.data_send = false;
	if (!mqtt_context.data_send && mqtt_context.last_send)
		return -1;

	ret = mqtt_msg_send(topic_str, message);
	if (!ret) {
		mqtt_context.data_send = false;
		MQTT_CLIENT_LOCK;
			mqtt_context.last_send = time_ms_since_boot();
		MQTT_CLIENTL_UNLOCK;
	}
	return ret;
}

int mqtt_msg_component_publish(mqtt_component_t *component, char *message)
{
	int ret;

	ret = mqtt_msg_publish(component->state_topic, message, component->force);
	if (!ret) {
		component->force = false;
		component->last_send = time_ms_since_boot();
	}

	return ret;
}

static void mqtt_connect(void)
{
	enum mqtt_client_state_t st;
	ip_resolve_state_t res;
	uint64_t last_send;
	uint64_t now;
	int ret;

	if (!wifi_is_connected()) {
		if (mqtt_is_connected()) {
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "No WiFi, force reconnection");
			mqtt_reconnect();
		}
		return;
	}
	if (mqtt_is_connected())
		return;

	MQTT_CLIENT_LOCK;
		st = mqtt_context.state;
		last_send = mqtt_context.last_send;
	MQTT_CLIENTL_UNLOCK;

	now = time_ms_since_boot();
	if (st == MQTT_CLIENT_CONNECTING) {
		if ((now - last_send) < IP_TIMEOUT_MS)
			return;
		if (mqtt_context.client) {
			LWIP_LOCK_START;
				mqtt_disconnect(mqtt_context.client);
				mqtt_client_free(mqtt_context.client);
			LWIP_LOCK_END;
			mqtt_context.client = NULL;
		}
		MQTT_CLIENT_LOCK;
			mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
			mqtt_context.sever_ip_state = IP_NOT_RESOLEVED;
		MQTT_CLIENTL_UNLOCK;

		hlog_info(MQTTLOG, "Connect to %s timeout", mqtt_context.server_url);
	}

	MQTT_CLIENT_LOCK;
		res = mqtt_context.sever_ip_state;
	MQTT_CLIENTL_UNLOCK;

	switch (res) {
	case IP_NOT_RESOLEVED:
		LWIP_LOCK_START;
			ret = dns_gethostbyname(mqtt_context.server_url, &mqtt_context.server_addr, mqtt_server_found, NULL);
		LWIP_LOCK_END;
		if (ret == ERR_INPROGRESS) {
			hlog_info(MQTTLOG, "Resolving %s ...", mqtt_context.server_url);
			MQTT_CLIENT_LOCK;
				mqtt_context.last_send = time_ms_since_boot();
				mqtt_context.sever_ip_state = IP_RESOLVING;
			MQTT_CLIENTL_UNLOCK;
			return;
		} else if (ret == ERR_OK) {
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "MQTT server resolved");
			MQTT_CLIENT_LOCK;
				mqtt_context.sever_ip_state = IP_RESOLVED;
			MQTT_CLIENTL_UNLOCK;
		} else {
			return;
		}
		break;
	case IP_RESOLVED:
		break;
	case IP_RESOLVING:
		if ((now - last_send) > IP_TIMEOUT_MS) {
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Server resolving timeout");
			MQTT_CLIENT_LOCK;
				mqtt_context.sever_ip_state = IP_NOT_RESOLEVED;
			MQTT_CLIENTL_UNLOCK;
		}
		return;
	default:
		return;
	}

	MQTT_CLIENT_LOCK;
		if (mqtt_context.state == MQTT_CLIENT_INIT)
			hlog_info(MQTTLOG, "Connecting to MQTT server %s (%s) ...", mqtt_context.server_url, inet_ntoa(mqtt_context.server_addr));
		LWIP_LOCK_START;
			if (mqtt_context.client) {
				mqtt_disconnect(mqtt_context.client);
				mqtt_client_free(mqtt_context.client);
			}
			mqtt_context.client = mqtt_client_new();
		LWIP_LOCK_END;
		if (!mqtt_context.client) {
			MQTT_CLIENTL_UNLOCK;
			return;
		}
		mqtt_context.state = MQTT_CLIENT_CONNECTING;
	MQTT_CLIENTL_UNLOCK;

	LWIP_LOCK_START;
		ret = mqtt_client_connect(mqtt_context.client, &mqtt_context.server_addr,
								  mqtt_context.server_port, mqtt_hook, NULL, &mqtt_context.client_info);
	LWIP_LOCK_END;

	MQTT_CLIENT_LOCK;
		if (!ret) {
			mqtt_context.last_send = time_ms_since_boot();
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Connected to server %s", mqtt_context.server_url);
		} else {
			mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
			hlog_info(MQTTLOG, "Connecting to MQTT server %s (%s) failed: %d",
					mqtt_context.server_url, inet_ntoa(mqtt_context.server_addr), ret);
		}
	MQTT_CLIENTL_UNLOCK;
}

void mqtt_run(void)
{
	mqtt_connect();
	if (mqtt_context.state != MQTT_CLIENT_CONNECTED)
		return;
	mqtt_config_send();
}

static int mqtt_get_config(void)
{
	char *str, *tok, *rest;
	int res;

	if (MQTT_SERVER_ENDPOINT_len < 1 || MQTT_TOPIC_len < 1 || MQTT_USER_len < 1)
		return -1;

	mqtt_context.state_topic = param_get(MQTT_TOPIC);

	str = param_get(MQTT_SERVER_ENDPOINT);
	rest = str;
	tok = strtok_r(rest, ":", &rest);
	mqtt_context.server_url = strdup(tok);
	if (rest)
		mqtt_context.server_port = atoi(rest);
	else
		mqtt_context.server_port = DEF_SERVER_PORT;
	free(str);

	rest = param_get(MQTT_USER);
	tok = strtok_r(rest, ";", &rest);
	mqtt_context.client_info.client_user = tok;
	mqtt_context.client_info.client_pass = rest;

	if (MQTT_RATE_PPM_len > 1) {
		str = param_get(MQTT_RATE_PPM);
		rest = str;
		tok = strtok_r(rest, ";", &rest);
		res = (int)strtol(tok, NULL, 10);
		mqtt_context.mqtt_max_delay = MSEC_INSEC / res;
		res = (int)strtol(rest, NULL, 10);
		mqtt_context.mqtt_min_delay = MSEC_INSEC / res;
		free(str);
	} else {
		mqtt_context.mqtt_max_delay = DF_MAX_PKT_DELAY_MS;
		mqtt_context.mqtt_min_delay = DF_MIN_PKT_DELAY_MS;
	}

	return 0;
}

void mqtt_reconnect(void)
{
	MQTT_CLIENT_LOCK;
		if (mqtt_context.state == MQTT_CLIENT_INIT) {
			MQTT_CLIENTL_UNLOCK;
			return;
		}
		if (mqtt_context.client) {
			LWIP_LOCK_START;
				mqtt_disconnect(mqtt_context.client);
				mqtt_client_free(mqtt_context.client);
			LWIP_LOCK_END;
			mqtt_context.client = NULL;
		}
		mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
		mqtt_context.sever_ip_state = IP_NOT_RESOLEVED;
		hlog_info(MQTTLOG, "Disconnected form %s", mqtt_context.server_url);
	MQTT_CLIENTL_UNLOCK;
}

bool mqtt_init(void)
{
	memset(&mqtt_context, 0, sizeof(mqtt_context));
	mutex_init(&mqtt_context.lock);

	if (mqtt_get_config())
		return false;

	mqtt_context.state = MQTT_CLIENT_INIT;
	snprintf(mqtt_context.status_topic, MQTT_MAX_TOPIC_SIZE,
			 STATUS_TOPIC_TEMPLATE, mqtt_context.state_topic);
	mqtt_context.client_info.client_id = param_get(DEV_HOSTNAME);
	mqtt_context.client_info.keep_alive = MQTT_KEEPALIVE_S;
	mqtt_context.client_info.will_topic = mqtt_context.status_topic;
	mqtt_context.client_info.will_msg = OFFLINE_MSG;
	mqtt_context.client_info.will_qos = 1;
	mqtt_context.client_info.will_retain = 1;
	mqtt_context.send_in_progress = false;
	mqtt_context.max_payload_size = MQTT_OUTPUT_RINGBUF_SIZE - MQTT_MAX_TOPIC_SIZE;

	add_status_callback(mqtt_log_status, NULL);

	return true;
}

void mqtt_debug_set(uint32_t lvl)
{
	mqtt_context.debug = lvl;
}

int mqtt_msg_component_register(mqtt_component_t *component)
{
	int idx = mqtt_context.cmp_count;

	if (idx >= MQTT_DISCOVERY_MAX_COUNT) {
		hlog_info(MQTTLOG, "Failed to registered discovery message for %s/%s: limit %d reached",
				  component->module, component->name, MQTT_DISCOVERY_MAX_COUNT);
		return -1;
	}

	MQTT_CLIENT_LOCK;
		component->force = true;
		component->id = idx;
		if (!component->state_topic)
			sys_asprintf(&component->state_topic, "%s/%s/%s/status",
						 mqtt_context.state_topic, component->module, component->name);
		mqtt_context.components[idx] = component;
		mqtt_context.cmp_count++;
		mqtt_context.config.last_send = 0;
	MQTT_CLIENTL_UNLOCK;

	if (IS_DEBUG)
		hlog_info(MQTTLOG, "Registered discovery message for %s/%s", component->module, component->name);
	return idx;
}

int mqtt_add_commands(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data)
{
	mqtt_cmd_t *arr_hooks;
	int size, ret;

	MQTT_CLIENT_LOCK;

	if (!mqtt_context.state_topic)
		goto out_err;
	if (mqtt_context.commands.cmd_handler >= MAX_CMD_HANDLERS)
		goto out_err;

	if (!mqtt_context.commands.cmd_topic) {
		size = strlen(mqtt_context.state_topic) + strlen(COMMAND_TOPIC_TEMPLATE) + 1;
		mqtt_context.commands.cmd_topic = malloc(size);
		if (!mqtt_context.commands.cmd_topic)
			goto out_err;
		ret = snprintf(mqtt_context.commands.cmd_topic, size, COMMAND_TOPIC_TEMPLATE, mqtt_context.state_topic);
		if (ret <= 0) {
			free(mqtt_context.commands.cmd_topic);
			mqtt_context.commands.cmd_topic = NULL;
			goto out_err;
		}
	}
	arr_hooks = &mqtt_context.commands.hooks[mqtt_context.commands.cmd_handler++];
	arr_hooks->url = module;
	arr_hooks->description = description;
	arr_hooks->count = commands_cont;
	arr_hooks->user_data = user_data;
	arr_hooks->user_hook = commands;

	MQTT_CLIENTL_UNLOCK;
	mqtt_cmd_subscribe();
	return 0;

out_err:
	MQTT_CLIENTL_UNLOCK;
	return -1;
}
