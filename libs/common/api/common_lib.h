// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_COMMON_H_
#define _LIB_COMMON_H_

#include "pico/util/datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))
#endif

#ifndef UNUSED
#define UNUSED(x) { (void)(x); }
#endif

#define GPIO_PIN_MIN	0
#define GPIO_PIN_MAX	28

#define LED_ON	{ cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); }
#define LED_OFF { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); }
#define param_get(X) base64_decode(X, X ## _len)

bool system_common_init(void);
void system_common_run(void);
void system_force_reboot(int delay_ms);
void system_common_main(void);

int sys_asprintf(char **strp, const char *fmt, ...);
uint8_t sys_value_to_percent(uint32_t range_min, uint32_t range_max, uint32_t val);

uint32_t samples_filter(uint32_t *samples, int total_count, int filter_count);
char *get_current_time_str(char *buf, int buflen);
bool tz_datetime_get(datetime_t *date);
uint64_t time_msec2datetime(datetime_t *date, uint64_t msec);
char *time_date2str(char *buf, int str_len, datetime_t *date);
uint64_t time_ms_since_boot(void);

float temperature_internal_get(void);
void dump_hex_data(char *topic, const uint8_t *data, int len);
void dump_char_data(char *topic, const uint8_t *data, int len);
void wd_update(void);

typedef enum {
	CMD_CTX_WEB,
	CMD_CTX_MQTT,
} run_type_t;

typedef struct {
	// ToDo
} run_context_mqtt_t;

typedef struct {
	int client_idx;
	bool keep_open;
	bool keep_silent;
	int hret;
} run_context_web_t;

typedef union {
	run_context_web_t web;
	run_context_mqtt_t mqtt;
} run_context_t;

typedef struct {
	run_type_t		type;
	run_context_t	context;
} cmd_run_context_t;

// C - cmd_run_context_t; S - log string
#define WEB_CLIENT_REPLY(C, S)\
	do {if ((C)->type == CMD_CTX_WEB) {\
		weberv_client_send_data((C)->context.web.client_idx, (S), strlen((S)));\
	}} while (0)

typedef int (*app_command_cb_t) (cmd_run_context_t *ctx, char *cmd, char *params, void *user_data);
typedef struct {
	char *command;
	char *help;
	app_command_cb_t cb;
} app_command_t;

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

/* USB API */
bool usb_init(void);
void usb_run(void);
void usb_bus_restart(void);
int usb_send_to_device(int idx, char *buf, int len);
typedef struct {
	uint16_t	vid; /* Vendor ID */
	uint16_t	pid; /* Product ID */
} usb_dev_desc_t;
typedef enum {
	CDC_MOUNT,
	CDC_UNMOUNT,
	HID_MOUNT,
	HID_UNMOUNT,
	HID_REPORT
} usb_event_t;
typedef void (*usb_event_handler_t) (int idx, usb_event_t event, const void *data, int deta_len, void *context);
int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context);

/* Log */
void hlog_any(int severity, const char *topic, const char *fmt, ...);
enum {
	HLOG_EMERG	= 0,
	HLOG_ALERT,
	HLOG_CRIT,
	HLOG_ERR,
	HLOG_WARN,
	HLOG_NOTICE,
	HLOG_INFO,
	HLOG_DEBUG
};
bool hlog_remoute(void);
#define hlog_info(topic, args...) hlog_any(HLOG_INFO, topic, args)
#define hlog_warning(topic, args...) hlog_any(HLOG_WARN, topic, args)
#define hlog_err(topic, args...) hlog_any(HLOG_ERR, topic, args)
#define hlog_dbg(topic, args...) hlog_any(HLOG_DEBUG, topic, args)
#define hlog_null(topic, args...)

/* manchester code  */
uint64_t manchester_encode(uint32_t frame, bool invert);
int manchester_decode(uint64_t mframe, bool invert, uint32_t *value);

typedef bool (*log_status_cb_t) (void *context);
int add_status_callback(log_status_cb_t cb, void *user_context);
void debug_log_forward(int client_idx);

/* WebHook API */
typedef void (*webhook_reply_t) (int idx, int http_code, void *context);
int webhook_state(int idx, bool *connected, bool *sending);
int webhook_send(int idx, char *data, int datalen);
int webhook_add(char *addr, int port, char *content_type, char *endpoint, char *http_command,
				bool keep_open, webhook_reply_t user_cb, void *user_data);

/* WebServer API */
enum http_response_id {
	HTTP_RESP_OK = 0,
	HTTP_RESP_BAD,
	HTTP_RESP_NOT_FOUND,
	HTTP_RESP_INTERNAL_ERROR,
	HTTP_RESP_TOO_MANY_ERROR,
	HTTP_RESP_MAX
};
typedef enum http_response_id (*webserv_request_cb_t) (run_context_web_t *wctx, char *cmd, char *url, void *context);
int weberv_client_send(int client_idx, char *data, int datalen, enum http_response_id rep);
int weberv_client_send_data(int client_idx, char *data, int datalen);

#define WEB_CMD_NR   "\r\n"
/* Web commands API */
int webserv_add_commands(char *url, app_command_t *commands, int commands_cont, char *description, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_COMMON_H_ */
