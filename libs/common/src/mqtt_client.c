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
#define DISCOVERY_INTERVAL_MSEC		3600000 // send every hour; 60*60*1000
#define DISCOVERY_TOPIC_TEMPLATE	"homeassistant/device/%s/config"

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

#define WILL_TOPIC	"herak/status"
#define WILL_MSG	"{\"status\":\"offline\"}"

#define MQTT_CLIENT_LOCK	mutex_enter_blocking(&mqtt_context.lock)
#define MQTT_CLIENTL_UNLOCK	mutex_exit(&mqtt_context.lock)

enum mqtt_client_state_t {
	MQTT_CLIENT_INIT = 0,
	MQTT_CLIENT_DISCONNECTED,
	MQTT_CLIENT_CONNECTING,
	MQTT_CLIENT_CONNECTED
};

static struct {
	char *server_url;
	char *state_topic;
	char *discovery_topic;
	uint64_t discovery_last_send;
	bool discovery_send;
	char discovery_msg[MQTT_OUTPUT_RINGBUF_SIZE];
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
	bool send_in_progerss;
	uint64_t last_send;
	mutex_t lock;
	uint32_t connect_count;
	uint32_t debug;
} mqtt_context;

static void mqtt_hook(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	UNUSED(client);
	UNUSED(arg);
	MQTT_CLIENT_LOCK;
		mqtt_context.send_in_progerss = false;
	MQTT_CLIENTL_UNLOCK;

	switch (status) {
	case MQTT_CONNECT_ACCEPTED:
		if (mqtt_context.state != MQTT_CLIENT_CONNECTED) {
			system_log_status();
			MQTT_CLIENT_LOCK;
				mqtt_context.connect_count++;
				mqtt_context.discovery_last_send = 0;
			MQTT_CLIENTL_UNLOCK;
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Connected accepted");
		}
		mqtt_context.state = MQTT_CLIENT_CONNECTED;
		break;
	case MQTT_CONNECT_DISCONNECTED:
		if (mqtt_context.state != MQTT_CLIENT_DISCONNECTED)
			hlog_info(MQTTLOG, "Disconnected from server %s", mqtt_context.server_url);
		mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
		break;
	case MQTT_CONNECT_TIMEOUT:
		hlog_info(MQTTLOG, "Timeout server %s", mqtt_context.server_url);
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

static void mqtt_log_status(void *context)
{

	UNUSED(context);

	if (!mqtt_is_connected())
		hlog_info(MQTTLOG, "Not connected to a server, looking for %s ... connect count %d ",
				  mqtt_context.server_url, mqtt_context.connect_count);
	else
		hlog_info(MQTTLOG, "Connected to server %s, publish rate limit between %lldppm and %lldppm, connect count %d",
				mqtt_context.server_url, MSEC_INSEC/mqtt_context.mqtt_max_delay,
				MSEC_INSEC/mqtt_context.mqtt_min_delay, mqtt_context.connect_count);
}

static void mqtt_publish_cb(void *arg, err_t result)
{
	UNUSED(result);
	UNUSED(arg);
	MQTT_CLIENT_LOCK;
		mqtt_context.send_in_progerss = false;
	MQTT_CLIENTL_UNLOCK;
}

static int mqtt_msg_send(char *topic, char *message)
{
	bool in_progress;
	err_t err;

	if (!mqtt_is_connected())
		return -1;

	MQTT_CLIENT_LOCK;
		in_progress = mqtt_context.send_in_progerss;
	MQTT_CLIENTL_UNLOCK;

	if (in_progress) {
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Cannot publish: connected %d, send in progress %d", mqtt_is_connected(), in_progress);
		return -1;
	}

	LWIP_LOCK_START;
		err = mqtt_publish(mqtt_context.client, topic, message,
						   strlen(message), MQTT_QOS, MQTT_RETAIN, mqtt_publish_cb, NULL);
	LWIP_LOCK_END;

	if (err == ERR_OK) {
		MQTT_CLIENT_LOCK;
			mqtt_context.send_in_progerss = true;
		MQTT_CLIENTL_UNLOCK;
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Published %d bytes", strlen(message));
	} else {
		hlog_info(MQTTLOG, "Failed to publish the message: %d", err);
		return -1;
	}

	return 0;
}

void mqtt_msg_publish(char *message, bool force)
{
	uint64_t now;

	if (strlen(message) > mqtt_context.max_payload_size) {
		hlog_info(MQTTLOG, "Message too big: %d, max payload is %d", strlen(message), mqtt_context.max_payload_size);
		return;
	}

	mqtt_context.data_send = force;
	/* Rate limit the packets between mqtt_min_delay and mqtt_max_delay */
	now = time_ms_since_boot();
	if ((now - mqtt_context.last_send) > mqtt_context.mqtt_max_delay)
		mqtt_context.data_send = true;
	else if ((now - mqtt_context.last_send) < mqtt_context.mqtt_min_delay)
		mqtt_context.data_send = false;
	if (!mqtt_context.data_send && mqtt_context.last_send)
		return;

	if (!mqtt_msg_send(mqtt_context.state_topic, message)) {
		mqtt_context.data_send = false;
		MQTT_CLIENT_LOCK;
			mqtt_context.last_send = time_ms_since_boot();
		MQTT_CLIENTL_UNLOCK;
	}
}

int mqtt_msg_discovery_send(void)
{
	bool send = false;
	uint64_t now;
	int ret;

	if (!mqtt_context.discovery_send)
		return -1;

	now = time_ms_since_boot();
	MQTT_CLIENT_LOCK;
		if (!mqtt_context.discovery_last_send ||
		    (now - mqtt_context.discovery_last_send) > DISCOVERY_INTERVAL_MSEC)
			send = true;
	MQTT_CLIENTL_UNLOCK;
	if (!send)
		return 0;

	ret = mqtt_msg_send(mqtt_context.discovery_topic, mqtt_context.discovery_msg);
	if (!ret) {
		MQTT_CLIENT_LOCK;
			mqtt_context.discovery_last_send = now;
		MQTT_CLIENTL_UNLOCK;
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Send %d bytes discovery message", strlen(mqtt_context.discovery_msg));
	} else {
		hlog_info(MQTTLOG, "Failed to publish %d bytes discovery message",
				  strlen(mqtt_context.discovery_msg));
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
	mqtt_msg_discovery_send();
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
	mqtt_context.client_info.client_id = param_get(DEV_HOSTNAME);
	mqtt_context.client_info.keep_alive = MQTT_KEEPALIVE_S;
	mqtt_context.client_info.will_topic = WILL_TOPIC;
	mqtt_context.client_info.will_msg = WILL_MSG;
	mqtt_context.client_info.will_qos = 1;
	mqtt_context.client_info.will_retain = 1;
	mqtt_context.send_in_progerss = false;
	mqtt_context.discovery_send = false;
	mqtt_context.max_payload_size = MQTT_OUTPUT_RINGBUF_SIZE - (strlen(mqtt_context.state_topic) + 2);

	add_status_callback(mqtt_log_status, NULL);

	return true;
}

void mqtt_debug_set(uint32_t lvl)
{
	mqtt_context.debug = lvl;
}

static int discovery_init(void)
{
	int size, ret;

	if (!mqtt_context.state_topic)
		return -1;
	size = strlen(mqtt_context.state_topic) + strlen(DISCOVERY_TOPIC_TEMPLATE) + 1;
	mqtt_context.discovery_topic = malloc(size);
	if (!mqtt_context.discovery_topic)
		return -1;
	ret = snprintf(mqtt_context.discovery_topic, size, DISCOVERY_TOPIC_TEMPLATE, mqtt_context.state_topic);
	if (ret <= 0) {
		free(mqtt_context.discovery_topic);
		mqtt_context.discovery_topic = NULL;
		return -1;
	}
	return 0;
}

#define ADD_STR(F, ...) {\
	int ret = snprintf(mqtt_context.discovery_msg + count, size, F, __VA_ARGS__);\
	count += ret; size -= ret;\
	if (size < 0)	\
		return size; \
	}
// https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
int mqtt_msg_discovery_register(mqtt_discovery_t *discovery)
{
	int count = 0;
	int ccount;
	int size;
	int i;

	if (!wifi_is_connected())
		return -1;

	if (!mqtt_context.discovery_topic) {
		if (discovery_init())
			return -1;
	}

	size = MQTT_OUTPUT_RINGBUF_SIZE - (strlen(mqtt_context.discovery_topic) + 2);
	MQTT_CLIENT_LOCK;
		mqtt_context.discovery_send = false;
	MQTT_CLIENTL_UNLOCK;

	mqtt_context.discovery_msg[0] = 0;
	/* Device section */
	ADD_STR("%s", "{\"dev\":{");
	ADD_STR("\"ids\": \"%s\"", mqtt_context.client_info.client_id);
	if (discovery->dev_name) {
		ADD_STR(",\"name\": \"%s\"", discovery->dev_name);
	} else {
		ADD_STR(",\"name\": \"%s\"", mqtt_context.client_info.client_id);
	}
	if (discovery->dev_manufacture)
		ADD_STR(",\"mf\": \"%s\"", discovery->dev_manufacture);
	if (discovery->dev_model)
		ADD_STR(",\"mdl\": \"%s\"", discovery->dev_model);
	if (discovery->dev_sw_ver)
		ADD_STR(",\"sw\": \"%s\"", discovery->dev_sw_ver);
	if (discovery->dev_hw_ver)
		ADD_STR(",\"hw\": \"%s\"", discovery->dev_hw_ver);
	if (discovery->dev_sn)
		ADD_STR(",\"sn\": \"%s\"", discovery->dev_sn);

	/* Origin section */
	ADD_STR("%s", "},\"o\":{");
	if (!discovery->origin_name)
		return -1;
	ADD_STR("\"name\": \"%s\"", discovery->origin_name);
	if (discovery->origin_sw_ver)
		ADD_STR(",\"sw\": \"%s\"", discovery->origin_sw_ver);
	if (webserv_port())
		ADD_STR(",\"url\": \"http://%s:%d/help\"",
				inet_ntoa(cyw43_state.netif[0].ip_addr), webserv_port());
	ADD_STR("%s", "},");

	/* Components */
	ccount = 0;
	ADD_STR("%s", "\"cmps\":{");
	for (i = 0; i < discovery->comp_count; i++) {
		if (!discovery->components[i].name ||
			!discovery->components[i].platform ||
			!discovery->components[i].id)
			continue;
		if (ccount)
			ADD_STR("%s", ",");
		ADD_STR("\"%s\":{", discovery->components[i].name);
		ADD_STR("\"p\": \"%s\"", discovery->components[i].platform);
		ADD_STR(",\"unique_id\": \"%s_%s\"",
				mqtt_context.client_info.client_id, discovery->components[i].id);
		if (discovery->components[i].dev_class)
			ADD_STR(",\"device_class\": \"%s\"", discovery->components[i].dev_class);
		if (discovery->components[i].unit)
			ADD_STR(",\"unit_of_measurement\": \"%s\"", discovery->components[i].unit);
		if (discovery->components[i].value_template)
			ADD_STR(",\"value_template\": \"%s\"", discovery->components[i].value_template);
		ADD_STR("%s", "}");
		ccount++;
	}
	ADD_STR("%s", "}");

	ADD_STR(",\"state_topic\": \"%s\"", mqtt_context.state_topic);
	ADD_STR(",\"qos\": \"%d\"", discovery->qos);
	ADD_STR("%s", "}");

	MQTT_CLIENT_LOCK;
		mqtt_context.discovery_send = true;
		mqtt_context.discovery_last_send = 0;
	MQTT_CLIENTL_UNLOCK;

	if (IS_DEBUG)
		hlog_info(MQTTLOG, "Registered %d bytes discovery message",
				  strlen(mqtt_context.discovery_msg));

	return size;
}
