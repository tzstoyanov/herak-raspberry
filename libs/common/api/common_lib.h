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

#define LED_ON	{ cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); }
#define LED_OFF { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); }
#define param_get(X) base64_decode(X, X ## _len)

bool system_common_init(void);
void system_common_run(void);
void system_force_reboot(int delay_ms);
void mqtt_msg_publish(char *message, bool force);
uint32_t samples_filter(uint32_t *samples, int total_count, int filter_count);
char *get_current_time_str(char *buf, int buflen);
bool tz_datetime_get(datetime_t *date);
float temperature_internal_get(void);
void dump_hex_data(char *topic, const uint8_t *data, int len);
void dump_char_data(char *topic, const uint8_t *data, int len);

/* LCD API */
int lcd_set_int(int idx, int row, int column, int num);
int lcd_set_double(int idx, int row, int column, double num);
int lcd_set_text(int idx, int row, int column, char *text);
int lcd_clear_cell(int idx);

/* USB API */
bool usb_init(void);
void usb_run(void);
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

typedef void (*log_status_cb_t) (void *context);
int add_status_callback(log_status_cb_t cb, void *user_context);
void debug_log_forward(int client_idx);

/* Bluetooth API */
#define BT_MAX_DEVICES	2
#define BT_MAX_SERVICES	40
#define BT_UUID128_LEN	16
#define UUID_128_FMT "%0.2X%0.2X%0.2X%0.2X-%0.2X%0.2X-%0.2X%0.2X-%0.2X%0.2X-%0.2X%0.2X%0.2X%0.2X%0.2X%0.2X"
#define UUID_128_PARAM(_XX_) (_XX_)[0], (_XX_)[1], (_XX_)[2], (_XX_)[3], (_XX_)[4], (_XX_)[5], (_XX_)[6],\
							 (_XX_)[7], (_XX_)[8], (_XX_)[9], (_XX_)[10], (_XX_)[11], (_XX_)[12], (_XX_)[13],\
							 (_XX_)[14], (_XX_)[15]
typedef uint8_t bt_addr_t[6];
typedef uint8_t bt_uuid128_t[BT_UUID128_LEN];
typedef enum {
	BT_DISCONNECTED = 0,
	BT_CONNECTED,
	BT_NEW_SERVICE,
	BT_NEW_CHARACTERISTIC,
	BT_READY,
	BT_VALUE_RECEIVED
} bt_event_t;
typedef struct {
	uint32_t		svc_id;
	bool			primary;
	uint16_t		uuid16;
	bt_uuid128_t	uuid128;
} bt_service_t;
typedef struct {
	uint32_t		char_id;
	uint32_t		properties;
	uint16_t		uuid16;
	bt_uuid128_t	uuid128;
} bt_characteristic_t;
typedef struct {
	bool val_long;
	uint32_t charId;
	uint8_t *data;
	uint16_t len;
} bt_characteristicvalue_t;
typedef void (*bt_event_handler_t) (int device_idx, bt_event_t event, const void *data, int deta_len, void *context);
int bt_add_known_device(bt_addr_t addr, char *pin, bt_event_handler_t cb, void *context);
int bt_service_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16);
int bt_characteristic_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16);
int bt_characteristic_read(uint32_t char_id);
int bt_characteristic_write(uint32_t char_id, uint8_t *data, uint16_t data_len);
int bt_characteristic_notify(uint32_t char_id, bool enable);

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
typedef enum http_response_id (*webserv_request_cb_t) (int client_idx, char *cmd, char *url, void *context);
int webserv_add_handler(char *url, webserv_request_cb_t user_cb, void *user_data);
int weberv_client_send(int client_idx, char *data, int datalen, enum http_response_id rep);
int weberv_client_send_data(int client_idx, char *data, int datalen);
int weberv_client_close(int client_idx);

/* Web commands API */
typedef void (*web_cmd_cb_t) (int client_idx, char *params, void *user_data);
typedef struct {
	char *command;
	char *help;
	web_cmd_cb_t cb;
} web_requests_t;
int webserv_add_commands(char *url, web_requests_t *commands, int commands_cont, char *description, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_COMMON_H_ */
