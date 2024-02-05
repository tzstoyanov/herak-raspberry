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

#define MSEC_INSEC	60000

#define IS_DEBUG	(mqtt_context.debug != 0)

/* Send a packet each 60s at least, even if the data is the same */
#define MQTT_SEND_MAX_TIME_MSEC	60000	/* 60s */
/* Limit the rate of the packets to*/
#define MQTT_SEND_MIN_TIME_MSEC	60000	/* 60s */

#define DEF_SERVER_PORT	1883
#define DF_MAX_PKT_DELAY_MS	60000
#define DF_MIN_PKT_DELAY_MS	5000

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
	char *topic;
	int server_port;
	uint32_t mqtt_max_delay;
	uint32_t mqtt_min_delay;
	uint32_t max_payload_size;
	enum mqtt_client_state_t state;
	ip_addr_t server_addr;
	ip_resolve_state_t sever_ip_state;
	mqtt_client_t *client;
	struct mqtt_connect_client_info_t client_info;
	bool data_send;
	bool send_in_progerss;
	uint32_t last_send;
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
			mqtt_context.connect_count++;
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

void mqtt_log_status(void)
{
	if (!mqtt_is_connected())
		hlog_info(MQTTLOG, "Not connected to a server, looking for %s ... connect count %d ",
				  mqtt_context.server_url, mqtt_context.connect_count);
	else
		hlog_info(MQTTLOG, "Connected to server %s, publish rate limit between %dppm and %dppm, connect count %d",
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

void mqtt_msg_publish(char *message, bool force)
{
	bool in_progress;
	uint32_t now;
	err_t err;

	if (!mqtt_is_connected())
		return;

	if (strlen(message) > mqtt_context.max_payload_size) {
		hlog_info(MQTTLOG, "Message too big: %d, max payload is %d", strlen(message), mqtt_context.max_payload_size);
		return;
	}

	MQTT_CLIENT_LOCK;
		in_progress = mqtt_context.send_in_progerss;
	MQTT_CLIENTL_UNLOCK;

	if (!mqtt_is_connected() || in_progress) {
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Cannot publish: connected %d, send in progress %d", mqtt_is_connected(), in_progress);
		return;
	}
	mqtt_context.data_send = force;

	/* Rate limit the packets between mqtt_min_delay and mqtt_max_delay */
	now = to_ms_since_boot(get_absolute_time());
	if ((now - mqtt_context.last_send) > mqtt_context.mqtt_max_delay)
		mqtt_context.data_send = true;
	else if ((now - mqtt_context.last_send) < mqtt_context.mqtt_min_delay)
		mqtt_context.data_send = false;
	if (!mqtt_context.data_send && mqtt_context.last_send)
		return;

	LWIP_LOCK_START;
		err = mqtt_publish(mqtt_context.client, mqtt_context.topic, message,
						   strlen(message), MQTT_QOS, MQTT_RETAIN, mqtt_publish_cb, NULL);
	LWIP_LOCK_END;

	if (err == ERR_OK) {
		MQTT_CLIENT_LOCK;
			mqtt_context.send_in_progerss = true;
			mqtt_context.data_send = false;
		MQTT_CLIENTL_UNLOCK;
		if (IS_DEBUG)
			hlog_info(MQTTLOG, "Published %d bytes", strlen(message));
	} else {
		hlog_info(MQTTLOG, "Failed to publish the message: %d", err);
	}

	MQTT_CLIENT_LOCK;
		mqtt_context.last_send = to_ms_since_boot(get_absolute_time());
	MQTT_CLIENTL_UNLOCK;
}

void mqtt_connect(void)
{
	enum mqtt_client_state_t st;
	ip_resolve_state_t res;
	uint32_t last_send;
	uint32_t now;
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

	now = to_ms_since_boot(get_absolute_time());
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
				mqtt_context.last_send = to_ms_since_boot(get_absolute_time());
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
			mqtt_context.last_send = to_ms_since_boot(get_absolute_time());
			if (IS_DEBUG)
				hlog_info(MQTTLOG, "Connected to server %s", mqtt_context.server_url);
		} else {
			mqtt_context.state = MQTT_CLIENT_DISCONNECTED;
			hlog_info(MQTTLOG, "Connecting to MQTT server %s (%s) failed: %d",
					mqtt_context.server_url, inet_ntoa(mqtt_context.server_addr), ret);
		}
	MQTT_CLIENTL_UNLOCK;
}

static int mqtt_get_config(void)
{
	char *str, *tok, *rest;
	int res;

	if (MQTT_SERVER_ENDPOINT_len < 1 || MQTT_TOPIC_len < 1 || MQTT_USER_len < 1)
		return -1;

	mqtt_context.topic = param_get(MQTT_TOPIC);

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
	mqtt_context.max_payload_size = MQTT_OUTPUT_RINGBUF_SIZE - (strlen(mqtt_context.topic) + 2);

	return true;
}

void mqtt_debug_set(uint32_t lvl)
{
	mqtt_context.debug = lvl;
}
