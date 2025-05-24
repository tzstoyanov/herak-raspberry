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

#include "common_lib.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"
#include "bms_jk.h"

#define BMC_DEBUG(C)	(C->debug)

#define CMD_POLL_MS	5000 /* Send command each 5s */
static const uint8_t __in_flash() jk_notify_pkt_start[] = {0x55, 0xAA, 0xEB, 0x90};
static const uint8_t __in_flash() jk_request_pkt_start[] = {0xAA, 0x55, 0x90, 0xEB};

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
static const bt_uuid128_t __in_flash() _terminal_charc_write = {0x00, 0x00, 0xff, 0xe2, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
static const bt_uuid128_t __in_flash() _terminal_charc_read = {0x00, 0x00, 0xff, 0xe1, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};

enum {
	JK_COMMAND_CELL_INFO = 0x96,
	JK_COMMAND_DEVICE_INFO = 0x97,
};

static void charc_new(bms_context_t *ctx, bt_characteristic_t *charc)
{
	bt_uuid128_t svc128;
	uint16_t	svc16;

	if (bt_service_get_uuid(charc->char_id, &svc128, &svc16))
		return;
	if (memcmp(ctx->jk_term_charc.svc_uid128, svc128, BT_UUID128_LEN))
		return;
	if (memcmp(ctx->jk_term_charc.charc_uid128, charc->uuid128, BT_UUID128_LEN))
		return;

	ctx->jk_term_charc.char_id = charc->char_id;
	ctx->jk_term_charc.properties = charc->properties;
	ctx->jk_term_charc.valid = true;

	if (BMC_DEBUG(ctx))
		hlog_info(BMS_JK_MODULE, "Got new characteristic [%s] %d: properties 0x%X, svc 0x%X ("UUID_128_FMT"), "UUID_128_FMT"",
				  ctx->jk_term_charc.desc, charc->char_id, charc->properties, svc16, svc128, charc->uuid128);

}

static void charc_reset(bms_context_t *ctx)
{
	ctx->jk_term_charc.valid = false;
	ctx->jk_term_charc.send_time = 0;
	ctx->jk_term_charc.notify = false;
	ctx->nbuff_curr = 0;
	ctx->nbuff_ready = false;
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
	{ if ((S) != (V)) { ctx->cell_info.data_force = true; (S) = (V); }}
static void jk_bt_process_cell_frame(bms_context_t *ctx)
{
	uint16_t d;
	int i;

	ctx->cell_info.valid = true;
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		d = DATA_UINT16_GET(ctx, CELL_FRAME_VOLT + i * 2); // * 0.001f
		if (ctx->cell_info.cells_v[i] != d)
			ctx->cell_info.cell_v_force = true;
		ctx->cell_info.cells_v[i] = d;
	}
	BMS_DATA_READ(ctx->cell_info.cells_enabled, DATA_UINT32_GET(ctx, CELL_FRAME_ENABLES_CELLS));
	BMS_DATA_READ(ctx->cell_info.v_avg, DATA_UINT16_GET(ctx, CELL_FRAME_VOLT_AVG));			// * 0.001f
	BMS_DATA_READ(ctx->cell_info.v_delta, DATA_UINT16_GET(ctx, CELL_FRAME_VOLT_DELTA));		// * 0.001f
	BMS_DATA_READ(ctx->cell_info.cell_v_max, DATA_UINT8_GET(ctx, CELL_FRAME_CELL_MAX));
	BMS_DATA_READ(ctx->cell_info.cell_v_min, DATA_UINT8_GET(ctx, CELL_FRAME_CELL_MIN));
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		d = DATA_UINT16_GET(ctx, CELL_FRAME_RESISTANCE + i * 2); // * 0.001f
		if (ctx->cell_info.cells_res[i] != d)
			ctx->cell_info.cell_r_force = true;
		ctx->cell_info.cells_res[i] = d;
	}
	BMS_DATA_READ(ctx->cell_info.power_temp, DATA_UINT16_GET(ctx, CELL_FRAME_POWER_TEMP));	// * 0.1f
	BMS_DATA_READ(ctx->cell_info.cell_warn, DATA_UINT32_GET(ctx, CELL_FRAME_CELL_WARN));
	BMS_DATA_READ(ctx->cell_info.batt_volt, DATA_UINT32_GET(ctx, CELL_FRAME_BATT_VOLT));	// * 0.001f
	BMS_DATA_READ(ctx->cell_info.batt_power, DATA_UINT32_GET(ctx, CELL_FRAME_BATT_POWER));
	BMS_DATA_READ(ctx->cell_info.batt_charge_curr, DATA_INT32_GET(ctx, CELL_FRAME_BATT_CHARGE)); // * 0.001f
	BMS_DATA_READ(ctx->cell_info.batt_temp1, DATA_UINT16_GET(ctx, CELL_FRAME_TEMP_1));		// * 0.1f
	BMS_DATA_READ(ctx->cell_info.batt_temp2, DATA_UINT16_GET(ctx, CELL_FRAME_TEMP_2));		// * 0.1f
	BMS_DATA_READ(ctx->cell_info.batt_temp_mos, DATA_UINT16_GET(ctx, CELL_FRAME_TEMP_MOS));	// * 0.1f
	BMS_DATA_READ(ctx->cell_info.alarms, DATA_UINT16_GET(ctx, CELL_FRAME_ALARM));
	BMS_DATA_READ(ctx->cell_info.batt_balance_curr, DATA_UINT16_GET(ctx, CELL_FRAME_BATT_BALANCE));	// * 0.001f
	BMS_DATA_READ(ctx->cell_info.batt_action, DATA_UINT8_GET(ctx, CELL_FRAME_BATT_ACTION));
	BMS_DATA_READ(ctx->cell_info.batt_state, DATA_UINT8_GET(ctx, CELL_FRAME_BATT_STATE));
	BMS_DATA_READ(ctx->cell_info.batt_cap_rem, DATA_UINT32_GET(ctx, CELL_FRAME_BATT_CAP_REMAIN));	// * 0.001f
	BMS_DATA_READ(ctx->cell_info.batt_cap_nom, DATA_UINT32_GET(ctx, CELL_FRAME_BATT_CAP_NOMINAL));	// * 0.001f
	BMS_DATA_READ(ctx->cell_info.batt_cycles, DATA_UINT32_GET(ctx, CELL_FRAME_CYCLE_COUNT));
	BMS_DATA_READ(ctx->cell_info.batt_cycles_cap, DATA_UINT32_GET(ctx, CELL_FRAME_CYCLE_CAP));	// * 0.001f
	BMS_DATA_READ(ctx->cell_info.soh, DATA_UINT8_GET(ctx, CELL_FRAME_SOH));
	BMS_DATA_READ(ctx->cell_info.run_time, DATA_UINT32_GET(ctx, CELL_FRAME_RUNTIME));
	BMS_DATA_READ(ctx->cell_info.charge_enable, DATA_UINT8_GET(ctx, CELL_FRAME_CHARGE_ENABLE));
	BMS_DATA_READ(ctx->cell_info.discharge_enable, DATA_UINT8_GET(ctx, CELL_FRAME_DISCHARGE_ENABLE));
	BMS_DATA_READ(ctx->cell_info.precharge_enable, DATA_UINT8_GET(ctx, CELL_FRAME_PRECHARGE_ENABLE));
	BMS_DATA_READ(ctx->cell_info.ballance_work, DATA_UINT8_GET(ctx, CELL_FRAME_BALANCER_WORK));
	BMS_DATA_READ(ctx->cell_info.batt_v, DATA_UINT16_GET(ctx, CELL_FRAME_BATTV)); // ?
	BMS_DATA_READ(ctx->cell_info.batt_heat_a, DATA_UINT16_GET(ctx, CELL_FRAME_BATT_HEAT_CURR)); // * 0.001f
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
			ctx->cell_info.dev_force = true;\
		memcpy((DEST), (C)->nbuff + (OFS), (LEN));\
		(DEST)[(LEN)-1] = 0;\
	} while (0)
#define BMS_DEV_INT_READ(S, V)\
	{ if ((S) != (V)) { ctx->cell_info.dev_force = true; (S) = (V); }}
static void jk_bt_process_device_frame(bms_context_t *ctx)
{
	ctx->dev_info.valid = true;
	BMS_DEV_STR_READ(ctx, ctx->dev_info.Model, DEV_FRAME_MODEL, 16);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.Vendor, DEV_FRAME_VENDOR, 16);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.Hardware, DEV_FRAME_HW, 8);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.Software, DEV_FRAME_SW, 8);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.ManufacturingDate, DEV_FRAME_MAN_DATE, 8);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.SerialN, DEV_FRAME_SN, 12);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.PassRead, DEV_FRAME_PASS_READ, 16);
	BMS_DEV_STR_READ(ctx, ctx->dev_info.PassSetup, DEV_FRAME_PASS_SETUP, 16);
	BMS_DEV_INT_READ(ctx->dev_info.Uptime, DATA_UINT32_GET(ctx, DEV_FRAME_UPTIME));
	BMS_DEV_INT_READ(ctx->dev_info.PowerOnCount, DATA_UINT32_GET(ctx, DEV_FRAME_POC));
}

static uint8_t calc_crc(const uint8_t data[], const uint16_t len)
{
	uint8_t crc = 0;
	uint16_t i;

	for (i = 0; i < len; i++)
		crc = crc + data[i];

	return crc;
}

static void jk_bt_process_terminal(bms_context_t *ctx, bt_characteristicvalue_t *val)
{
	int ssize = ARRAY_SIZE(jk_notify_pkt_start);
	int csize = 0;
	uint8_t crc;

	if (ctx->nbuff_ready) /* Previous frame not processed yet */
		return;

	if (ctx->jk_term_charc.char_id != val->charId) {
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Not on terminal service, ignoring: %d / %d",
					  ctx->jk_term_charc.char_id != val->charId);
		return;
	}
	if (val->len < ssize) {
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Data not enough, ignoring: %d, at least %d expected",
					  val->len, ssize);
		return;
	}

	if (!memcmp(val->data, jk_notify_pkt_start, ssize)) {
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "New notification detected");
		/* New notification starts */
		ctx->nbuff_curr = 0;
		csize = val->len < NOTIFY_PACKET_SIZE ? val->len : NOTIFY_PACKET_SIZE;
	} else {
		/* Assemble previous notification */
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Assemble previous notification: +%d bytes", val->len);
		csize = NOTIFY_PACKET_SIZE - ctx->nbuff_curr;
		csize = val->len < csize ? val->len : csize;
	}

	memcpy(ctx->nbuff + ctx->nbuff_curr, val->data, csize);
	ctx->nbuff_curr += csize;
	if (ctx->nbuff_curr == NOTIFY_PACKET_SIZE) {
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Processing frame %d of type %d: %d bytes", ctx->nbuff[5], ctx->nbuff[4], ctx->nbuff_curr);
		if (memcmp(ctx->nbuff, jk_notify_pkt_start, ssize)) {
			if (BMC_DEBUG(ctx))
				hlog_info(BMS_JK_MODULE, "Invalid start magic [0x%X 0x%X 0x%X 0x%X]",
						  ctx->nbuff[0], ctx->nbuff[1], ctx->nbuff[2], ctx->nbuff[3]);
			ctx->nbuff_curr = 0;
			return;
		}
		crc = calc_crc(ctx->nbuff, ctx->nbuff_curr - 1);
		if (crc != ctx->nbuff[ctx->nbuff_curr - 1]) {
			if (BMC_DEBUG(ctx))
				hlog_info(BMS_JK_MODULE, "Broken CRC %d != %d", crc, ctx->nbuff[ctx->nbuff_curr - 1]);
			ctx->nbuff_curr = 0;
			return;
		}
		ctx->nbuff_ready = true;
	}
}

static void bms_jk_frame_process(bms_context_t *ctx)
{
	if (!ctx->nbuff_ready)
		return;
	switch (ctx->nbuff[4]) {
	case 0x01: /* settings, not supported yet */
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Got frame with settings, not supported yet");
		break;
	case 0x02: /* cell info */
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Got cell info");
		jk_bt_process_cell_frame(ctx);
		break;
	case 0x03: /* device info */
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Got device info");
		jk_bt_process_device_frame(ctx);
		break;
	default:
		if (BMC_DEBUG(ctx))
			hlog_info(BMS_JK_MODULE, "Got unsupported message type %d", ctx->nbuff[4]);
		break;
	}
	ctx->last_reply = time_ms_since_boot();
	ctx->nbuff_curr = 0;
	ctx->nbuff_ready = false;
}

static void jk_bt_event(int idx, bt_event_t event, const void *data, int data_len, void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;
	bt_service_t *svc;

		if (idx != ctx->bt_index)
			return;
		switch (event) {
		case BT_CONNECTED:
			ctx->state = BT_CONNECTED;
			free(ctx->name);
			ctx->name = strdup(data);
			charc_reset(ctx);
			if (ctx->state != BT_CONNECTED)
				hlog_info(BMS_JK_MODULE, "Connected to %s", ctx->name);
			ctx->state = BT_CONNECTED;
			break;
		case BT_DISCONNECTED:
			if (ctx->state != BT_DISCONNECTED)
				hlog_info(BMS_JK_MODULE, "Disconnected from %s", ctx->name);
			charc_reset(ctx);
			ctx->state = BT_DISCONNECTED;
			free(ctx->name);
			ctx->name = NULL;
			break;
		case BT_READY:
			if (ctx->state != BT_READY)
				hlog_info(BMS_JK_MODULE, "Device %s is ready", ctx->name);
			ctx->state = BT_READY;
			break;
		case BT_NEW_SERVICE:
			if (data_len != sizeof(bt_service_t))
				break;
			svc = (bt_service_t *)data;
			if (BMC_DEBUG(ctx))
				hlog_info(BMS_JK_MODULE, "New service discovered (0x%X): ["UUID_128_FMT"]", svc->uuid16, UUID_128_PARAM(svc->uuid128));
			break;
		case BT_NEW_CHARACTERISTIC:
			if (data_len != sizeof(bt_characteristic_t))
				break;
			charc_new(ctx, (bt_characteristic_t *)data);
			break;
		case BT_VALUE_RECEIVED:
			if (BMC_DEBUG(ctx))
				hlog_info(BMS_JK_MODULE, "Data received, terminal is %s / %d",
						  ctx->state == BT_READY ? "ready" : "not ready", ctx->state);
			if (data_len == sizeof(bt_characteristicvalue_t) && ctx->state == BT_READY)
				jk_bt_process_terminal(ctx, (bt_characteristicvalue_t *)data);
			break;
		}
}

static int bms_jk_read_cmd(bms_context_t *ctx, uint8_t address, uint32_t value, uint8_t length)
{
	uint64_t now = time_ms_since_boot();
	uint8_t frame[20] = {0};
	unsigned int i;
	int ret;

	if (!ctx->jk_term_charc.notify) {
		if (!bt_characteristic_notify(ctx->jk_term_charc.char_id, true))
			ctx->jk_term_charc.notify = true;
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

	ret = bt_characteristic_write(ctx->jk_term_charc.char_id, frame, sizeof(frame));

	if (BMC_DEBUG(ctx))
		hlog_info(BMS_JK_MODULE, "Requested 0x%X val 0x%X: %d", address, value, ret);
	if (ret == 0)
		ctx->jk_term_charc.send_time = now;

	bt_characteristic_read(ctx->jk_term_charc.char_id);
	return (ret == 0 ? 0 : -1);
}

#define BMS_MODEL_STR   "JK"
static bool get_bms_config(bms_context_t **ctx)
{
	char *bt_mod = param_get(BMS_MODEL);
	char *bt_id = param_get(BMS_BT);
	char *rest, *rest1;
	bt_addr_t address;
	bool ret = false;
	char *pin = NULL;
	char *tok;
	int  i;

	if (!bt_mod || strlen(bt_mod) != strlen(BMS_MODEL_STR))
		goto out;
	if (strcmp(bt_mod, BMS_MODEL_STR))
		goto out;
	if (!bt_id || strlen(bt_id) < 1)
		goto out;
	rest = bt_id;
	tok = strtok_r(rest, ";", &rest);
	if (!tok)
		goto out;
	pin = strdup(rest);
	if (!pin)
		goto out;

	rest1 = tok;
	i = 0;
	while ((tok = strtok_r(rest1, ":", &rest1))  && i < 6)
		address[i++] = (int)strtol(tok, NULL, 16);
	if (i != 6)
		goto out;

	(*ctx) = calloc(1, sizeof(bms_context_t));
	if (!(*ctx))
		goto out;
	(*ctx)->pin = pin;
	memcpy((*ctx)->address, address, sizeof(bt_addr_t));

	ret = true;
out:
	free(bt_id);
	free(bt_mod);
	if (!ret)
		free(pin);
	return ret;
}

bool bms_jk_init(bms_context_t **ctx)
{
	//bt_uuid128_t sid = CUSTOM1_SVC_UID;
	//bt_uuid128_t cid = CUSTOM1_CHAR_READ_UID;

	if (!get_bms_config(ctx))
		return false;
	mutex_init(&(*ctx)->lock);
	(*ctx)->state = BT_DISCONNECTED;

	memcpy((*ctx)->jk_term_charc.svc_uid128, _terminal_svc, sizeof(bt_uuid128_t));
	memcpy((*ctx)->jk_term_charc.charc_uid128, _terminal_charc_read, sizeof(bt_uuid128_t));
	(*ctx)->jk_term_charc.desc = "Terminal";

	(*ctx)->bt_index = bt_add_known_device((*ctx)->address, (*ctx)->pin, jk_bt_event, *ctx);
	if ((*ctx)->bt_index < 1)
		goto out_err;

	bms_jk_mqtt_init(*ctx);
	hlog_info(BMS_JK_MODULE, "Initialise successfully JK BMS module");
	return true;

out_err:
	free((*ctx)->pin);
	free((*ctx));
	return false;
}

static void bms_jk_run(void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;
	static int cmd;
	uint64_t now;

	if (ctx->state != BT_READY)
		return;
	now = time_ms_since_boot();

	if (TERM_IS_ACTIVE(ctx) && (now - ctx->send_time) >= CMD_POLL_MS) {
		if (cmd % 10)
			bms_jk_read_cmd(ctx, JK_COMMAND_CELL_INFO, 0, 0);
		else
			bms_jk_read_cmd(ctx, JK_COMMAND_DEVICE_INFO, 0, 0);
		cmd++;
		ctx->send_time = now;
	}
	bms_jk_frame_process(ctx);
	bms_jk_mqtt_send(ctx);
}


static void bms_jk_debug_set(uint32_t debug, void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

static bool bms_jk_log_cells(bms_context_t *ctx)
{
	static int in_progress;
	int i;

	switch (in_progress) {
	case 0:
		hlog_info(BMS_JK_MODULE, "JK BMS cells:");
		hlog_info(BMS_JK_MODULE, "\tEnabled cells: 0x%X", ctx->cell_info.cells_enabled);
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			hlog_info(BMS_JK_MODULE, "\tcell %d: %3.2fv, %3.2fohm", i,
					ctx->cell_info.cells_v[i]*0.001, ctx->cell_info.cells_res[i]*0.001);
		}
		in_progress++;
		break;
	case 1:
		hlog_info(BMS_JK_MODULE, "\tV average: %3.2fv", ctx->cell_info.v_avg*0.001);
		hlog_info(BMS_JK_MODULE, "\tV delta: %3.2fv", ctx->cell_info.v_avg*0.001);
		hlog_info(BMS_JK_MODULE, "\tCell min %d, max %d", ctx->cell_info.cell_v_min, ctx->cell_info.cell_v_max);
		hlog_info(BMS_JK_MODULE, "\tTemperatures: power %3.2f; mos %3.2f, temp1 %3.2f, temp2 %3.2f",
				ctx->cell_info.power_temp*0.1, ctx->cell_info.batt_temp_mos*0.1,
				ctx->cell_info.batt_temp1*0.1, ctx->cell_info.batt_temp1*0.1);
		hlog_info(BMS_JK_MODULE, "\tBatt volt: %3.2fv", ctx->cell_info.batt_volt*0.001);
		hlog_info(BMS_JK_MODULE, "\tBatt power: %ld", ctx->cell_info.batt_power);
		hlog_info(BMS_JK_MODULE, "\tCell warnings: 0x%X", ctx->cell_info.cell_warn);
		hlog_info(BMS_JK_MODULE, "\tCell alarms: 0x%X", ctx->cell_info.alarms);
		in_progress++;
		break;
	case 2:
		hlog_info(BMS_JK_MODULE, "\tBalance current: %3.2fA", ctx->cell_info.batt_balance_curr*0.001);
		hlog_info(BMS_JK_MODULE, "\tBatt action: %s", ctx->cell_info.batt_action == 0x0?"Off" :
													(ctx->cell_info.batt_action == 0x1?"Charging" :
													(ctx->cell_info.batt_action == 0x2?"Discharging" : "Uknown")));
		hlog_info(BMS_JK_MODULE, "\tBatt state: %d%%", ctx->cell_info.batt_state);
		hlog_info(BMS_JK_MODULE, "\tBatt cycles: %d", ctx->cell_info.batt_cycles);
		hlog_info(BMS_JK_MODULE, "\tBatt cycles capacity: %3.2f Ah", ctx->cell_info.batt_cycles_cap*0.001);
		hlog_info(BMS_JK_MODULE, "\tBatt capacity remain: %3.2f Ah", ctx->cell_info.batt_cap_rem*0.001);
		hlog_info(BMS_JK_MODULE, "\tBatt capacity nominal: %3.2f Ah", ctx->cell_info.batt_cap_nom*0.001);
		in_progress++;
		break;
	case 3:
		hlog_info(BMS_JK_MODULE, "\tSoH: %d", ctx->cell_info.soh);
		hlog_info(BMS_JK_MODULE, "\tRuntime: %lds", ctx->cell_info.run_time);
		hlog_info(BMS_JK_MODULE, "\tCharge %s", ctx->cell_info.charge_enable?"enabled":"disabled");
		hlog_info(BMS_JK_MODULE, "\tDischarge %s", ctx->cell_info.discharge_enable?"enabled":"disabled");
		hlog_info(BMS_JK_MODULE, "\tPrecharge %s", ctx->cell_info.precharge_enable?"enabled":"disabled");
		hlog_info(BMS_JK_MODULE, "\tBallance %s", ctx->cell_info.ballance_work?"enabled":"disabled");
		hlog_info(BMS_JK_MODULE, "\tBatt V: %3.2fV", ctx->cell_info.batt_v*0.001);
		hlog_info(BMS_JK_MODULE, "\tBatt heat current: %3.2fA", ctx->cell_info.batt_heat_a*0.001);
		in_progress = 0;
		break;
	default:
		in_progress = 0;
		break;
	}

	return in_progress;
}

static void bms_jk_log_device(bms_context_t *ctx)
{
	hlog_info(BMS_JK_MODULE, "JK BMS module:");
	hlog_info(BMS_JK_MODULE, "\tVendor: %s", ctx->dev_info.Vendor);
	hlog_info(BMS_JK_MODULE, "\tModel: %s", ctx->dev_info.Model);
	hlog_info(BMS_JK_MODULE, "\tHardware: %s", ctx->dev_info.Hardware);
	hlog_info(BMS_JK_MODULE, "\tSoftware: %s", ctx->dev_info.Software);
	hlog_info(BMS_JK_MODULE, "\tSerialN: %s", ctx->dev_info.SerialN);
	hlog_info(BMS_JK_MODULE, "\tUptime: %ld", ctx->dev_info.Uptime);
	hlog_info(BMS_JK_MODULE, "\tPowerOnCount: %d", ctx->dev_info.PowerOnCount);
}

#define TIME_STR_LEN	64
static bool bms_jk_log(void *context)
{
	bms_context_t *ctx = (bms_context_t *)context;
	static bool in_progress;
	char tbuf[TIME_STR_LEN];
	datetime_t date;

	time_msec2datetime(&date, time_ms_since_boot() - ctx->last_reply);
	time_date2str(tbuf, TIME_STR_LEN, &date);

	if (!in_progress) {
		in_progress = true;
		hlog_info(BMS_JK_MODULE, "BT stack is %s, Terminal is %s, notifications are %s, last valid response [%s] ago",
				  ctx->state == BT_READY ? "Ready" : "Not ready",
				  TERM_IS_ACTIVE(ctx) ? "active" : "not active",
				  ctx->jk_term_charc.notify ? "registered" : "not registered", tbuf);
		if (!ctx->dev_info.valid)
			hlog_info(BMS_JK_MODULE, "No valid device info received");
		else
			bms_jk_log_device(context);
	} else {
		if (!ctx->cell_info.valid) {
			hlog_info(BMS_JK_MODULE, "No valid cells info received");
			in_progress = false;
		} else {
			in_progress = bms_jk_log_cells(context);
		}
	}
	return !in_progress;
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
