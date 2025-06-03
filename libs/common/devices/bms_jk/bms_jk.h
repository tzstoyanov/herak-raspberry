// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _BMS_JK_H_
#define _BMS_JK_H_

#include "pico/stdlib.h"
#include "pico/mutex.h"

#include "herak_sys.h"
#include "common_lib.h"
#include "common_internal.h"

#define BMS_JK_MODULE	"bms_jk"
#define INFO_STR_LEN	16
#define NOTIFY_PACKET_SIZE	300

#define LOG_BT_DEBUG	0x0001
#define LOG_BMC_DEBUG	0x0002
#define LOG_MQTT_DEBUG	0x0004

typedef struct {
	bool valid;
	char Vendor[INFO_STR_LEN];
	char Model[INFO_STR_LEN];
	char Hardware[INFO_STR_LEN];
	char Software[INFO_STR_LEN];
	char ManufacturingDate[INFO_STR_LEN];
	char SerialN[INFO_STR_LEN];
	uint32_t Uptime;
	uint16_t PowerOnCount;
	char PassRead[INFO_STR_LEN];
	char PassSetup[INFO_STR_LEN];
} bms_dev_info_t;

#define BMS_MAX_CELLS	32
#define BMS_MQTT_DATA_LEN   768
#define BMS_MQTT_COMPONENTS (BMS_MAX_CELLS*2 + 30)
typedef struct {
	bool valid;
	bool cell_v_force;
	bool cell_r_force;
	bool data_force;
	bool dev_force;
	uint16_t cells_v[BMS_MAX_CELLS]; // * 0.001, V
	uint16_t cells_res[BMS_MAX_CELLS]; // * 0.001, ohms
	uint32_t cells_enabled;			// bitmask
	uint16_t v_avg;					//  * 0.001, V
	uint16_t v_delta;				//  * 0.001, V
	uint8_t  cell_v_min;			// cell index
	uint8_t  cell_v_max;			// cell index
	uint16_t power_temp;			// * 0.1 *C
	uint32_t cell_warn;				// Bitmask with warnings
	uint32_t batt_volt;				//  * 0.001, V
	uint32_t batt_power;			// ?
	int32_t batt_charge_curr;		// * 0.001, A    //bt_uuid128_t sid = CUSTOM1_SVC_UID;
	//bt_uuid128_t cid = CUSTOM1_CHAR_READ_UID;

	uint16_t batt_temp1;			// * 0.1, *C
	uint16_t batt_temp2;			// * 0.1, *C
	uint16_t batt_temp_mos;			// * 0.1, *C
	uint16_t alarms;				// ?
	uint16_t batt_balance_curr;		// * 0.001, A
	uint8_t  batt_action;			// 0x00: Off; 0x01: Charging; 0x02: Discharging
	uint8_t  batt_state;			// %
	uint32_t batt_cap_rem;			// * 0.001, Ah
	uint32_t batt_cap_nom;			// * 0.001, Ah
	uint32_t batt_cycles;
	uint32_t batt_cycles_cap;		// * 0.001, Ah
	uint8_t soh;					// State Of Health
	uint32_t run_time;				// sec
	uint8_t charge_enable:1;
	uint8_t discharge_enable:1;
	uint8_t precharge_enable:1;
	uint8_t ballance_work:1;
	uint16_t batt_v;                // * 0.001f, V
	uint16_t batt_heat_a;           // * 0.001f, A
} bms_cells_info_t;

typedef struct {
	mqtt_component_t *cells_v;
	mqtt_component_t *cells_res;
	mqtt_component_t *bms_data;
	mqtt_component_t *bms_info;
	mqtt_component_t mqtt_comp[BMS_MQTT_COMPONENTS];
	char payload[BMS_MQTT_DATA_LEN + 1];
	uint8_t send_id;
	uint64_t last_send;
} bms_jk_mqtt_t;

typedef struct {
	bool valid;
	uint32_t char_id;
	uint16_t svc_uid16;
	bt_uuid128_t svc_uid128;
	bt_uuid128_t charc_uid128;
	uint32_t properties; // ATT_PROPERTY_READ | ATT_PROPERTY_WRITE ...
	uint64_t send_time;
	char *desc;
	bool notify;
} bt_charc_t;

#define TERM_IS_ACTIVE(C) ((C)->jk_term_charc.valid)

typedef struct {
	sys_module_t mod;
	bt_addr_t address;
	char *name;
	char *pin;
	int bt_index;
	mutex_t lock;
	uint64_t send_time;
	uint64_t last_reply;
	bt_event_t state;
	bms_dev_info_t dev_info;
	bms_cells_info_t cell_info;
	bool nbuff_ready;
	uint8_t nbuff[NOTIFY_PACKET_SIZE];
	uint16_t nbuff_curr;
	bt_charc_t jk_term_charc;
	bms_jk_mqtt_t mqtt;
	uint32_t debug;
	uint32_t connect_count;
} bms_context_t;

void bms_jk_mqtt_init(bms_context_t *ctx);
void bms_jk_mqtt_send(bms_context_t *ctx);

#endif /* _BMS_JK_H_ */
