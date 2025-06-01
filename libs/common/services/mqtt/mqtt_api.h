// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_MQTT_API_H_
#define _LIB_SYS_MQTT_API_H_

#ifdef __cplusplus
extern "C" {
#endif

// https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
typedef struct {
	char *module;		// mandatory
	char *name;			// mandatory
	char *platform;		// mandatory - sensor, switch, number ...
	char *dev_class;	// temperature, humidity ...
	char *unit;			// unit of measurement
	char *value_template;
	char *payload_on;
	char *payload_off;
	char *state_topic;
	int id;
	bool force;
	uint64_t last_send;
} mqtt_component_t;

/* MQTT */
#define MQTT_DEV_QOS    2
typedef void (*mqtt_msg_receive_cb_t)(char *topic, char *data, uint16_t len, void *context);
int mqtt_msg_publish(char *topic, char *message, bool force);
int mqtt_msg_component_publish(mqtt_component_t *component, char *message);
int mqtt_msg_component_register(mqtt_component_t *component);
int mqtt_add_commands(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data);

bool mqtt_is_connected(void);
bool mqtt_is_discovery_sent(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_MQTT_API_H_*/

