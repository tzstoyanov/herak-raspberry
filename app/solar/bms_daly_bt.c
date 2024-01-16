// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include <btstack_util.h>

#include "solar.h"

#define BMS	"bms"

//#define BMS_DEBUG

#define STATE_WAIT_MS	20000 /* Send command each 20s */
#define TERM_WAIT_MS	5000 /* Wait 5s for response from terminal */

#define BMS_LOCAL_LOCK		mutex_enter_blocking(&bms_context.lock)
#define BMS_LOCAL_UNLOCK	mutex_exit(&bms_context.lock)

//#define BMS_LOCAL_LOCK
//#define BMS_LOCAL_UNLOCK


/* HLK B20 custom service		0000fff0-0000-1000-8000-00805f9b34fb */
/* HLK B20 read characteristic	0000fff1-0000-1000-8000-00805f9b34fb (Read/Notify) */
/* HLK B20 write characteristic	0000fff2-0000-1000-8000-00805f9b34fb (Write Without Response) */
/*							    0000FFF2-0000-1000-8000-00805F9B34FB, properties 0xE */
#define SERIAL_SVC_UID		  {0x00, 0x00, 0xff, 0xf0, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}
#define SERIAL_CHAR_READ_UID  {0x00, 0x00, 0xff, 0xf1, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}
#define SERIAL_CHAR_WRITE_UID {0x00, 0x00, 0xff, 0xf2, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}

// Dummy mouse test UID
// SVC:	00001812-0000-1000-8000-00805F9B34FB
// Read: 00002A4E-0000-1000-8000-00805F9B34FB
// Write: 00002A4C-0000-1000-8000-00805F9B34FB
//#define SERIAL_SVC_UID {0x00, 0x00, 0x18, 0x12, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
//#define SERIAL_CHAR_READ_UID {0x00, 0x00, 0x2A, 0x4E, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}
//#define SERIAL_CHAR_WRITE_UID {0x00, 0x00, 0x2A, 0x4C, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}

static uint8_t bt_serial_svc[16] = SERIAL_SVC_UID;
static uint8_t bt_read_char[16] = SERIAL_CHAR_READ_UID;
static uint8_t bt_write_char[16] = SERIAL_CHAR_WRITE_UID;

// ATT_PROPERTY_...

/* BMS Daly data */
#define DALY_MAX_CELLS	48
#define DALY_MAX_TEMPS	16
struct bms_daly_state_t {
	/* 0x90 */
	float bat_v;			/* Cumulative total voltage (0.1 V) */
	float acquisition_v;	/* Gather total voltage (0.1 V) */
	float bat_i;			/* Current (30000 Offset ,0.1A) */
	float soc_p;			/* SOC (0.1%) */

	/* 0x91 */
	uint16_t bat_maxv;		/* Maximum cell voltage value (mV) */
	uint8_t bat_maxv_cell;	/* No of cell with Maximum voltage */
	uint16_t bat_minv;		/* Minimum cell voltage value (mV) */
	uint8_t bat_minv_cell;	/* No of cell with Minimum voltage */

	/* 0x92 */
	uint8_t bat_maxt;		/* Maximum temperature value (40 Offset ,°C) */
	uint8_t bat_maxt_cell;	/* Maximum temperature cell No */
	uint8_t bat_mint;		/* Minimum temperature value (40 Offset ,°C) */
	uint8_t bat_mint_cell;	/* Minimum temperature cell No */

	/* 0x93 */
	uint8_t charge_state;	/* State (0 stationary 1 charge 2 discharge) */
	uint8_t charge_mos;		/* Charge MOS state */
	uint8_t discharge_mos;	/* Discharge MOS status */
	uint8_t bms_life;		/* BMS life (0~255 cycles) */
	uint32_t remain_capacity;/* Remain capacity (mAH) */

	/* 0x94 */
	uint8_t cells;			/* number of battery cells */
	uint8_t t_sensors;		/* number of temperature sensors */
	uint8_t charger_running; /* Charger status (0 disconnect 1 access) */
	uint8_t load_running;	/* Load status (0 disconnect 1 access) */
	uint8_t	dio_states;	/*	Bit 0:DI1, Bit 1:DI2, Bit 2:DI3, Bit 3:DI4
						 *	Bit 4:DO1, Bit 5:DO2, Bit 6:DO3, Bit 7:DO4
						 */

	/* 0x95 */
	uint16_t	cells_voltage[DALY_MAX_CELLS];		/* Cell voltage (1 mV) */
	/* 0x96 */
	uint16_t	cells_temperature[DALY_MAX_TEMPS];	/* cell temperature(40 Offset ,℃) */
	/* 0x97 */
	uint8_t	cells_states[DALY_MAX_CELLS/8];			/* 1 bit per cell: 0： Closed 1： Open */

	/* 0x98 */
	uint8_t	fail_status[7];
	uint8_t	fail_code;
};

#define IS_TERMINAL_READY	(bms_context.terminal.readId && bms_context.terminal.writeId)

struct bt_terminal_t {
	uint32_t	readId;
	uint32_t	writeId;
	bool wait_response;
	uint32_t send_time;
};

static struct {
	bt_addr_t address;
	char *name;
	char *pin;
	int bt_index;
	mutex_t lock;
	struct bt_terminal_t terminal;
	bt_event_t state;
	struct bms_daly_state_t data;
	uint32_t	read_chars[BT_MAX_SERVICES];
	uint8_t		rchars_count;
	int qcommand;
} bms_context;

#define GET_U16(_dd_) ((((char *)(_dd_))[0] << 8) | (((char *)(_dd_))[1]))
#define GET_U32(_dd_) ((((char *)(_dd_))[0] << 0x18) | (((char *)(_dd_))[1]) << 0x10 | (((char *)(_dd_))[2]) << 0x08 | ((char *)(_dd_))[3])

static void bms_send_mqtt_data(void)
{
	mqtt_bms_data_t data;

	data.bat_v = bms_context.data.bat_v;
	data.bat_i = bms_context.data.bat_i;
	data.soc_p = bms_context.data.soc_p;
	data.bms_life = bms_context.data.bms_life;
	data.remain_capacity = bms_context.data.remain_capacity;

	mqtt_data_bms(&data);
}

/* Current V, A, % */
static void d90_cmd_process(uint8_t *buf)
{
	bms_context.data.bat_v = GET_U16(buf) / 10;
	bms_context.data.acquisition_v =  GET_U16(buf+2) / 10;
	bms_context.data.bat_i = (GET_U16(buf + 4) - 30000) / 10;
	bms_context.data.soc_p = GET_U16(buf + 8) / 10;
}

/* Max / Min cell voltage */
static void d91_cmd_process(uint8_t *buf)
{
	bms_context.data.bat_maxv = GET_U16(buf);
	bms_context.data.bat_maxv_cell = buf[2];
	bms_context.data.bat_minv = GET_U16(buf + 3);
	bms_context.data.bat_minv_cell = buf[5];
}

/* Max / Min cell temperature */
static void d92_cmd_process(uint8_t *buf)
{
	bms_context.data.bat_maxt = buf[0] - 40;
	bms_context.data.bat_maxt_cell = buf[1];
	bms_context.data.bat_mint = buf[2] - 40;
	bms_context.data.bat_mint_cell = buf[3];
}
/* Charge and Discharge*/
static void d93_cmd_process(uint8_t *buf)
{
	bms_context.data.charge_state = buf[0];
	bms_context.data.charge_mos = buf[1];
	bms_context.data.discharge_mos = buf[2];
	bms_context.data.bms_life = buf[3];
	bms_context.data.remain_capacity = GET_U32(buf + 4);
}

/* Status 1 */
static void d94_cmd_process(uint8_t *buf)
{
	bms_context.data.cells = buf[0];
	bms_context.data.t_sensors = buf[1];
	bms_context.data.charger_running = buf[2];
	bms_context.data.load_running = buf[3];
	bms_context.data.dio_states = buf[4];
}

/* Cell voltage 1~48 */
static void d95_cmd_process(uint8_t *buf)
{
	int frame = buf[0];

	if (frame >= 16)
		return;
	frame *= 3;
	bms_context.data.cells_voltage[frame] = GET_U16(buf+1);
	bms_context.data.cells_voltage[frame+1] = GET_U16(buf+3);
	bms_context.data.cells_voltage[frame+2] = GET_U16(buf+5);
}

/* Cell temperature 1~16 */
static void d96_cmd_process(uint8_t *buf)
{
	int frame = buf[0];
	int i;

	if (frame >= 3)
		return;
	frame *= 7;
	for (i = 0; i < 7; i++)
		bms_context.data.cells_temperature[frame+i] = buf[i+1] - 40;
}

/* Cell balance State 1~48 */
static void d97_cmd_process(uint8_t *buf)
{
	int i;

	for (i = 0; i < 6; i++)
		bms_context.data.cells_states[i] = buf[i];
}

/* Battery failure status */
static void d98_cmd_process(uint8_t *buf)
{
	int i;

	for (i = 0; i < 7; i++)
		bms_context.data.fail_status[i] = buf[i];
	bms_context.data.fail_code = buf[7];
}

typedef void (*cmd_handler_t) (uint8_t *buf);
static struct {
	daly_qcmd_t id;
	cmd_handler_t cb;
} daly_commnds_handler[] = {
		{DALY_90, d90_cmd_process},
		{DALY_91, d91_cmd_process},
		{DALY_92, d92_cmd_process},
		{DALY_93, d93_cmd_process},
		{DALY_94, d94_cmd_process},
		{DALY_95, d95_cmd_process},
		{DALY_96, d96_cmd_process},
		{DALY_97, d97_cmd_process},
		{DALY_98, d98_cmd_process}
};

static void check_terminal(bt_characteristic_t *charc)
{
	bt_uuid128_t svc128;

	if (bt_service_get_uuid(charc->char_id, &svc128, NULL))
		return;

	if (memcmp(svc128, bt_serial_svc, BT_UUID128_LEN))
		return;

	if (!memcmp(charc->uuid128, bt_read_char, BT_UUID128_LEN) &&
		(charc->properties & ATT_PROPERTY_READ))
		bms_context.terminal.readId = charc->char_id;

	if (!memcmp(charc->uuid128, bt_write_char, BT_UUID128_LEN) &&
		(charc->properties & ATT_PROPERTY_WRITE_WITHOUT_RESPONSE))
		bms_context.terminal.writeId = charc->char_id;
}

static void process_response(daly_qcmd_t cmd, uint8_t *buf)
{
	static int qsize = ARRAY_SIZE(daly_commnds_handler);
	int i;

	for (i = 0; i < qsize; i++) {
		if (daly_commnds_handler[i].id == cmd) {
			if (daly_commnds_handler[i].cb)
				daly_commnds_handler[i].cb(buf);
			break;
		}
	}
}

static void daly_bt_process_data(bt_characteristicvalue_t *val)
{
	const char *qcmd, *qdesc;
	daly_qcmd_t cmd;

	if (IS_TERMINAL_READY && val->charId == bms_context.terminal.readId) {
		bms_context.terminal.wait_response = false;
		hlog_info(BMS, "Got %d bytes %s data from terminal",
				  val->len, val->val_long?"long":"short");
		cmd = bms_verify_response(val->data, val->len);
		if (cmd < DALY_MAX && !bms_get_qcommand_desc(cmd, &qcmd, &qdesc)) {
			hlog_info(BMS, "Got response [%s] %s", qcmd, qdesc);
			process_response(cmd, val->data + 4);
			bms_send_mqtt_data();
		} else {
			hlog_info(BMS, "Invalid terminal response:");
			dump_hex_data(BMS, val->data, val->len);
		}
	} else {
		hlog_info(BMS, "Got %d bytes %s data, but terminal is not ready %d ",
				  val->len, val->val_long?"long":"short", IS_TERMINAL_READY);
#if BMS_DEBUG
		{
			bt_uuid128_t svc128, char128;

			if (!bt_service_get_uuid(val->charId, &svc128, NULL) && !bt_characteristic_get_uuid(val->charId, &char128, NULL)) {
				hlog_info(BMS, "Got %d bytes %s data from svc ["UUID_128_FMT"], characteristic ["UUID_128_FMT"]:",
						  val->len, val->val_long?"long":"short", UUID_128_PARAM(svc128), UUID_128_PARAM(char128));
			} else {
				hlog_info(BMS, "Got %d bytes %s data from uknown characteristic",
						  val->len, val->val_long?"long":"short");
			}
			dump_hex_data(BMS, val->data, val->len);
		}
#endif
	}


}

static void daly_bt_event(int idx, bt_event_t event, const void *data, int data_len, void *context)
{
	bt_characteristic_t *charc;
	bt_service_t *svc;
	uint16_t u16;

	UNUSED(context);
	BMS_LOCAL_LOCK;
		if (idx != bms_context.bt_index)
			goto out;
		switch (event) {
		case BT_CONNECTED:
			bms_context.state = BT_CONNECTED;
			free(bms_context.name);
			bms_context.name = strdup(data);
			memset(&bms_context.terminal, 0, sizeof(bms_context.terminal));
			if (bms_context.state != BT_CONNECTED)
				hlog_info(BMS, "Connected to %s", bms_context.name);
			bms_context.state = BT_CONNECTED;
			break;
		case BT_DISCONNECTED:
			if (bms_context.state != BT_DISCONNECTED)
				hlog_info(BMS, "Disconnected from %s", bms_context.name);
			bms_context.state = BT_DISCONNECTED;
			free(bms_context.name);
			bms_context.name = NULL;
			memset(&bms_context.terminal, 0, sizeof(bms_context.terminal));
			break;
		case BT_READY:
			if (bms_context.state != BT_READY)
				hlog_info(BMS, "Device %s is ready, terminal is %s",
					  bms_context.name, IS_TERMINAL_READY?"ready":"not ready");
			bms_context.state = BT_READY;
			break;
		case BT_NEW_SERVICE:
			if (data_len != sizeof(bt_service_t))
				break;
			svc = (bt_service_t *)data;
			hlog_info(BMS, "New service discovered (0x%X): ["UUID_128_FMT"]", svc->uuid16, UUID_128_PARAM(svc->uuid128));
			break;
		case BT_NEW_CHARACTERISTIC:
			if (data_len != sizeof(bt_characteristic_t))
				break;
			charc = (bt_characteristic_t *)data;
			check_terminal(charc);
			if ((charc->properties & ATT_PROPERTY_READ) && bms_context.rchars_count < BT_MAX_SERVICES)
				bms_context.read_chars[bms_context.rchars_count++] = charc->char_id;
			if (!bt_service_get_uuid(charc->char_id, NULL, &u16))
				hlog_info(BMS, "New characteristic of service (0x%X) discovered (0x%X): ["UUID_128_FMT"]", u16, UUID_128_PARAM(charc->uuid128));
			else
				hlog_info(BMS, "New characteristic of uknown service discovered: ["UUID_128_FMT"]", UUID_128_PARAM(charc->uuid128));
			break;
		case BT_VALUE_RECEIVED:
			if (data_len == sizeof(bt_characteristicvalue_t) && bms_context.state == BT_READY)
				daly_bt_process_data((bt_characteristicvalue_t *)data);
			break;
		}
out:
	BMS_LOCAL_UNLOCK;
}

void bms_solar_query(void)
{
	const char *qcmd, *qdesc;
	static uint8_t rchar;
	uint32_t now;
	int len;
	uint8_t *cmd;

	now = to_ms_since_boot(get_absolute_time());
	BMS_LOCAL_LOCK;
		if (bms_context.state != BT_READY)
			goto out;
		if (IS_TERMINAL_READY) {
			if (bms_context.terminal.wait_response && (now - bms_context.terminal.send_time) < TERM_WAIT_MS)
				goto out;
			bms_context.terminal.wait_response = false;
			cmd = bms_get_qcommand(bms_context.qcommand, &len);
			if (cmd && !bt_characteristic_write(bms_context.terminal.writeId, cmd, len)) {
				bms_context.terminal.send_time = now;
				bms_context.terminal.wait_response = true;
				bms_get_qcommand_desc(bms_context.qcommand, &qcmd, &qdesc);
				hlog_info(BMS, "Sent to device %d bytes query %d: [%s] (%s)",
						  len, bms_context.qcommand, qcmd, qdesc);
				busy_wait_ms(20);
				bt_characteristic_read(bms_context.terminal.readId);
			} else {
				hlog_info(BMS, "Failed to send command %d", bms_context.qcommand);
			}
			bms_context.qcommand++;
			if (bms_context.qcommand > DALY_98)
				bms_context.qcommand = 0;
		} else {
			if (rchar >= bms_context.rchars_count)
				rchar = 0;
//			bt_characteristic_read(bms_context.read_chars[rchar++]);
		}
out:
	BMS_LOCAL_UNLOCK;
}

static bool get_bms_config(void)
{
	bool ret = false;
	char *bt_id = NULL;
	char *rest, *rest1;
	char *tok;
	int  i;

	if (BMS_DALY_BT_len < 1)
		return false;

	bt_id = param_get(BMS_DALY_BT);
	if (!bt_id || strlen(bt_id) < 1)
		goto out;
	rest = bt_id;
	tok = strtok_r(rest, ";", &rest);
	if (!tok)
		goto out;
	bms_context.pin = strdup(rest);
	if (!bms_context.pin)
		goto out;

	rest1 = tok;
	i = 0;
	while ((tok = strtok_r(rest1, ":", &rest1))  && i < 6)
		bms_context.address[i++] = (int)strtol(tok, NULL, 16);
	if (i != 6)
		goto out;

	ret = true;
out:
	free(bt_id);
	if (!ret) {
		free(bms_context.pin);
		bms_context.pin = NULL;
	}
	return ret;
}

bool bms_solar_init(void)
{
	memset(&bms_context, 0, sizeof(bms_context));
	mutex_init(&bms_context.lock);
	bms_context.state = BT_DISCONNECTED;

	if (!get_bms_config())
		return false;
	bms_context.bt_index = bt_add_known_device(bms_context.address, bms_context.pin, daly_bt_event, NULL);
	if (bms_context.bt_index < 1)
		return false;

	return true;
}
