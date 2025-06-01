// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_BT_API_H_
#define _LIB_SYS_BT_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Bluetooth API */
#define BT_MAX_SERVICES	20
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

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_BT_API_H_ */

