// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include <btstack_util.h>
#include "pico/platform/sections.h"
#include "pico/util/datetime.h"

#include "base64.h"
#include "params.h"
#include "bms_jk.h"

#define BMC_DEBUG(C)	(C->debug)

#define CMD_POLL_MS	5000 /* Send command each 5s */
#define CMD_TIMEOUT_MS	1000 /* Wait for response 1s */
static const uint8_t __in_flash() jk_notify_pkt_start[] = {0x55, 0xAA, 0xEB, 0x90};
static const uint8_t __in_flash() jk_request_pkt_start[] = {0xAA, 0x55, 0x90, 0xEB};

#define TIME_STR_LEN	64

#define	WH_PAYLOAD_TEMPLATE "Battery %s is %s"

/*

Device Information 0x180A
	char 00002a29 0000 1000 8000 00805F9B34FB; Manufacturer name; string, Read
		[BEKEN SAS]
	char 00002a24 0000 1000 8000 00805F9B34FB; Model Number; string; Read
		[BK-BLE-1.0]
	char 00002a25 0000 1000 8000 00805F9B34FB; Serial Number; string; Read
		[1.0.0.0-LE]
	char 00002a27 0000 1000 8000 00805F9B34FB; Hardware Revision; string; Read
		[1.0.0]
	char 00002a26 0000 1000 8000 00805F9B34FB; Firmware Revision; string; Read
		[6.1.2]
	char 00002a28 0000 1000 8000 00805F9B34FB; Software Revision; string; Read
		[6.3.0]
	char 00002a23 0000 1000 8000 00805F9B34FB; System ID; ?; Read
		[4V]
	char 00002a50 0000 1000 8000 00805F9B34FB; PnP ID; ?; Read
		[^@]

Generic Access 0x1800  ??
Battery Service 0x1800
	char 00002a19 0000 1000 8000 00805F9B34FB; Read Notify
		[0%]

Custom Service 0000FFE0 0000 1000 8000 00805F9B34FB ??
Custom Service F000FFC0 0451 4000 B000 000000000000
	char F000FFC1 0451 4000 B000 000000000000; Write Notify
	char F000FFC2 0451 4000 B000 000000000000; Write Notify

*/

/* Terminal */
static const bt_uuid128_t __in_flash() _terminal_svc = {0x00, 0x00, 0xFF, 0xE0, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
static const bt_uuid128_t __in_flash() _terminal_charc_read = {0x00, 0x00, 0xff, 0xe1, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
//static const bt_uuid128_t __in_flash() _terminal_charc_write = {0x00, 0x00, 0xff, 0xe2, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};


enum {
	JK_COMMAND_CELL_INFO = 0x96,
	JK_COMMAND_DEVICE_INFO = 0x97,
};

static bms_context_t *__bms_jk_context;

static bms_context_t *bms_jk_context_get(void)
{
	return __bms_jk_context;
}


static void charc_new(struct jk_bms_dev_t *dev, bt_characteristic_t *charc)
{
	bt_uuid128_t svc128;
	uint16_t	svc16;

	if (bt_service_get_uuid(charc->char_id, &svc128, &svc16))
		return;
	if (memcmp(dev->jk_term_charc.svc_uid128, svc128, BT_UUID128_LEN))
		return;
	if (memcmp(dev->jk_term_charc.charc_uid128, charc->uuid128, BT_UUID128_LEN))
		return;

	dev->jk_term_charc.char_id = charc->char_id;
	dev->jk_term_charc.properties = charc->properties;
	dev->jk_term_charc.valid = true;

	if (BMC_DEBUG(dev->ctx))
		hlog_info(BMS_JK_MODULE, "Got new characteristic [%s] %d: properties 0x%X, svc 0x%X ("UUID_128_FMT"), "UUID_128_FMT"",
				  dev->jk_term_charc.desc, charc->char_id, charc->properties, svc16, svc128, charc->uuid128);

}

static void charc_reset(struct jk_bms_dev_t *dev)
{
	dev->jk_term_charc.valid = false;
	dev->jk_term_charc.send_time = 0;
	dev->jk_term_charc.notify = false;
	dev->nbuff_curr = 0;
	dev->nbuff_ready = false;
	dev->wait_reply = false;
}

#define DATA_UINT8_GET(C, OFS)	 ((uint8_t)(C)->nbuff[(OFS)])
#define DATA_UINT16_GET(C, OFS) ((((uint16_t)((C)->nbuff[(OFS) + 1])) << 8) | ((uint16_t)((C)->nbuff[(OFS)])))
#define DATA_UINT32_GET(C, OFS) ((((uint32_t)(DATA_UINT16_GET((C), (OFS) + 2))) << 16) | ((uint32_t)(DATA_UINT16_GET((C), (OFS)))))
#define DATA_INT32_GET(C, OFS) ((int32_t)((((uint32_t)(DATA_UINT16_GET((C), (OFS) + 2))) << 16) | ((uint32_t)(DATA_UINT16_GET((C), (OFS))))))

#define CELL_FRAME_VOLT				6	// 2 bytes, uint16 * 0.001, V ... 32 cells
#define CELL_FRAME_ENABLES_CELLS	70	// 4 bytes, bitmask
#define CELL_FRAME_VOLT_AVG			74	// 2 bytes, uint16 * 0.001, V
#define CELL_FRAME_VOLT_DELTA		76	// 2 bytes, uint16 * 0.001, V
#define CELL_FRAME_CELL_MAX			78	// 1 byte, index
#define CELL_FRAME_CELL_MIN			79	// 1 byte, index
#define CELL_FRAME_RESISTANCE		80	// 2 bytes, uint16 * 0.001... 32 cells
#define CELL_FRAME_POWER_TEMP		144	// 2 bytes, float16, *C
#define CELL_FRAME_CELL_WARN		146	// 4 bytes, bitmask
#define CELL_FRAME_BATT_VOLT		150	// 4 bytes, uint32 * 0.001, V
#define CELL_FRAME_BATT_POWER		154	// 4 bytes, uint32, ?
#define CELL_FRAME_BATT_CHARGE		158	// 4 bytes, uint32 * 0.001, A
#define CELL_FRAME_TEMP_1			162	// 2 bytes, uint16 * 0.1, *C
#define CELL_FRAME_TEMP_2			164	// 2 bytes, uint16 * 0.1, *C
#define CELL_FRAME_TEMP_MOS			166	// 2 bytes, uint16 * 0.1, *C
#define CELL_FRAME_ALARM			168	// 2 bytes, uint16 ?
#define CELL_FRAME_BATT_BALANCE		170	// 2 bytes, uint16  * 0.001, A
#define CELL_FRAME_BATT_ACTION		172	// 1 byte,  uint8  0x00: Off; 0x01: Charging; 0x02: Discharging
#define CELL_FRAME_BATT_STATE		173	// 1 byte,  uint8  %
#define CELL_FRAME_BATT_CAP_REMAIN	174	// 4 bytes, uint32 * 0.001, Ah
#define CELL_FRAME_BATT_CAP_NOMINAL	178	// 4 bytes, uint32 * 0.001, Ah
#define CELL_FRAME_CYCLE_COUNT		182	// 4 bytes, uint32
#define CELL_FRAME_CYCLE_CAP		186	// 4 bytes, uint32 * 0.001, Ah
#define CELL_FRAME_SOH				190	// 1 byte,  State of health
#define CELL_FRAME_PRECHARGE		191	// 1 byte
#define CELL_FRAME_USER_ALARM		192	// 2 bytes
#define CELL_FRAME_RUNTIME			194	// 4 bytes, uint32 sec
#define CELL_FRAME_CHARGE_ENABLE	198	// 1 byte
#define CELL_FRAME_DISCHARGE_ENABLE	199	// 1 byte
#define CELL_FRAME_PRECHARGE_ENABLE	200	// 1 byte
#define CELL_FRAME_BALANCER_WORK	201	// 1 byte
#define CELL_FRAME_DISCHR_OVERC_PROT_TIMER	202	// 2 bytes, uint16
#define CELL_FRAME_DISCHR_SC_PROT_TIMER		204	// 2 bytes, uint16
#define CELL_FRAME_CHR_OVERC_PROT_TIMER		206	// 2 bytes, uint16
#define CELL_FRAME_CHR_SC_PROT_TIMER		208	// 2 bytes, uint16
#define CELL_FRAME_UDERV_PROT_TIMER		210	// 2 bytes, uint16
#define CELL_FRAME_OVERV_PROT_TIMER		212	// 2 bytes, uint16
#define CELL_FRAME_TEMP_PRESENCE		214	// 2 bytes, bitmask bits <1..5>
#define CELL_FRAME_HEAT_SENSOR			216	// 2 bytes
#define CELL_FRAME_TIME_EMERG			218	// 2 bytes, uint16
#define CELL_FRAME_DISCH_CURR_CORR		220	// 2 bytes, uint16
#define CELL_FRAME_CHR_CURR				222	// 2 bytes, uint16  * 0.001
#define CELL_FRAME_DISCHR_CURR			224	// 2 bytes, uint16  * 0.001
#define CELL_FRAME_BATTV_CORR			226	// 4 bytes, float32
/* .. */
#define CELL_FRAME_BATTV				234	// 2 bytes, float16
#define CELL_FRAME_BATT_HEAT_CURR		236	// 2 bytes, float16 * 0.001f

#define BMS_DATA_READ(S, V)\
	{ if ((S) != (V)) { dev->cell_info.data_force = true; (S) = (V); }}
static void jk_bt_process_cell_frame(struct jk_bms_dev_t *dev)
{
	uint16_t d;
	int i;

	dev->cell_info.valid = true;
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		d = DATA_UINT16_GET(dev, CELL_FRAME_VOLT + i * 2); // * 0.001f
		if (dev->cell_info.cells_v[i] != d)
			dev->cell_info.cell_v_force = true;
		dev->cell_info.cells_v[i] = d;
	}
	BMS_DATA_READ(dev->cell_info.cells_enabled, DATA_UINT32_GET(dev, CELL_FRAME_ENABLES_CELLS));
	BMS_DATA_READ(dev->cell_info.v_avg, DATA_UINT16_GET(dev, CELL_FRAME_VOLT_AVG));			// * 0.001f
	BMS_DATA_READ(dev->cell_info.v_delta, DATA_UINT16_GET(dev, CELL_FRAME_VOLT_DELTA));		// * 0.001f
	BMS_DATA_READ(dev->cell_info.cell_v_max, DATA_UINT8_GET(dev, CELL_FRAME_CELL_MAX));
	BMS_DATA_READ(dev->cell_info.cell_v_min, DATA_UINT8_GET(dev, CELL_FRAME_CELL_MIN));
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		d = DATA_UINT16_GET(dev, CELL_FRAME_RESISTANCE + i * 2); // * 0.001f
		if (dev->cell_info.cells_res[i] != d)
			dev->cell_info.cell_r_force = true;
		dev->cell_info.cells_res[i] = d;
	}
	BMS_DATA_READ(dev->cell_info.power_temp, DATA_UINT16_GET(dev, CELL_FRAME_POWER_TEMP));	// * 0.1f
	BMS_DATA_READ(dev->cell_info.cell_warn, DATA_UINT32_GET(dev, CELL_FRAME_CELL_WARN));
	BMS_DATA_READ(dev->cell_info.batt_volt, DATA_UINT32_GET(dev, CELL_FRAME_BATT_VOLT));	// * 0.001f
	BMS_DATA_READ(dev->cell_info.batt_power, DATA_UINT32_GET(dev, CELL_FRAME_BATT_POWER));
	BMS_DATA_READ(dev->cell_info.batt_charge_curr, DATA_INT32_GET(dev, CELL_FRAME_BATT_CHARGE)); // * 0.001f
	BMS_DATA_READ(dev->cell_info.batt_temp1, DATA_UINT16_GET(dev, CELL_FRAME_TEMP_1));		// * 0.1f
	BMS_DATA_READ(dev->cell_info.batt_temp2, DATA_UINT16_GET(dev, CELL_FRAME_TEMP_2));		// * 0.1f
	BMS_DATA_READ(dev->cell_info.batt_temp_mos, DATA_UINT16_GET(dev, CELL_FRAME_TEMP_MOS));	// * 0.1f
	BMS_DATA_READ(dev->cell_info.alarms, DATA_UINT16_GET(dev, CELL_FRAME_ALARM));
	BMS_DATA_READ(dev->cell_info.batt_balance_curr, DATA_UINT16_GET(dev, CELL_FRAME_BATT_BALANCE));	// * 0.001f
	BMS_DATA_READ(dev->cell_info.batt_action, DATA_UINT8_GET(dev, CELL_FRAME_BATT_ACTION));
	BMS_DATA_READ(dev->cell_info.batt_state, DATA_UINT8_GET(dev, CELL_FRAME_BATT_STATE));
	BMS_DATA_READ(dev->cell_info.batt_cap_rem, DATA_UINT32_GET(dev, CELL_FRAME_BATT_CAP_REMAIN));	// * 0.001f
	BMS_DATA_READ(dev->cell_info.batt_cap_nom, DATA_UINT32_GET(dev, CELL_FRAME_BATT_CAP_NOMINAL));	// * 0.001f
	BMS_DATA_READ(dev->cell_info.batt_cycles, DATA_UINT32_GET(dev, CELL_FRAME_CYCLE_COUNT));
	BMS_DATA_READ(dev->cell_info.batt_cycles_cap, DATA_UINT32_GET(dev, CELL_FRAME_CYCLE_CAP));	// * 0.001f
	BMS_DATA_READ(dev->cell_info.soh, DATA_UINT8_GET(dev, CELL_FRAME_SOH));
	BMS_DATA_READ(dev->cell_info.run_time, DATA_UINT32_GET(dev, CELL_FRAME_RUNTIME));
	BMS_DATA_READ(dev->cell_info.charge_enable, DATA_UINT8_GET(dev, CELL_FRAME_CHARGE_ENABLE));
	BMS_DATA_READ(dev->cell_info.discharge_enable, DATA_UINT8_GET(dev, CELL_FRAME_DISCHARGE_ENABLE));
	BMS_DATA_READ(dev->cell_info.precharge_enable, DATA_UINT8_GET(dev, CELL_FRAME_PRECHARGE_ENABLE));
	BMS_DATA_READ(dev->cell_info.ballance_work, DATA_UINT8_GET(dev, CELL_FRAME_BALANCER_WORK));
	BMS_DATA_READ(dev->cell_info.batt_v, DATA_UINT16_GET(dev, CELL_FRAME_BATTV)); // ?
	BMS_DATA_READ(dev->cell_info.batt_heat_a, DATA_UINT16_GET(dev, CELL_FRAME_BATT_HEAT_CURR)); // * 0.001f
}

#define DEV_FRAME_MODEL			6	// 16 bytes, string
#define DEV_FRAME_HW			22	// 8 bytes, string
#define DEV_FRAME_SW			30	// 8 bytes, string
#define DEV_FRAME_UPTIME		38	// 4 bytes, uint32
#define DEV_FRAME_POC			42	// 4 bytes, uint32
#define DEV_FRAME_NAME			46	// 16 bytes, string
#define DEV_FRAME_PASS_READ		62	// 16 bytes, string
#define DEV_FRAME_MAN_DATE		78	// 8 bytes, string
#define DEV_FRAME_SN			86	// 12 bytes, string
#define DEV_FRAME_VENDOR		102	// 16 bytes, string
#define DEV_FRAME_PASS_SETUP	134	// 16 bytes, string

#define BMS_DEV_STR_READ(C, DEST, OFS, LEN) \
	do { \
		if (memcmp((DEST), (C)->nbuff + (OFS), (LEN))) \
			dev->cell_info.dev_force = true;\
		memcpy((DEST), (C)->nbuff + (OFS), (LEN));\
		(DEST)[(LEN)-1] = 0;\
	} while (0)
#define BMS_DEV_INT_READ(S, V)\
	{ if ((S) != (V)) { dev->cell_info.dev_force = true; (S) = (V); }}
static void jk_bt_process_device_frame(struct jk_bms_dev_t *dev)
{
	dev->dev_info.valid = true;
	BMS_DEV_STR_READ(dev, dev->dev_info.Model, DEV_FRAME_MODEL, 16);
	BMS_DEV_STR_READ(dev, dev->dev_info.Vendor, DEV_FRAME_VENDOR, 16);
	BMS_DEV_STR_READ(dev, dev->dev_info.Hardware, DEV_FRAME_HW, 8);
	BMS_DEV_STR_READ(dev, dev->dev_info.Software, DEV_FRAME_SW, 8);
	BMS_DEV_STR_READ(dev, dev->dev_info.ManufacturingDate, DEV_FRAME_MAN_DATE, 8);
	BMS_DEV_STR_READ(dev, dev->dev_info.SerialN, DEV_FRAME_SN, 12);
	BMS_DEV_STR_READ(dev, dev->dev_info.PassRead, DEV_FRAME_PASS_READ, 16);
	BMS_DEV_STR_READ(dev, dev->dev_info.PassSetup, DEV_FRAME_PASS_SETUP, 16);
	BMS_DEV_INT_READ(dev->dev_info.Uptime, DATA_UINT32_GET(dev, DEV_FRAME_UPTIME));
	BMS_DEV_INT_READ(dev->dev_info.PowerOnCount, DATA_UINT32_GET(dev, DEV_FRAME_POC));
}

static uint8_t calc_crc(const uint8_t data[], const uint16_t len)
{
	uint8_t crc = 0;
	uint16_t i;

	for (i = 0; i < len; i++)
		crc = crc + data[i];

	return crc;
}

static void jk_bt_process_terminal(struct jk_bms_dev_t *dev, bt_characteristicvalue_t *val)
{
	int ssize = ARRAY_SIZE(jk_notify_pkt_start);
	int csize = 0;
	uint8_t crc;

	if (dev->nbuff_ready) /* Previous frame not processed yet */
		return;

	if (dev->jk_term_charc.char_id != val->charId) {
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Not on terminal service, ignoring: %d / %d",
					  dev->jk_term_charc.char_id != val->charId);
		return;
	}
	if (val->len < ssize) {
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Data not enough, ignoring: %d, at least %d expected",
					  val->len, ssize);
		return;
	}

	if (!memcmp(val->data, jk_notify_pkt_start, ssize)) {
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "New notification detected");
		/* New notification starts */
		dev->nbuff_curr = 0;
		csize = val->len < NOTIFY_PACKET_SIZE ? val->len : NOTIFY_PACKET_SIZE;
	} else {
		/* Assemble previous notification */
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Assemble previous notification: +%d bytes", val->len);
		csize = NOTIFY_PACKET_SIZE - dev->nbuff_curr;
		csize = val->len < csize ? val->len : csize;
	}

	memcpy(dev->nbuff + dev->nbuff_curr, val->data, csize);
	dev->nbuff_curr += csize;
	if (dev->nbuff_curr == NOTIFY_PACKET_SIZE) {
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Processing frame %d of type %d: %d bytes", dev->nbuff[5], dev->nbuff[4], dev->nbuff_curr);
		if (memcmp(dev->nbuff, jk_notify_pkt_start, ssize)) {
			if (BMC_DEBUG(dev->ctx))
				hlog_info(BMS_JK_MODULE, "Invalid start magic [0x%X 0x%X 0x%X 0x%X]",
						  dev->nbuff[0], dev->nbuff[1], dev->nbuff[2], dev->nbuff[3]);
			dev->nbuff_curr = 0;
			return;
		}
		crc = calc_crc(dev->nbuff, dev->nbuff_curr - 1);
		if (crc != dev->nbuff[dev->nbuff_curr - 1]) {
			if (BMC_DEBUG(dev->ctx))
				hlog_info(BMS_JK_MODULE, "Broken CRC %d != %d", crc, dev->nbuff[dev->nbuff_curr - 1]);
			dev->nbuff_curr = 0;
			return;
		}
		dev->nbuff_ready = true;
	}
}

static void jk_bt_check_cell_levels(struct jk_bms_dev_t *dev)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];
	int i;

	if (!dev->cell_info.valid)
		return;

	if (dev->full_battery) {
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			if (!(1<<i & dev->cell_info.cells_enabled))
				break;
			if (dev->cell_info.cells_v[i] < dev->cell_v_low) {
				dev->full_battery = false;
				hlog_info(BMS_JK_MODULE, "Battery %s is empty: cell %d is %3.2fV",
						  dev->name, i, (float)(dev->cell_info.cells_v[i] * 0.001));
				if (dev->ctx->wh_notify && dev->batt_state_set) {
					snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
							dev->name, "empty");
					webhook_send(notify_buff);
				}
				if (dev->ssr_trigger)
					ssr_api_state_set(dev->ssr_id, !dev->ssr_norm_state, 0, 0);
				break;
			}
		}
	} else {
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			if (!(1<<i & dev->cell_info.cells_enabled))
				break;
			if (dev->cell_info.cells_v[i] < dev->cell_v_high)
				break;
		}
		if (!(1<<i & dev->cell_info.cells_enabled)) {
			dev->full_battery = true;
			hlog_info(BMS_JK_MODULE, "Battery %s is full", dev->name);
			if (dev->ctx->wh_notify && dev->batt_state_set) {
				snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
						dev->name, "full");
				webhook_send(notify_buff);
			}
			if (dev->ssr_trigger)
				ssr_api_state_set(dev->ssr_id, dev->ssr_norm_state, 0, 0);
		}
	}
	dev->batt_state_set = true;
}

static void bms_jk_frame_process(struct jk_bms_dev_t *dev)
{
	if (!dev->nbuff_ready)
		return;
	switch (dev->nbuff[4]) {
	case 0x01: /* settings, not supported yet */
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Got frame with settings, not supported yet");
		break;
	case 0x02: /* cell info */
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Got cell info");
		jk_bt_process_cell_frame(dev);
		if (dev->track_batt_level)
			jk_bt_check_cell_levels(dev);
		break;
	case 0x03: /* device info */
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Got device info");
		jk_bt_process_device_frame(dev);
		break;
	default:
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Got unsupported message type %d", dev->nbuff[4]);
		break;
	}
	dev->last_reply = time_ms_since_boot();
	dev->nbuff_curr = 0;
	dev->nbuff_ready = false;
	dev->wait_reply = false;
}

static void jk_bt_event(int idx, bt_event_t event, const void *data, int data_len, void *context)
{
	struct jk_bms_dev_t *dev = (struct jk_bms_dev_t *)context;
	bt_service_t *svc;

	if (idx != dev->bt_index)
		return;
	switch (event) {
	case BT_CONNECTED:
		dev->state = BT_CONNECTED;
		free(dev->name);
		dev->name = strdup(data);
		charc_reset(dev);
		if (dev->state != BT_CONNECTED)
			hlog_info(BMS_JK_MODULE, "Connected to %s", dev->name);
		dev->state = BT_CONNECTED;
		dev->last_reply = time_ms_since_boot();
		dev->connect_count++;
		break;
	case BT_DISCONNECTED:
		if (dev->state != BT_DISCONNECTED)
			hlog_info(BMS_JK_MODULE, "Disconnected from %s", dev->name);
		charc_reset(dev);
		dev->state = BT_DISCONNECTED;
		free(dev->name);
		dev->name = NULL;
		break;
	case BT_READY:
		if (dev->state != BT_READY)
			hlog_info(BMS_JK_MODULE, "Device %s is ready", dev->name);
		dev->state = BT_READY;
		dev->last_reply = time_ms_since_boot();
		break;
	case BT_NEW_SERVICE:
		if (data_len != sizeof(bt_service_t))
			break;
		svc = (bt_service_t *)data;
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "New service discovered (0x%X): ["UUID_128_FMT"]", svc->uuid16, UUID_128_PARAM(svc->uuid128));
		break;
	case BT_NEW_CHARACTERISTIC:
		if (data_len != sizeof(bt_characteristic_t))
			break;
		charc_new(dev, (bt_characteristic_t *)data);
		break;
	case BT_VALUE_RECEIVED:
		if (BMC_DEBUG(dev->ctx))
			hlog_info(BMS_JK_MODULE, "Data received, terminal is %s / %d",
					  dev->state == BT_READY ? "ready" : "not ready", dev->state);
		if (data_len == sizeof(bt_characteristicvalue_t) && dev->state == BT_READY)
			jk_bt_process_terminal(dev, (bt_characteristicvalue_t *)data);
		break;
	}
}

static int bms_jk_read_cmd(struct jk_bms_dev_t *dev, uint8_t address, uint32_t value, uint8_t length)
{
	uint64_t now = time_ms_since_boot();
	uint8_t frame[20] = {0};
	unsigned int i;
	int ret;

	if (!dev->jk_term_charc.notify) {
		if (!bt_characteristic_notify(dev->jk_term_charc.char_id, true))
			dev->jk_term_charc.notify = true;
	}

	/* start sequence: 0xAA, 0x55, 0x90, 0xEB */
	for (i = 0; i < ARRAY_SIZE(jk_request_pkt_start); i++)
		frame[i] = jk_request_pkt_start[i];
	frame[4] = address;  // holding register
	frame[5] = length;   // size of the value in byte
	frame[6] = value >> 0;
	frame[7] = value >> 8;
	frame[8] = value >> 16;
	frame[9] = value >> 24;
	frame[19] = calc_crc(frame, sizeof(frame) - 1);

	ret = bt_characteristic_write(dev->jk_term_charc.char_id, frame, sizeof(frame));

	if (BMC_DEBUG(dev->ctx))
		hlog_info(BMS_JK_MODULE, "Requested 0x%X val 0x%X: %d", address, value, ret);
	if (ret == 0)
		dev->jk_term_charc.send_time = now;

	bt_characteristic_read(dev->jk_term_charc.char_id);
	return (ret == 0 ? 0 : -1);
}

#define BMS_MODEL_STR   "JK"
static bool get_bms_config(bms_context_t **ctx)
{
	char *bt_batt_switch = USER_PRAM_GET(BMS_BATT_SWITCH);
	char *bt_batt_cell = USER_PRAM_GET(BMS_CELL_LEVELS);
	char *bt_timeout = USER_PRAM_GET(BMS_TIMEOUT_SEC);
	char *bt_mod = USER_PRAM_GET(BMS_MODEL);
	char *bt_id = USER_PRAM_GET(BMS_BT);
	char *bt_wh_notify = USER_PRAM_GET(BMS_NOTIFY);
	char *dev, *addr, *ch;
	char *mod, *mod_rest;
	char *rest, *rest1;
	bt_addr_t address;
	bool ret = false;
	uint32_t i;

	(*ctx) = NULL;
	if (!bt_mod || strlen(bt_mod) < 1)
		goto out;
	if (!bt_id || strlen(bt_id) < 1)
		goto out;
	(*ctx) = calloc(1, sizeof(bms_context_t));
	if (!(*ctx))
		goto out;

	__bms_jk_context = *ctx;

	rest = bt_id;
	mod_rest = bt_mod;
	while ((dev = strtok_r(rest, ";", &rest))) {
		mod = strtok_r(mod_rest, ";", &mod_rest);
		if (!mod || strlen(mod) != strlen(BMS_MODEL_STR))
			continue;
		if (strcmp(mod, BMS_MODEL_STR))
			continue;
		rest1 = dev;
		addr = strtok_r(rest1, ",", &rest1);
		if (!addr || strlen(addr) < 1)
			continue;
		if (!rest1 || strlen(rest1) < 1)
			continue;
		i = 0;
		while ((ch = strtok_r(addr, ":", &addr))  && i < 6)
			address[i++] = (int)strtol(ch, NULL, 16);
		if (i != 6)
			continue;
		if ((*ctx)->count >= BMS_MAX_DEVICES)
			break;
		(*ctx)->devices[(*ctx)->count] = calloc(1, sizeof(struct jk_bms_dev_t));
		if (!(*ctx)->devices[(*ctx)->count])
			break;
		memcpy((*ctx)->devices[(*ctx)->count]->address, address, sizeof(bt_addr_t));
		(*ctx)->devices[(*ctx)->count]->pin = strdup(rest1);
		if (!(*ctx)->devices[(*ctx)->count]->pin)
			break;
		(*ctx)->count++;
	}

	if (bt_timeout && strlen(bt_timeout) >= 1) {
		rest = bt_timeout;
		i = 0;
		while ((dev = strtok_r(rest, ";", &rest))) {
			(*ctx)->devices[i]->timeout_msec = (int)strtol(dev, NULL, 0);
			if ((*ctx)->devices[i]->timeout_msec > 0)
				(*ctx)->devices[i]->timeout_msec *= 1000;
			i++;
			if (i >= (*ctx)->count)
				break;
		}
	}

	if (bt_batt_cell && strlen(bt_batt_cell) >= 1) {
		rest = bt_batt_cell;
		i = 0;
		while ((dev = strtok_r(rest, ";", &rest))) {
			rest1 = dev;
			mod = strtok_r(rest1, ",", &rest1);
			if (!mod || !rest1)
				continue;
			(*ctx)->devices[i]->cell_v_low = (uint16_t)(strtof(mod, NULL) * 1000);
			(*ctx)->devices[i]->cell_v_high = (uint16_t)(strtof(rest1, NULL) * 1000);
			if ((*ctx)->devices[i]->cell_v_low > 0 && (*ctx)->devices[i]->cell_v_high > 0)
				(*ctx)->devices[i]->track_batt_level = true;
			i++;
			if (i >= (*ctx)->count)
				break;
		}
	}

	if (bt_batt_switch && strlen(bt_batt_switch) >= 1) {
		rest = bt_batt_switch;
		i = 0;
		while ((dev = strtok_r(rest, ";", &rest))) {
			rest1 = dev;
			mod = strtok_r(rest1, "-", &rest1);
			if (!mod || !rest1)
				continue;
			(*ctx)->devices[i]->ssr_id = (uint16_t)(strtol(mod, NULL, 0));
			(*ctx)->devices[i]->ssr_norm_state = (bool)(strtol(rest1, NULL, 0));
			(*ctx)->devices[i]->ssr_trigger = true;
			i++;
			if (i >= (*ctx)->count)
				break;
		}
	}

	if (bt_wh_notify && strlen(bt_wh_notify) >= 1)
		(*ctx)->wh_notify = (bool)(strtol(bt_wh_notify, NULL, 0));

	if ((*ctx)->count > 0)
		ret = true;
out:
	free(bt_id);
	free(bt_mod);
	if (bt_batt_cell)
		free(bt_batt_cell);
	if (bt_batt_switch)
		free(bt_batt_switch);
	if (bt_wh_notify)
		free(bt_wh_notify);
	if (!ret) {
		if ((*ctx)) {
			for (i = 0; i < (*ctx)->count; i++) {
				if (!(*ctx)->devices[i])
					continue;
				if ((*ctx)->devices[i]->pin)
					free((*ctx)->devices[i]->pin);
				free((*ctx)->devices[i]);
			}
			free(*ctx);
			*ctx = NULL;
		}
	}

	return ret;
}

bool bms_jk_init(bms_context_t **ctx)
{
	uint32_t i;
	//bt_uuid128_t sid = CUSTOM1_SVC_UID;
	//bt_uuid128_t cid = CUSTOM1_CHAR_READ_UID;

	if (!get_bms_config(ctx))
		return false;
	mutex_init(&(*ctx)->lock);
	for (i = 0; i < (*ctx)->count; i++) {
		(*ctx)->devices[i]->state = BT_DISCONNECTED;

		memcpy((*ctx)->devices[i]->jk_term_charc.svc_uid128, _terminal_svc, sizeof(bt_uuid128_t));
		memcpy((*ctx)->devices[i]->jk_term_charc.charc_uid128, _terminal_charc_read, sizeof(bt_uuid128_t));
		(*ctx)->devices[i]->ctx = (*ctx);
		(*ctx)->devices[i]->jk_term_charc.desc = "Terminal";

		(*ctx)->devices[i]->bt_index = bt_add_known_device((*ctx)->devices[i]->address,
														   (*ctx)->devices[i]->pin,
														   jk_bt_event, (*ctx)->devices[i]);
		if ((*ctx)->devices[i]->bt_index < 1)
			goto out_err;
		bms_jk_mqtt_init((*ctx), i);
	}

	hlog_info(BMS_JK_MODULE, "Initialise successfully %d JK BMS module", (*ctx)->count);
	return true;

out_err:
	for (i = 0; i < (*ctx)->count; i++) {
		if (!(*ctx)->devices[i])
			continue;
		if ((*ctx)->devices[i]->pin)
			free((*ctx)->devices[i]->pin);
		free((*ctx)->devices[i]);
	}
	free((*ctx));
	(*ctx) = NULL;
	return false;
}

static void bms_jk_send_request(struct jk_bms_dev_t *dev)
{
	if (dev->request_count % 10)
		bms_jk_read_cmd(dev, JK_COMMAND_CELL_INFO, 0, 0);
	else
		bms_jk_read_cmd(dev, JK_COMMAND_DEVICE_INFO, 0, 0);
	dev->request_count++;
	dev->send_time = time_ms_since_boot();
	dev->wait_reply = true;
}

static void bms_jk_timeout_check(bms_context_t *ctx)
{
	char tbuf[TIME_STR_LEN];
	datetime_t date;
	uint64_t now;
	uint32_t i;

	now = time_ms_since_boot();
	for (i = 0; i < ctx->count; i++) {
		if (ctx->devices[i]->timeout_msec < 1)
			continue;
		if (ctx->devices[i]->state != BT_READY ||
			!TERM_IS_ACTIVE(ctx->devices[i]) ||
			!ctx->devices[i]->jk_term_charc.notify)
				continue;
		if ((now - ctx->devices[i]->last_reply) > ctx->devices[i]->timeout_msec)
			break;
	}
	if (i >= ctx->count)
		return;

	time_msec2datetime(&date, now - ctx->devices[i]->last_reply);
	time_date2str(tbuf, TIME_STR_LEN, &date);
	hlog_info(BMS_JK_MODULE, "Timeout on device %s: %s, going to reboot ...",
			  ctx->devices[i]->name, tbuf);

	system_force_reboot(0);
}

static void bms_jk_process(bms_context_t *ctx)
{
	uint32_t i;

	for (i = 0; i < ctx->count; i++) {
		bms_jk_frame_process(ctx->devices[i]);
		bms_jk_mqtt_send(ctx->devices[i]);
	}

}

static void bms_jk_run(void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;
	struct jk_bms_dev_t *dev;
	static uint32_t idx;
	uint64_t now;

	if (idx >= ctx->count)
		idx = 0;

	dev = ctx->devices[idx];
	if (dev->state != BT_READY || !TERM_IS_ACTIVE(dev)) {
		idx++;
		goto out;
	}

	now = time_ms_since_boot();
	if (dev->wait_reply) {
		if ((now - dev->send_time) > CMD_TIMEOUT_MS) {
			dev->nbuff_curr = 0;
			dev->nbuff_ready = false;
			dev->wait_reply = false;
			idx++;
		}
	} else if ((now - dev->send_time) >= CMD_POLL_MS) {
		bms_jk_send_request(dev);
	} else
		idx++;

out:
	bms_jk_process(ctx);
	bms_jk_timeout_check(ctx);
}


static void bms_jk_debug_set(uint32_t debug, void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

static void bms_jk_log_cells(struct jk_bms_dev_t *dev)
{
	int i;

	hlog_info(BMS_JK_MODULE, "\tJK BMS cells:");
	hlog_info(BMS_JK_MODULE, "\t Enabled cells: 0x%X", dev->cell_info.cells_enabled);
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		hlog_info(BMS_JK_MODULE, "\t cell %d: %3.2fv, %3.2fohm", i,
				dev->cell_info.cells_v[i]*0.001, dev->cell_info.cells_res[i]*0.001);
	}
	hlog_info(BMS_JK_MODULE, "\t V average: %3.2fv", dev->cell_info.v_avg*0.001);
	hlog_info(BMS_JK_MODULE, "\t V delta: %3.2fv", dev->cell_info.v_avg*0.001);
	hlog_info(BMS_JK_MODULE, "\t Cell min %d, max %d", dev->cell_info.cell_v_min, dev->cell_info.cell_v_max);
	hlog_info(BMS_JK_MODULE, "\t Temperatures: power %3.2f; mos %3.2f, temp1 %3.2f, temp2 %3.2f",
			  dev->cell_info.power_temp*0.1, dev->cell_info.batt_temp_mos*0.1,
			  dev->cell_info.batt_temp1*0.1, dev->cell_info.batt_temp1*0.1);
	hlog_info(BMS_JK_MODULE, "\t Batt volt: %3.2fv", dev->cell_info.batt_volt*0.001);
	hlog_info(BMS_JK_MODULE, "\t Batt power: %ld", dev->cell_info.batt_power);
	hlog_info(BMS_JK_MODULE, "\t Cell warnings: 0x%X", dev->cell_info.cell_warn);
	hlog_info(BMS_JK_MODULE, "\t Cell alarms: 0x%X", dev->cell_info.alarms);
	hlog_info(BMS_JK_MODULE, "\t Balance current: %3.2fA", dev->cell_info.batt_balance_curr*0.001);
	hlog_info(BMS_JK_MODULE, "\t Batt action: %s", dev->cell_info.batt_action == 0x0?"Off" :
												(dev->cell_info.batt_action == 0x1?"Charging" :
												(dev->cell_info.batt_action == 0x2?"Discharging" : "Uknown")));
	hlog_info(BMS_JK_MODULE, "\t Batt state: %d%%", dev->cell_info.batt_state);
	hlog_info(BMS_JK_MODULE, "\t Batt cycles: %d", dev->cell_info.batt_cycles);
	hlog_info(BMS_JK_MODULE, "\t Batt cycles capacity: %3.2f Ah", dev->cell_info.batt_cycles_cap*0.001);
	hlog_info(BMS_JK_MODULE, "\t Batt capacity remain: %3.2f Ah", dev->cell_info.batt_cap_rem*0.001);
	hlog_info(BMS_JK_MODULE, "\t Batt capacity nominal: %3.2f Ah", dev->cell_info.batt_cap_nom*0.001);
	hlog_info(BMS_JK_MODULE, "\t SoH: %d", dev->cell_info.soh);
	hlog_info(BMS_JK_MODULE, "\t Runtime: %lds", dev->cell_info.run_time);
	hlog_info(BMS_JK_MODULE, "\t Charge %s", dev->cell_info.charge_enable?"enabled":"disabled");
	hlog_info(BMS_JK_MODULE, "\t Discharge %s", dev->cell_info.discharge_enable?"enabled":"disabled");
	hlog_info(BMS_JK_MODULE, "\t Precharge %s", dev->cell_info.precharge_enable?"enabled":"disabled");
	hlog_info(BMS_JK_MODULE, "\t Ballance %s", dev->cell_info.ballance_work?"enabled":"disabled");
	hlog_info(BMS_JK_MODULE, "\t Batt V: %3.2fV", dev->cell_info.batt_v*0.001);
	hlog_info(BMS_JK_MODULE, "\t Batt heat current: %3.2fA", dev->cell_info.batt_heat_a*0.001);
}

static void bms_jk_log_device(struct jk_bms_dev_t *dev)
{
	hlog_info(BMS_JK_MODULE, "\tJK BMS module:");
	hlog_info(BMS_JK_MODULE, "\t Vendor: %s", dev->dev_info.Vendor);
	hlog_info(BMS_JK_MODULE, "\t Model: %s", dev->dev_info.Model);
	hlog_info(BMS_JK_MODULE, "\t Hardware: %s", dev->dev_info.Hardware);
	hlog_info(BMS_JK_MODULE, "\t Software: %s", dev->dev_info.Software);
	hlog_info(BMS_JK_MODULE, "\t SerialN: %s", dev->dev_info.SerialN);
	hlog_info(BMS_JK_MODULE, "\t Uptime: %ld", dev->dev_info.Uptime);
	hlog_info(BMS_JK_MODULE, "\t PowerOnCount: %d", dev->dev_info.PowerOnCount);
}

static bool bms_jk_log(void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;
	struct jk_bms_dev_t *dev;
	char tbuf[TIME_STR_LEN];
	static uint32_t idx;
	datetime_t date;

	if (idx >= ctx->count) {
		idx = 0;
		return true;
	}
	dev = ctx->devices[idx];
	time_msec2datetime(&date, time_ms_since_boot() - dev->last_reply);
	time_date2str(tbuf, TIME_STR_LEN, &date);

	hlog_info(BMS_JK_MODULE, "Device %d status:", idx);
	hlog_info(BMS_JK_MODULE, "\tBT stack is %s, Terminal is %s, notifications are %s",
			  dev->state == BT_READY ? "Ready" : "Not ready",
			  TERM_IS_ACTIVE(dev) ? "active" : "not active",
			  dev->jk_term_charc.notify ? "registered" : "not registered");
	hlog_info(BMS_JK_MODULE, "\tLast valid response [%s] ago, connection count %d",
			  tbuf, dev->connect_count);
	if (dev->timeout_msec)
		hlog_info(BMS_JK_MODULE, "\tInactivity timeout %lld sec", dev->timeout_msec / 1000);

	if (!dev->dev_info.valid)
		hlog_info(BMS_JK_MODULE, "\tNo valid device info received");
	else
		bms_jk_log_device(dev);

	if (!dev->cell_info.valid)
		hlog_info(BMS_JK_MODULE, "\tNo valid cells info received");
	else
		bms_jk_log_cells(dev);

	if (dev->track_batt_level) {
		hlog_info(BMS_JK_MODULE, "\tTrack battery state between %3.2fV and %3.2fV",
				  dev->cell_v_low * 0.001, dev->cell_v_high * 0.001);
		hlog_info(BMS_JK_MODULE, "\tBattery level is %s", dev->full_battery ? "normal" : "low");
		if (dev->ssr_trigger) {
			hlog_info(BMS_JK_MODULE, "\tSwitch SSR %d on normal battery to %s",
					  dev->ssr_id, dev->ssr_norm_state ? "ON" : "OFF");
		}
	}

	idx++;
	return false;
}

void bms_jk_register(void)
{
	bms_context_t *ctx = NULL;

	if (!bms_jk_init(&ctx))
		return;

	ctx->mod.name = BMS_JK_MODULE;
	ctx->mod.run = bms_jk_run;
	ctx->mod.log = bms_jk_log;
	ctx->mod.debug = bms_jk_debug_set;
	ctx->mod.commands.description = "JK BMS monitor";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}

/* API */
int bms_jk_is_battery_full(uint32_t bms_id)
{
	bms_context_t *ctx = bms_jk_context_get();

	if (!ctx || bms_id >= ctx->count || !ctx->devices[bms_id]->track_batt_level)
		return -1;
	if (ctx->devices[bms_id]->state != BT_READY || !TERM_IS_ACTIVE(ctx->devices[bms_id]))
		return -1;
	return ctx->devices[bms_id]->full_battery;
}
