// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_MQTT_INTERNAL_H_
#define _LIB_SYS_MQTT_INTERNAL_H_

#define MQTT_MODULE	"mqtt"

#define MQTT_QOS		0
#define MQTT_RETAIN		1

#define MQTT_DISCOVERY_MAX_COUNT	512
#define MQTT_DISCOVERY_BUFF_SIZE	640
#define MQTT_MAX_TOPIC_SIZE	96

#define ONLINE_MSG				"online"
#define OFFLINE_MSG				"offline"

#define MSEC2MIN	60000ULL

#define IS_DEBUG(C)	((C)->debug)

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
	uint32_t max_payload_size;
	uint32_t max_ppm;
	enum mqtt_client_state_t state;
	ip_addr_t server_addr;
	ip_resolve_state_t sever_ip_state;
	mqtt_client_t *client;
	struct mqtt_connect_client_info_t client_info;
	int send_in_progress;
	uint64_t send_start;
	uint64_t last_send;
	uint32_t filter_pkt_count;
	uint64_t filter_pkt_send;
	uint32_t connect_count;
	uint32_t debug;
	uint8_t send_err_count;
};

struct mqtt_context_t *mqtt_context_get(void);
bool mqtt_is_connected_ctx(struct mqtt_context_t *ctx);

int mqtt_msg_send(struct mqtt_context_t *ctx, char *topic, char *message);
int mqtt_msg_discovery_send(struct mqtt_context_t *ctx);
int mqtt_msg_discovery_send_device(struct mqtt_context_t *ctx);

int mqtt_cmd_subscribe(struct mqtt_context_t *ctx);
bool mqtt_incoming_ready(struct mqtt_context_t *ctx);
void mqtt_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags);
void mqtt_incoming_publish(void *arg, const char *topic, u32_t tot_len);

#endif /* _LIB_SYS_MQTT_INTERNAL_H_*/

