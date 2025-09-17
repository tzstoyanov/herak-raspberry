// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "stdlib.h"
#include "pico/time.h"
#include "pico/mutex.h"
#include "base64.h"
#include "params.h"
#include "btstack_config.h"
#include "btstack.h"
#include "btstack_event.h"

#include "herak_sys.h"
#include "common_internal.h"


#define BTLOG	"bt"
#define BT_MODULE	"bt"
#define CONNECT_TIMEOUT_MS 10000

#define IS_DEBUG(C) (!(C) || ((C) && (C)->debug))

//#define BT_DEBUG

#define GET_INDEX_FROM_ID(_id_, _dev_, _svc_, _char_) {\
			(_dev_) = (_id_) >> 16; (_dev_) &= 0xFF; (_dev_) -= 1;\
			(_svc_) = (_id_) >> 8; (_svc_) &= 0xFF;	(_svc_) -= 1;\
			(_char_) = (_id_) & 0xFF; (_char_) -= 1; }

enum bt_dev_state_t {
	BT_DEV_DISCONNECTED = 0,
	BT_DEV_CONNECTED,
	BT_DEV_DISCOVERING_PRIMARY,
	BT_DEV_DISCOVERING_SECONDARY,
	BT_DEV_DISCOVERING_CHARACTERISTIC,
	BT_DEV_READY
};

//#define BT_LOCAL_LOCK(C)		if ((C)) mutex_enter_blocking(&((C)->lock));
//#define BT_LOCAL_UNLOCK(C)	if ((C)) mutex_exit(&((C)->lock));

#define BT_LOCAL_LOCK(C) 	{ (void)(C); }
#define BT_LOCAL_UNLOCK(C)	{ (void)(C); }

#define BT_DEV_MAX_NAME	32
#define BT_MAX_DEVICES	4

struct bt_char_t {
	uint32_t id;
	gatt_client_characteristic_t gat_char;
	gatt_client_notification_t gat_notify;
};

struct bt_svc_t {
	uint32_t id;
	bool primary;
	gatt_client_service_t gat_svc;
	struct bt_char_t chars[BT_MAX_SERVICES];
	int char_count;
};

struct bt_device_t {
	int id;
	hci_con_handle_t connection_handle;
	bd_addr_t btaddress;
	char *pin;
	char name[BT_DEV_MAX_NAME];
	enum bt_dev_state_t state;
	bool discovering;
	uint64_t state_time;
	struct bt_svc_t services[BT_MAX_SERVICES];
	int svc_count;
	int svc_current;
	bt_event_handler_t user_cb;
	struct bt_context_t *bt_ctx;
	void *user_context;
};

struct bt_context_t {
	sys_module_t mod;
	btstack_packet_callback_registration_t hci_event_cb_reg;
	struct bt_device_t *devcies[BT_MAX_DEVICES];
	int dev_count;
	bool force_init;
	struct bt_device_t *current_device;
	bool started;
	bool running;
	bool scanning;
	mutex_t lock;
	uint32_t debug;
};

static struct bt_context_t *__bt_context;

static struct bt_context_t *bt_get_context(struct bt_device_t *dev)
{
	if (dev)
		return dev->bt_ctx;
	return __bt_context;
}

static struct bt_device_t *bt_get_device_by_address(bd_addr_t btaddress)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	struct bt_device_t *dev = NULL;
	int i = 0;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		while (i < ctx->dev_count) {
			if (!memcmp(btaddress, ctx->devcies[i]->btaddress, BD_ADDR_LEN)) {
				dev = ctx->devcies[i];
				break;
			}
			i++;
		}
	BT_LOCAL_UNLOCK(ctx);
	return dev;
}

static struct bt_device_t *bt_get_device_by_handle(hci_con_handle_t handle)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	struct bt_device_t *dev = NULL;
	int i;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		for (i = 0; i < ctx->dev_count; i++) {
			if (ctx->devcies[i]->connection_handle == handle) {
				dev = ctx->devcies[i];
				break;
			}
		}
	BT_LOCAL_UNLOCK(ctx);

	return dev;
}

static struct bt_char_t *bt_get_char_by_handle(struct bt_device_t *dev, uint16_t val_handle)
{
	struct bt_context_t *ctx = bt_get_context(dev);
	struct bt_char_t *charc = NULL;
	int i, j;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		for (i = 0; i < dev->svc_count; i++) {
			for (j = 0; j < dev->services[i].char_count; j++) {
				if (val_handle == dev->services[i].chars[j].gat_char.value_handle) {
					charc = &(dev->services[i].chars[j]);
					break;
				}
			}
			if (charc)
				break;
		}
	BT_LOCAL_UNLOCK(ctx);

	return charc;
}

struct advertising_report_t {
	uint8_t   type;
	uint8_t   event_type;
	uint8_t   address_type;
	bd_addr_t address;
	uint8_t   rssi;
	uint8_t   length;
	const uint8_t *data;
};

static const char *const __in_flash() ad_types[] = {
	"",
	"Flags",
	"Incomplete List of 16-bit Service Class UUIDs",
	"Complete List of 16-bit Service Class UUIDs",
	"Incomplete List of 32-bit Service Class UUIDs",
	"Complete List of 32-bit Service Class UUIDs",
	"Incomplete List of 128-bit Service Class UUIDs",
	"Complete List of 128-bit Service Class UUIDs",
	"Shortened Local Name",
	"Complete Local Name",
	"Tx Power Level",
	"",
	"",
	"Class of Device",
	"Simple Pairing Hash C",
	"Simple Pairing Randomizer R",
	"Device ID",
	"Security Manager TK Value",
	"Slave Connection Interval Range",
	"",
	"List of 16-bit Service Solicitation UUIDs",
	"List of 128-bit Service Solicitation UUIDs",
	"Service Data",
	"Public Target Address",
	"Random Target Address",
	"Appearance",
	"Advertising Interval"
};

static const char * const __in_flash() flags[] = {
	"LE Limited Discoverable Mode",
	"LE General Discoverable Mode",
	"BR/EDR Not Supported",
	"Simultaneous LE and BR/EDR to Same Device Capable (Controller)",
	"Simultaneous LE and BR/EDR to Same Device Capable (Host)",
	"Reserved",
	"Reserved",
	"Reserved"
};

static void get_advertisement_data(struct bt_device_t *dev, const uint8_t *adv_data, uint8_t adv_size)
{
	struct bt_context_t *ctx = bt_get_context(dev);
	uint8_t uuid_128[16];
	ad_context_t context;
	bd_addr_t address;
	int sz;

	if (!ctx)
		return;

	for (ad_iterator_init(&context, adv_size, (uint8_t *)adv_data); ad_iterator_has_more(&context); ad_iterator_next(&context)) {
		uint8_t data_type	= ad_iterator_get_data_type(&context);
		uint8_t size		= ad_iterator_get_data_len(&context);
		const uint8_t *data	= ad_iterator_get_data(&context);
		int i;

		if (data_type > 0 && data_type < 0x1B) {
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "	(%d)%s: ", data_type, ad_types[data_type]);
		}

		// Assigned Numbers GAP
		switch (data_type) {
		case BLUETOOTH_DATA_TYPE_FLAGS:
			// show only first octet, ignore rest
			if (IS_DEBUG(ctx)) {
				for (i = 0; i < 8; i++) {
					if (data[0] & (1<<i))
						hlog_info(BTLOG, "%s; ", flags[i]);
				}
			}
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_16_BIT_SERVICE_SOLICITATION_UUIDS:
			if (IS_DEBUG(ctx))
				for (i = 0; i < size; i += 2)
					hlog_info(BTLOG, "%02X ", little_endian_read_16(data, i));
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_32_BIT_SERVICE_SOLICITATION_UUIDS:
			if (IS_DEBUG(ctx))
				for (i = 0; i < size; i += 4)
					hlog_info(BTLOG, "%04X", little_endian_read_32(data, i));
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_128_BIT_SERVICE_SOLICITATION_UUIDS:
			reverse_128(data, uuid_128);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, UUID_128_FMT, UUID_128_PARAM(uuid_128));
			break;
		case BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME:
			sz = BT_DEV_MAX_NAME-1;
			if (size < sz)
				sz = size;
			memcpy(dev->name, data, sz);
			dev->name[sz] = 0;
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "%s", dev->name);
			break;
		case BLUETOOTH_DATA_TYPE_TX_POWER_LEVEL:
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "%d dBm", *(int8_t *)data);
			break;
		case BLUETOOTH_DATA_TYPE_SLAVE_CONNECTION_INTERVAL_RANGE:
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "Connection Interval Min = %u ms, Max = %u ms", little_endian_read_16(data, 0) * 5/4, little_endian_read_16(data, 2) * 5/4);
			break;
		case BLUETOOTH_DATA_TYPE_SERVICE_DATA:
			if (IS_DEBUG(ctx))
				printf_hexdump(data, size);
			break;
		case BLUETOOTH_DATA_TYPE_PUBLIC_TARGET_ADDRESS:
		case BLUETOOTH_DATA_TYPE_RANDOM_TARGET_ADDRESS:
			reverse_bd_addr(data, address);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "%s", bd_addr_to_str(address));
			break;
		case BLUETOOTH_DATA_TYPE_APPEARANCE:
			// https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.gap.appearance.xml
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "%02X", little_endian_read_16(data, 0));
			break;
		case BLUETOOTH_DATA_TYPE_ADVERTISING_INTERVAL:
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "%u ms", little_endian_read_16(data, 0) * 5/8);
			break;
		case BLUETOOTH_DATA_TYPE_3D_INFORMATION_DATA:
			if (IS_DEBUG(ctx))
				printf_hexdump(data, size);
			break;
		case BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA: // Manufacturer Specific Data
			break;
		case BLUETOOTH_DATA_TYPE_CLASS_OF_DEVICE:
		case BLUETOOTH_DATA_TYPE_SIMPLE_PAIRING_HASH_C:
		case BLUETOOTH_DATA_TYPE_SIMPLE_PAIRING_RANDOMIZER_R:
		case BLUETOOTH_DATA_TYPE_DEVICE_ID:
		case BLUETOOTH_DATA_TYPE_SECURITY_MANAGER_OUT_OF_BAND_FLAGS:
		default:
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "Advertising Data Type 0x%2x not handled yet", data_type);
			break;
		}
	}
}

static void printUUID(uint8_t *uuid128, uint16_t uuid16)
{
	if (uuid16)
		hlog_info(BTLOG, "%04x", uuid16);
	else
		hlog_info(BTLOG, UUID_128_FMT, UUID_128_PARAM(uuid128));
}

static void dump_characteristic(gatt_client_characteristic_t *characteristic)
{
	hlog_info(BTLOG, "\t * characteristic: [0x%04x-0x%04x-0x%04x], properties 0x%02x, uuid ",
			  characteristic->start_handle, characteristic->value_handle, characteristic->end_handle, characteristic->properties);
	printUUID(characteristic->uuid128, characteristic->uuid16);
}

static void dump_service(gatt_client_service_t *service)
{
	hlog_info(BTLOG, "\t * service: [0x%04x-0x%04x], uuid ", service->start_group_handle, service->end_group_handle);
	printUUID(service->uuid128, service->uuid16);
}

static void parse_advertising_report(struct bt_device_t *dev, struct advertising_report_t *e)
{
	struct bt_context_t *ctx = bt_get_context(dev);

	if (!ctx)
		return;

	if (IS_DEBUG(ctx)) {
		hlog_info(BTLOG, "\t * adv. event: evt-type %u, addr-type %u, addr %s, rssi %u, length adv %u, data: ",
			e->event_type, e->address_type, bd_addr_to_str(e->address), e->rssi, e->length);
		printf_hexdump(e->data, e->length);
	}
	get_advertisement_data(dev, e->data, e->length);
}

static void fill_advertising_report_from_packet(struct advertising_report_t *report, uint8_t *packet)
{
	gap_event_advertising_report_get_address(packet, report->address);
	report->event_type = gap_event_advertising_report_get_advertising_event_type(packet);
	report->address_type = gap_event_advertising_report_get_address_type(packet);
	report->rssi = gap_event_advertising_report_get_rssi(packet);
	report->length = gap_event_advertising_report_get_data_length(packet);
	report->data = gap_event_advertising_report_get_data(packet);
}

static void handle_gatt_client_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	struct bt_char_t *charc = NULL;
	bt_characteristicvalue_t val;
	struct bt_context_t *ctx;
	struct bt_device_t *dev;
	bool notify = false;

	UNUSED(packet_type);
	UNUSED(channel);
	UNUSED(size);

	switch (hci_event_packet_get_type(packet)) {
	case GATT_EVENT_CHARACTERISTIC_VALUE_QUERY_RESULT:
		val.len = gatt_event_characteristic_value_query_result_get_value_length(packet);
		val.data = (uint8_t *)gatt_event_characteristic_value_query_result_get_value(packet);
		val.val_long = false;
		dev = bt_get_device_by_handle(gatt_event_characteristic_value_query_result_get_handle(packet));
		ctx = bt_get_context(dev);
		if (dev)
			charc = bt_get_char_by_handle(dev, gatt_event_characteristic_value_query_result_get_value_handle(packet));
		if (charc) {
			val.charId = charc->id;
			notify = true;
		}
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "\t [%s] got characteristic short value %d bytes: 0x%2X ... ", dev ? dev->name : "Unknown", val.len, val.data[0]);
		break;
	case GATT_EVENT_LONG_CHARACTERISTIC_VALUE_QUERY_RESULT:
		val.len = gatt_event_long_characteristic_value_query_result_get_value_length(packet);
		val.data = (uint8_t *)gatt_event_long_characteristic_value_query_result_get_value(packet);
		val.val_long = true;
		dev = bt_get_device_by_handle(gatt_event_characteristic_value_query_result_get_handle(packet));
		ctx = bt_get_context(dev);
		if (dev)
			charc = bt_get_char_by_handle(dev, gatt_event_characteristic_value_query_result_get_value_handle(packet));
		if (charc) {
			val.charId = charc->id;
			notify = true;
		}
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "\t [%s] got characteristic LONG value %d bytes: 0x%2X ... ", dev ? dev->name : "Unknown", val.len, val.data[0]);
		break;
	case GATT_EVENT_QUERY_COMPLETE:
		dev = bt_get_device_by_handle(gatt_event_query_complete_get_handle(packet));
		ctx = bt_get_context(dev);
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "\t [%s] got query complete", dev ? dev->name : "Unknown");
		break;
	default:
		hlog_info(BTLOG, "Uknown read callback: %X", hci_event_packet_get_type(packet));
		break;
	}
	if (notify) {
		if (dev && dev->user_cb)
			dev->user_cb(dev->id, BT_VALUE_RECEIVED, &val, sizeof(val), dev->user_context);
	}
}

static struct bt_char_t *get_characteristic_by_uuid128(struct bt_svc_t *btsvc, uint8_t *uuid128)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	struct bt_char_t *charc = NULL;
	int i;

	BT_LOCAL_LOCK(ctx);
		for (i = 0; i < btsvc->char_count; i++) {
			if (!memcmp(uuid128, btsvc->chars[i].gat_char.uuid128, BT_UUID128_LEN)) {
				charc = &(btsvc->chars[i]);
				break;
			}
		}
	BT_LOCAL_UNLOCK(ctx);

	return charc;
}

static void bt_new_characteristic(struct bt_device_t *dev, gatt_client_characteristic_t *gchar)
{
	struct bt_context_t *ctx = bt_get_context(dev);
	bt_characteristic_t api_char;
	struct bt_svc_t *btsvc;

	if (!dev || !dev->discovering ||
		dev->svc_current < 0 || dev->svc_current >= BT_MAX_SERVICES)
		return;
	BT_LOCAL_LOCK(ctx);
		btsvc = &dev->services[dev->svc_current];
	BT_LOCAL_UNLOCK(ctx);
	if (btsvc->char_count < 0 || btsvc->char_count >= BT_MAX_SERVICES)
		return;
	if (get_characteristic_by_uuid128(btsvc, gchar->uuid128))
		return;
	BT_LOCAL_LOCK(ctx);
		memcpy(&(btsvc->chars[btsvc->char_count].gat_char), gchar, sizeof(gatt_client_characteristic_t));
		btsvc->chars[btsvc->char_count].id = btsvc->id | (btsvc->char_count+1);
	BT_LOCAL_UNLOCK(ctx);
	if (IS_DEBUG(ctx))
		hlog_info(BTLOG, "Device [%s] svc %X got CHARACTERISTIC [%X] "UUID_128_FMT", properties 0x%X",
				  dev->name, btsvc->gat_svc.uuid16, gchar->uuid16,
				  UUID_128_PARAM((uint8_t *)gchar->uuid128), gchar->properties);
	if (dev->user_cb) {
		api_char.char_id = btsvc->chars[btsvc->char_count].id;
		api_char.properties = gchar->properties;
		api_char.uuid16 = btsvc->gat_svc.uuid16;
		memcpy(api_char.uuid128, gchar->uuid128, BT_UUID128_LEN);
		dev->user_cb(dev->id, BT_NEW_CHARACTERISTIC, &api_char,
					 sizeof(api_char), dev->user_context);
	}
	BT_LOCAL_LOCK(ctx);
		btsvc->char_count++;
		dev->state_time = time_ms_since_boot();
	BT_LOCAL_UNLOCK(ctx);
}

static void bt_new_service(struct bt_device_t *dev, gatt_client_service_t *svc)
{
	struct bt_context_t *ctx = bt_get_context(dev);
	bt_service_t api_svc;
	struct bt_svc_t *btsvc;

	if (!dev->discovering || dev->svc_count >= BT_MAX_SERVICES)
		return;
	BT_LOCAL_LOCK(ctx);
	btsvc = &dev->services[dev->svc_count];
	memset(btsvc, 0, sizeof(struct bt_svc_t));
	memcpy(&(btsvc->gat_svc), svc, sizeof(gatt_client_service_t));
	btsvc->id = dev->id | ((dev->svc_count+1) << 8);
	switch (dev->state) {
	case BT_DEV_DISCOVERING_PRIMARY:
		btsvc->primary = true;
		break;
	case BT_DEV_DISCOVERING_SECONDARY:
		btsvc->primary = false;
		break;
	default:
		goto out;
	}
	dev->services[dev->svc_count].char_count = 0;
	if (IS_DEBUG(ctx))
		hlog_info(BTLOG, "Device [%s] got %s SERVICE [%X]: "UUID_128_FMT, dev->name,
				  btsvc->primary?"primary":"secondary", btsvc->gat_svc.uuid16, UUID_128_PARAM((uint8_t *)btsvc->gat_svc.uuid128));
	if (dev->user_cb) {
		api_svc.svc_id = btsvc->id;
		api_svc.primary = btsvc->primary;
		api_svc.uuid16 = btsvc->gat_svc.uuid16;
		memcpy(api_svc.uuid128, btsvc->gat_svc.uuid128, BT_UUID128_LEN);
		BT_LOCAL_UNLOCK(ctx);
			dev->user_cb(dev->id, BT_NEW_SERVICE, &api_svc, sizeof(api_svc), dev->user_context);
		BT_LOCAL_LOCK(ctx);
	}
	dev->svc_count++;
	dev->state_time = time_ms_since_boot();
out:
	BT_LOCAL_UNLOCK(ctx);
}

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	struct bt_context_t *ctx_g = bt_get_context(NULL);
	gatt_client_characteristic_t characteristic;
	struct bt_char_t *charc = NULL;
	bt_characteristicvalue_t val;
	gatt_client_service_t svc;
	struct bt_context_t *ctx;
	struct bt_device_t *dev;

	UNUSED(packet_type);
	UNUSED(channel);
	UNUSED(size);

	BT_LOCAL_LOCK(ctx_g);
		switch (hci_event_packet_get_type(packet)) {
		case GATT_EVENT_SERVICE_QUERY_RESULT:
			dev = bt_get_device_by_handle(gatt_event_service_query_result_get_handle(packet));
			ctx = bt_get_context(dev);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "GATT_EVENT_SERVICE_QUERY_RESULT %s", dev?dev->name:"N/A");
			if (dev) {
				gatt_event_service_query_result_get_service(packet, &svc);
				bt_new_service(dev, &svc);
				if (IS_DEBUG(ctx))
					dump_service(&svc);
			}
			break;
		case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
			dev = bt_get_device_by_handle(gatt_event_characteristic_query_result_get_handle(packet));
			ctx = bt_get_context(dev);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "GATT_EVENT_CHARACTERISTIC_QUERY_RESULT %s", dev?dev->name:"N/A");
			if (dev) {
				gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
				bt_new_characteristic(dev, &characteristic);
				if (IS_DEBUG(ctx))
					dump_characteristic(&characteristic);
			}
			break;
		case GATT_EVENT_QUERY_COMPLETE:
			dev = bt_get_device_by_handle(gatt_event_query_complete_get_handle(packet));
			ctx = bt_get_context(dev);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "GATT_EVENT_QUERY_COMPLETE %s", dev?dev->name:"N/A");
			if (dev)
				dev->discovering = false;
			break;
		case GATT_EVENT_NOTIFICATION:
			val.len = gatt_event_notification_get_value_length(packet);
			val.data = (uint8_t *)gatt_event_notification_get_value(packet);
			val.val_long = false;
			dev = bt_get_device_by_handle(gatt_event_notification_get_handle(packet));
			ctx = bt_get_context(dev);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "GATT_EVENT_NOTIFICATION %s: len %d, hdl 0x%X, val0: 0x%X",
						  dev?dev->name:"N/A",
						  val.len, gatt_event_notification_get_value_handle(packet), val.data[0]);
			charc = NULL;
			if (dev)
				charc = bt_get_char_by_handle(dev, gatt_event_characteristic_value_query_result_get_value_handle(packet));
			if (charc) {
				val.charId = charc->id;
				if (dev->user_cb)
					dev->user_cb(dev->id, BT_VALUE_RECEIVED, &val, sizeof(val), dev->user_context);
			}
			break;
		case GATT_EVENT_MTU:
			hlog_info(BTLOG, "GATT_EVENT_MTU: %d", gatt_event_mtu_get_MTU(packet));
			break;
		default:
			hlog_info(BTLOG, "handle client event for: %X", hci_event_packet_get_type(packet));
			break;
	}
	BT_LOCAL_UNLOCK(ctx_g);
}

static void bt_wlist_all_devices(struct bt_context_t *ctx)
{
	struct bt_device_t *dev;
	int i = 0;
	int ret;

	while (i < ctx->dev_count) {
		dev = ctx->devcies[i];
		ret = gap_whitelist_add(BD_ADDR_TYPE_LE_PUBLIC, dev->btaddress);
		if (ret)
			hlog_info(BTLOG, "Error adding device %s to the whitelist: %d", dev->name, ret);
		else
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "Whitelisted device %0.2X:%0.2X:%0.2X:%0.2X:%0.2X:%0.2X [%s]",
						  dev->btaddress[2], dev->btaddress[3], dev->btaddress[4],
						  dev->btaddress[0], dev->btaddress[1],
						  dev->btaddress[5], dev->pin);

		i++;
	}
}

static void trigger_scanning(struct bt_context_t *ctx)
{
	bool scan = false;
	int i;

	for (i = 0; i < ctx->dev_count; i++) {
		if (ctx->devcies[i]->state == BT_DEV_DISCONNECTED) {
			scan = true;
			break;
		}
	}

	if (scan && !ctx->scanning) {
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "Scanning started ...");
		ctx->scanning = true;
		gap_start_scan();
	} else if (!scan && ctx->scanning) {
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "Scanning stopped");
		ctx->scanning = false;
		gap_stop_scan();
	}
}

static void bt_reset_device(struct bt_device_t *dev, enum bt_dev_state_t state)
{
	struct bt_context_t *ctx = bt_get_context(dev);

	if (state == BT_DEV_DISCONNECTED && dev->user_cb)
		dev->user_cb(dev->id, BT_DISCONNECTED, NULL, 0, dev->user_context);
	BT_LOCAL_LOCK(ctx);
		dev->svc_count = 0;
		dev->state = state;
		dev->discovering = false;
		dev->svc_current = -1;
		memset(dev->services, 0, BT_MAX_SERVICES * sizeof(struct bt_svc_t));
	BT_LOCAL_UNLOCK(ctx);
}

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	struct advertising_report_t report;
	hci_con_handle_t handle;
	struct bt_device_t *dev;
	bd_addr_t btaddr;

	UNUSED(channel);
	UNUSED(size);

	if (!ctx || packet_type != HCI_EVENT_PACKET)
		return;

	switch (hci_event_packet_get_type(packet)) {
	case BTSTACK_EVENT_STATE:
		// BTstack activated, get started
		if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
			break;
		BT_LOCAL_LOCK(ctx);
			ctx->running = true;
		BT_LOCAL_UNLOCK(ctx);
		bt_wlist_all_devices(ctx);
		gap_set_scan_params(1, 0x0030, 0x0030, 0);
		trigger_scanning(ctx);
		hlog_info(BTLOG, "BTstack activated");
		break;
	case GAP_EVENT_ADVERTISING_REPORT:
		fill_advertising_report_from_packet(&report, packet);
		dev = bt_get_device_by_address(report.address);
		if (dev && dev->state == BT_DEV_DISCONNECTED) {
			parse_advertising_report(dev, &report);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "Detected %s, connecting ... ", dev->name);
			gap_connect(report.address, (bd_addr_type_t)report.address_type);
		}
		break;
	case HCI_EVENT_LE_META:
		// wait for connection complete
		if (hci_event_le_meta_get_subevent_code(packet) !=  HCI_SUBEVENT_LE_CONNECTION_COMPLETE)
			break;
		hci_subevent_le_connection_complete_get_peer_address(packet, btaddr);
		dev = bt_get_device_by_address(btaddr);
		if (dev) {
			BT_LOCAL_LOCK(ctx);
				dev->state = BT_DEV_CONNECTED;
				dev->svc_count = 0;
				memset(dev->services, 0, BT_MAX_SERVICES * sizeof(struct bt_svc_t));
				dev->connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
				dev->state_time = time_ms_since_boot();
			BT_LOCAL_UNLOCK(ctx);
			if (dev->user_cb)
				dev->user_cb(dev->id, BT_CONNECTED, dev->name, strlen(dev->name)+1, dev->user_context);
		}
		trigger_scanning(ctx);
		break;
	case HCI_EVENT_DISCONNECTION_COMPLETE:
		handle = hci_event_disconnection_complete_get_connection_handle(packet);
		dev = bt_get_device_by_handle(handle);
		// reason 0x3B - Unacceptable Connection Parameters
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "GATT browser - DISCONNECTED %s: status 0x%2X, reason 0x%2X", dev?dev->name:"Uknown",
					  hci_event_disconnection_complete_get_status(packet), hci_event_disconnection_complete_get_reason(packet));
		if (dev)
			bt_reset_device(dev, BT_DEV_DISCONNECTED);
		trigger_scanning(ctx);
		break;
	case HCI_EVENT_PIN_CODE_REQUEST:
		hci_event_pin_code_request_get_bd_addr(packet, btaddr);
		dev = bt_get_device_by_address(btaddr);
		if (dev) {
			hlog_info(BTLOG, "GATT device %s requested PIN %s", dev->name, dev->pin);
			gap_pin_code_response(dev->btaddress, dev->pin);
		}
		break;
	case HCI_EVENT_COMMAND_STATUS:
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "Command status : %d", hci_event_command_complete_get_command_opcode(packet));
		break;
	case HCI_EVENT_META_GAP:
		break;
	case HCI_EVENT_TRANSPORT_PACKET_SENT:
	case HCI_EVENT_COMMAND_COMPLETE:
	case BTSTACK_EVENT_SCAN_MODE_CHANGED:
	case HCI_SUBEVENT_LE_SCAN_REQUEST_RECEIVED:
		break;
	default:
		if (IS_DEBUG(ctx))
			hlog_info(BTLOG, "Got unknown HCI event 0x%0.2X", hci_event_packet_get_type(packet));
		break;
	}
}

static int bt_discover_next_char(struct bt_device_t *dev)
{
	struct bt_context_t *ctx = bt_get_context(dev);

	BT_LOCAL_LOCK(ctx);
		dev->svc_current++;
		if (dev->svc_current >= dev->svc_count) {
			BT_LOCAL_UNLOCK(ctx);
			return 0;
		}
		dev->state = BT_DEV_DISCOVERING_CHARACTERISTIC;
		dev->discovering = true;
		dev->state_time = time_ms_since_boot();
	BT_LOCAL_UNLOCK(ctx);
	if (IS_DEBUG(ctx))
		hlog_info(BTLOG, "Device [%s], discovery characteristic for service "UUID_128_FMT, dev->name,
				  UUID_128_PARAM(dev->services[dev->svc_current].gat_svc.uuid128));
	if (!gatt_client_discover_characteristics_for_service(handle_gatt_client_event, dev->connection_handle,
														  &dev->services[dev->svc_current].gat_svc))
		return 1;

	return -1;
}

static int bt_device_state(struct bt_device_t *dev)
{
	struct bt_context_t *ctx = bt_get_context(dev);
	uint64_t now;
	int ret;

	now = time_ms_since_boot();
	switch (dev->state) {
	case BT_DEV_CONNECTED:
		BT_LOCAL_LOCK(ctx);
			dev->discovering = false;
			ret = gatt_client_discover_primary_services(handle_gatt_client_event, dev->connection_handle);
			if (IS_DEBUG(ctx))
				hlog_info(BTLOG, "Discover primary BT services of [%s] ...  %d", dev->name, ret);
			if (ret)
				goto out_err_unlock;
			dev->discovering = true;
			dev->state = BT_DEV_DISCOVERING_PRIMARY;
			dev->state_time = now;
		BT_LOCAL_UNLOCK(ctx);
		break;
	case BT_DEV_DISCOVERING_PRIMARY:
	case BT_DEV_DISCOVERING_SECONDARY:
	case BT_DEV_DISCOVERING_CHARACTERISTIC:
		BT_LOCAL_LOCK(ctx);
			if (dev->discovering) {
				if ((now - dev->state_time) > CONNECT_TIMEOUT_MS) {
					hlog_info(BTLOG, "Timeout discovering BT services of [%s] ... ", dev->name);
					goto out_err_unlock;
				}
			} else { /* discovery completed */
				if (dev->state == BT_DEV_DISCOVERING_PRIMARY) {
					if (IS_DEBUG(ctx))
						hlog_info(BTLOG, "Discover secondary BT services of [%s] ... ", dev->name);
					if (gatt_client_discover_secondary_services(handle_gatt_client_event, dev->connection_handle))
						goto out_err_unlock;
					dev->state = BT_DEV_DISCOVERING_SECONDARY;
					dev->discovering = true;
					dev->state_time = now;
				} else { /* BT_DEV_DISCOVERING_SECONDARY or BT_DEV_DISCOVERING_CHARACTERISTIC */
					if (dev->state == BT_DEV_DISCOVERING_SECONDARY)
						dev->svc_current = -1;
					ret = bt_discover_next_char(dev);
					if (ret < 0)
						goto out_err_unlock;
					if (!ret) {
						dev->state = BT_DEV_READY;
						dev->svc_current = -1;
						//bms_bt_char_notify_enable();
						dev->state_time = now;
						if (IS_DEBUG(ctx))
							hlog_info(BTLOG, "Discovery of [%s] completed, device is ready", dev->name);
						BT_LOCAL_UNLOCK(ctx);
							if (dev->user_cb)
								dev->user_cb(dev->id, BT_READY, NULL, 0, dev->user_context);
						BT_LOCAL_LOCK(ctx);
					}
				}
			}
		BT_LOCAL_UNLOCK(ctx);
		break;
	case BT_DEV_DISCONNECTED:
	default:
		break;
	}

	return 0;

out_err_unlock:
	BT_LOCAL_UNLOCK(ctx);
	return -1;
}

static void bt_statck_init(struct bt_context_t *ctx)
{
	l2cap_init();
	sdp_init();
	sm_init();
	sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
	gatt_client_init();

	ctx->hci_event_cb_reg.callback = &bt_packet_handler;
	hci_add_event_handler(&ctx->hci_event_cb_reg);
}

static struct bt_device_t *get_device_by_id(uint32_t char_id)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	int dev_index, svc_index, char_index;
	struct bt_device_t *dev = NULL;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		GET_INDEX_FROM_ID(char_id, dev_index, svc_index, char_index);
		if (!ctx->running)
			goto out;
		if (dev_index < 0 || dev_index >= ctx->dev_count)
			goto out;
		dev = ctx->devcies[dev_index];
out:
	BT_LOCAL_UNLOCK(ctx);
	return dev;
}

static struct bt_svc_t *get_service_by_id(uint32_t svc_id)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	int dev_index, svc_index, char_index;
	struct bt_svc_t *svc = NULL;
	struct bt_device_t *dev;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		GET_INDEX_FROM_ID(svc_id, dev_index, svc_index, char_index);
		if (!ctx->running)
			goto out;
		if (dev_index < 0 || dev_index >= ctx->dev_count)
			goto out;
		dev = ctx->devcies[dev_index];
		if (dev->state == BT_DEV_DISCONNECTED)
			goto out;
		if (svc_index < 0 || svc_index >= dev->svc_count)
			goto out;
		svc = &(dev->services[svc_index]);
out:
	BT_LOCAL_UNLOCK(ctx);
	return svc;
}

static struct bt_char_t *get_characteristic_by_id(uint32_t char_id)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	int dev_index, svc_index, char_index;
	struct bt_char_t *charc = NULL;
	struct bt_device_t *dev;
	struct bt_svc_t *svc;

	if (!ctx)
		return NULL;

	BT_LOCAL_LOCK(ctx);
		GET_INDEX_FROM_ID(char_id, dev_index, svc_index, char_index);
		if (!ctx->running)
			goto out;
		if (dev_index < 0 || dev_index >= ctx->dev_count)
			goto out;
		dev = ctx->devcies[dev_index];
		if (dev->state == BT_DEV_DISCONNECTED)
			goto out;
		if (svc_index < 0 || svc_index >= dev->svc_count)
			goto out;
		svc = &(dev->services[svc_index]);
		if (char_index < 0 || char_index >= dev->services[svc_index].char_count)
			goto out;
		charc = &(svc->chars[char_index]);
out:
	BT_LOCAL_UNLOCK(ctx);
	return charc;
}

static int notify_characteristic_enable(uint32_t char_id)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	dev = get_device_by_id(char_id);
	charc = get_characteristic_by_id(char_id);
	if (dev && charc) {
		ret = gatt_client_write_client_characteristic_configuration(handle_gatt_client_event, dev->connection_handle,
																	&(charc->gat_char), GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
		if (!ret)
			gatt_client_listen_for_characteristic_value_updates(&(charc->gat_notify), handle_gatt_client_event, dev->connection_handle, &(charc->gat_char));
	}

	return ret;
}

static int notify_characteristic_disable(uint32_t char_id)
{
	struct bt_char_t *charc;

	charc = get_characteristic_by_id(char_id);
	if (charc)
		gatt_client_stop_listening_for_characteristic_value_updates(&(charc->gat_notify));

	return 0;
}

/************************ API ************************/

int bt_add_known_device(bt_addr_t addr, char *pin, bt_event_handler_t cb, void *context)
{
	struct bt_context_t *ctx = bt_get_context(NULL);
	struct bt_device_t *dev;
	bd_addr_t null_addr;
	int ret = -1;
	int i;

	if (!ctx)
		return -1;

	BT_LOCAL_LOCK(ctx);

	memset(null_addr, 0, BD_ADDR_LEN);
	if (!memcmp(addr, null_addr, BD_ADDR_LEN)) {
		ctx->force_init = true;
		goto out;
	}

	for (i = 0; i < BT_MAX_DEVICES; i++)
		if (!ctx->devcies[i])
			break;
	if (i == BT_MAX_DEVICES)
		goto out;

	dev = (struct bt_device_t *)calloc(1, sizeof(struct bt_device_t));
	if (!dev)
		goto out;

	memcpy(dev->btaddress, addr, sizeof(bd_addr_t));
	dev->pin = strdup(pin);
	dev->user_cb = cb;
	dev->bt_ctx = ctx;
	dev->user_context = context;
	dev->id = (i+1)<<16;
	snprintf(dev->name, BT_DEV_MAX_NAME, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
			 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	ctx->devcies[i] = dev;
	ctx->dev_count++;
	ret = dev->id;

out:
	BT_LOCAL_UNLOCK(ctx);
	return ret;
}

static void sys_bt_run(void *context)
{
	struct bt_context_t *ctx = (struct bt_context_t *)context;
	struct bt_device_t *dev = NULL;
	int state;
	int i;

	if (!ctx->dev_count && !ctx->force_init)
		return;

	if (!ctx->started) {
		hlog_info(BTLOG, "Init BT stack");
		bt_statck_init(ctx);
		if (!hci_power_control(HCI_POWER_ON))
			ctx->started = true;
		return;
	}
	if (!ctx->running)
		return;
	state = bt_device_state(ctx->current_device);
	BT_LOCAL_LOCK(ctx);
	if (ctx->current_device) {
		if (state) {
			BT_LOCAL_UNLOCK(ctx);
				bt_reset_device(ctx->current_device, BT_DEV_CONNECTED);
			BT_LOCAL_LOCK(ctx);
			ctx->current_device->state_time = time_ms_since_boot();
			ctx->current_device = NULL;
		} else if (ctx->current_device->state == BT_DEV_READY)
			ctx->current_device = NULL;
	} else {
		for (i = 0; i < ctx->dev_count; i++) {
			if (ctx->devcies[i]->state == BT_DEV_READY || ctx->devcies[i]->state == BT_DEV_DISCONNECTED)
				continue;
			if (!dev || (dev->state_time > ctx->devcies[i]->state_time))
				dev = ctx->devcies[i];
		}
	}
	if (dev)
		ctx->current_device = dev;
	BT_LOCAL_UNLOCK(ctx);
}

static bool sys_bt_log(void *context)
{
	struct bt_context_t *ctx = (struct bt_context_t *)context;
	struct bt_device_t *dev = NULL;
	int i, j, k;

	if (!ctx->started)
		return true;
	BT_LOCAL_LOCK(ctx);
		hlog_info(BTLOG, "BT stack started, %s, %s.",
					ctx->running?"running":"not running yet",
					ctx->scanning?"scanning for devices":"not scanning for devices");
		for (i = 0; i < ctx->dev_count; i++) {
			dev = ctx->devcies[i];
			if (!dev)
				continue;
			if (dev->state != BT_DEV_DISCONNECTED) {
				hlog_info(BTLOG, "\t%s to [%s].",
						dev->state < BT_DEV_READY?"Connecting":"Connected",
						dev->name);
				if (dev->state == BT_DEV_CONNECTED) {
					hlog_info(BTLOG, "\t\tDiscovered [%d] services:", dev->svc_count);
					for (j = 0; j < dev->svc_count; j++) {
						dump_service(&(dev->services[j].gat_svc));
						hlog_info(BTLOG, "\t\t\t[%d] characteristic:", dev->services[j].char_count);
						for (k = 0; k < dev->services[j].char_count; k++)
							dump_characteristic(&(dev->services[j].chars[k].gat_char));
					}
				}
			} else {
				hlog_info(BTLOG, "\tLooking for [%s] ...", dev->name);
			}
		}
	BT_LOCAL_UNLOCK(ctx);
	return true;
}

static bool sys_bt_init(struct bt_context_t **ctx)
{
	(*ctx) = (struct bt_context_t *)calloc(1, sizeof(struct bt_context_t));
	if (!(*ctx))
		return false;
	mutex_init(&((*ctx)->lock));

#ifdef BT_DEBUG
	ctx->debug = 0xFF;
#endif
	__bt_context = (*ctx);
	return true;
}

int bt_characteristic_notify(uint32_t char_id, bool enable)
{
	int ret;

	if (enable)
		ret = notify_characteristic_enable(char_id);
	else
		ret = notify_characteristic_disable(char_id);

	return ret;
}

int bt_characteristic_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16)
{
	struct bt_char_t *charc;
	int ret = -1;

	charc = get_characteristic_by_id(id);
	if (charc) {
		if (u128)
			memcpy(*u128, charc->gat_char.uuid128, BT_UUID128_LEN);
		if (u16)
			*u16 = charc->gat_char.uuid16;
		ret = 0;
	}

	return ret;
}

int bt_service_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16)
{
	struct bt_svc_t *svc;
	int ret = -1;

	svc = get_service_by_id(id);
	if (svc) {
		if (u128)
			memcpy(*u128, svc->gat_svc.uuid128, BT_UUID128_LEN);
		if (u16)
			*u16 = svc->gat_svc.uuid16;
		ret = 0;
	}

	return ret;
}

int bt_characteristic_read(uint32_t char_id)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	dev = get_device_by_id(char_id);
	charc = get_characteristic_by_id(char_id);
	if (dev && charc)
		ret = gatt_client_read_value_of_characteristic(handle_gatt_client_cb,
													   dev->connection_handle, &(charc->gat_char));
	return ret;
}

int bt_characteristic_write(uint32_t char_id, uint8_t *data, uint16_t data_len)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	dev = get_device_by_id(char_id);
	charc = get_characteristic_by_id(char_id);
	if (dev && charc)
		ret = gatt_client_write_value_of_characteristic_without_response(dev->connection_handle,
																		 charc->gat_char.value_handle,
																		 data_len, data);
	return ret;
}

static void sys_bt_debug_set(uint32_t debug, void *context)
{
	struct bt_context_t *ctx = (struct bt_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

void sys_bt_register(void)
{
	struct bt_context_t  *ctx = NULL;

	if (!sys_bt_init(&ctx))
		return;
hlog_info(BTLOG, "BT registered and init");
	ctx->mod.name = BT_MODULE;
	ctx->mod.run = sys_bt_run;
	ctx->mod.log = sys_bt_log;
	ctx->mod.debug = sys_bt_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
