// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "stdlib.h"
#include "pico/time.h"
#include "pico/mutex.h"
#include "base64.h"
#include "params.h"
#include "btstack_config.h"
#include "btstack.h"
#include "btstack_event.h"

#define BTLOG	"bt"
#define CONNECT_TIMEOUT_MS 10000

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

//#define BT_LOCAL_LOCK	mutex_enter_blocking(&bt_context.lock);
//#define BT_LOCAL_UNLOCK	mutex_exit(&bt_context.lock);

#define BT_LOCAL_LOCK
#define BT_LOCAL_UNLOCK

#define BT_DEV_MAX_NAME	32

struct bt_char_t {
	uint32_t id;
	bool notify;
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
	uint32_t state_time;
	struct bt_svc_t services[BT_MAX_SERVICES];
	int svc_count;
	int svc_current;
	bt_event_handler_t user_cb;
	void *user_context;
};

static struct {
	btstack_packet_callback_registration_t hci_event_cb_reg;
	struct bt_device_t devcies[BT_MAX_DEVICES];
	int dev_count;
	bool force_init;
	struct bt_device_t *current_device;
	bool started;
	bool running;
	bool scanning;
	bool verbose;
	mutex_t lock;
	uint32_t debug;
} bt_context;

static struct bt_device_t *bt_get_device_by_address(bd_addr_t btaddress)
{
	int i = 0;

	while (i < bt_context.dev_count) {
		if (!memcmp(btaddress, bt_context.devcies[i].btaddress, BD_ADDR_LEN))
			return &bt_context.devcies[i];
		i++;
	}

	return NULL;
}

static struct bt_device_t *bt_get_device_by_handle(hci_con_handle_t handle)
{
	int i;

	for (i = 0; i < bt_context.dev_count; i++)
		if (bt_context.devcies[i].connection_handle == handle)
			return &(bt_context.devcies[i]);

	return NULL;
}

static struct bt_char_t *bt_get_char_by_handle(struct bt_device_t *dev, uint16_t val_handle)
{
	int i, j;

	for (i = 0; i < dev->svc_count; i++) {
		for (j = 0; j < dev->services[i].char_count; j++)
			if (val_handle == dev->services[i].chars[j].gat_char.value_handle)
				return &(dev->services[i].chars[j]);
	}

	return NULL;
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

static const char *const ad_types[] = {
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

static const char * const flags[] = {
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
	uint8_t uuid_128[16];
	ad_context_t context;
	bd_addr_t address;
	int sz;

	for (ad_iterator_init(&context, adv_size, (uint8_t *)adv_data) ; ad_iterator_has_more(&context) ; ad_iterator_next(&context)) {
		uint8_t data_type	= ad_iterator_get_data_type(&context);
		uint8_t size		= ad_iterator_get_data_len(&context);
		const uint8_t *data	= ad_iterator_get_data(&context);
		int i;

		if (data_type > 0 && data_type < 0x1B) {
			if (bt_context.verbose)
				hlog_info(BTLOG, "	(%d)%s: ", data_type, ad_types[data_type]);
		}

		// Assigned Numbers GAP
		switch (data_type) {
		case BLUETOOTH_DATA_TYPE_FLAGS:
			// show only first octet, ignore rest
			if (bt_context.verbose) {
				for (i = 0; i < 8; i++) {
					if (data[0] & (1<<i))
						hlog_info(BTLOG, "%s; ", flags[i]);
				}
			}
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_16_BIT_SERVICE_SOLICITATION_UUIDS:
			if (bt_context.verbose)
				for (i = 0; i < size; i += 2)
					hlog_info(BTLOG, "%02X ", little_endian_read_16(data, i));
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_32_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_32_BIT_SERVICE_SOLICITATION_UUIDS:
			if (bt_context.verbose)
				for (i = 0; i < size; i += 4)
					hlog_info(BTLOG, "%04X", little_endian_read_32(data, i));
			break;
		case BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS:
		case BLUETOOTH_DATA_TYPE_LIST_OF_128_BIT_SERVICE_SOLICITATION_UUIDS:
			reverse_128(data, uuid_128);
			if (bt_context.verbose)
				hlog_info(BTLOG, UUID_128_FMT, UUID_128_PARAM(uuid_128));
			break;
		case BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME:
		case BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME:
			sz = BT_DEV_MAX_NAME-1;
			if (size < sz)
				sz = size;
			memcpy(dev->name, data, sz);
			dev->name[sz] = 0;
			if (bt_context.verbose)
				hlog_info(BTLOG, "%s", dev->name);
			break;
		case BLUETOOTH_DATA_TYPE_TX_POWER_LEVEL:
			if (bt_context.verbose)
				hlog_info(BTLOG, "%d dBm", *(int8_t *)data);
			break;
		case BLUETOOTH_DATA_TYPE_SLAVE_CONNECTION_INTERVAL_RANGE:
			if (bt_context.verbose)
				hlog_info(BTLOG, "Connection Interval Min = %u ms, Max = %u ms", little_endian_read_16(data, 0) * 5/4, little_endian_read_16(data, 2) * 5/4);
			break;
		case BLUETOOTH_DATA_TYPE_SERVICE_DATA:
			if (bt_context.verbose)
				printf_hexdump(data, size);
			break;
		case BLUETOOTH_DATA_TYPE_PUBLIC_TARGET_ADDRESS:
		case BLUETOOTH_DATA_TYPE_RANDOM_TARGET_ADDRESS:
			reverse_bd_addr(data, address);
			if (bt_context.verbose)
				hlog_info(BTLOG, "%s", bd_addr_to_str(address));
			break;
		case BLUETOOTH_DATA_TYPE_APPEARANCE:
			// https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.gap.appearance.xml
			if (bt_context.verbose)
				hlog_info(BTLOG, "%02X", little_endian_read_16(data, 0));
			break;
		case BLUETOOTH_DATA_TYPE_ADVERTISING_INTERVAL:
			if (bt_context.verbose)
				hlog_info(BTLOG, "%u ms", little_endian_read_16(data, 0) * 5/8);
			break;
		case BLUETOOTH_DATA_TYPE_3D_INFORMATION_DATA:
			if (bt_context.verbose)
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
			if (bt_context.verbose)
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
	if (bt_context.verbose) {
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

static void handle_gatt_client_read_value(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	struct bt_char_t *charc = NULL;
	bt_characteristicvalue_t val;
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
		if (dev)
			charc = bt_get_char_by_handle(dev, gatt_event_characteristic_value_query_result_get_value_handle(packet));
		if (charc) {
			val.charId = charc->id;
			notify = true;
		}
		if (bt_context.verbose)
			hlog_info(BTLOG, "\t [%s] got characteristic value %d bytes: 0x%2X ... ", dev ? dev->name : "Unknown", val.len, val.data[0]);
		break;
	case GATT_EVENT_LONG_CHARACTERISTIC_VALUE_QUERY_RESULT:
		val.len = gatt_event_long_characteristic_value_query_result_get_value_length(packet);
		val.data = (uint8_t *)gatt_event_long_characteristic_value_query_result_get_value(packet);
		val.val_long = true;
		dev = bt_get_device_by_handle(gatt_event_characteristic_value_query_result_get_handle(packet));
		if (dev)
			charc = bt_get_char_by_handle(dev, gatt_event_characteristic_value_query_result_get_value_handle(packet));
		if (charc) {
			val.charId = charc->id;
			notify = true;
		}
		if (bt_context.verbose)
			hlog_info(BTLOG, "\t [%s] got characteristic LONG value %d bytes: 0x%2X ... ", dev ? dev->name : "Unknown", val.len, val.data[0]);
		break;
	case GATT_EVENT_QUERY_COMPLETE:
		break;
	default:
		break;
	}
	if (notify) {
		BT_LOCAL_LOCK;
			if (dev && dev->user_cb)
				dev->user_cb(dev->id, BT_VALUE_RECEIVED, &val, sizeof(val), dev->user_context);
		BT_LOCAL_UNLOCK;
	}
}

static struct bt_char_t *get_characteristic_by_uuid128(struct bt_svc_t *btsvc, uint8_t *uuid128)
{
	int i;

	for (i = 0; i < btsvc->char_count; i++) {
		if (!memcmp(uuid128, btsvc->chars[i].gat_char.uuid128, BT_UUID128_LEN))
			return &(btsvc->chars[i]);
	}
	return NULL;
}

static void bt_new_characteristic(gatt_client_characteristic_t *gchar)
{
	bt_characteristic_t api_char;
	struct bt_svc_t *btsvc;

	if (!bt_context.current_device || !bt_context.current_device->discovering ||
		bt_context.current_device->svc_current < 0 || bt_context.current_device->svc_current >= BT_MAX_SERVICES)
		return;

	btsvc = &bt_context.current_device->services[bt_context.current_device->svc_current];
	if (btsvc->char_count < 0 || btsvc->char_count >= BT_MAX_SERVICES)
		return;
	if (get_characteristic_by_uuid128(btsvc, gchar->uuid128))
		return;
	memcpy(&(btsvc->chars[btsvc->char_count].gat_char), gchar, sizeof(gatt_client_characteristic_t));
	btsvc->chars[btsvc->char_count].id = btsvc->id | (btsvc->char_count+1);
	if (bt_context.verbose)
		hlog_info(BTLOG, "Device [%s] svc %X got CHARACTERISTIC [%X] "UUID_128_FMT", properties 0x%X",
				  bt_context.current_device->name, btsvc->gat_svc.uuid16, gchar->uuid16,
				  UUID_128_PARAM((uint8_t *)gchar->uuid128), gchar->properties);
	if (bt_context.current_device->user_cb) {
		api_char.char_id = btsvc->chars[btsvc->char_count].id;
		api_char.properties = gchar->properties;
		api_char.uuid16 = btsvc->gat_svc.uuid16;
		memcpy(api_char.uuid128, gchar->uuid128, BT_UUID128_LEN);
		bt_context.current_device->user_cb(bt_context.current_device->id, BT_NEW_CHARACTERISTIC, &api_char,
										   sizeof(api_char), bt_context.current_device->user_context);
	}
	btsvc->char_count++;
	bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
}

static void bt_new_service(struct bt_device_t *dev, gatt_client_service_t *svc)
{
	bt_service_t api_svc;
	struct bt_svc_t *btsvc;

	if (!dev->discovering || dev->svc_count >= BT_MAX_SERVICES)
		return;

	btsvc = &dev->services[bt_context.current_device->svc_count];
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
		return;
	}
	dev->services[dev->svc_count].char_count = 0;
	if (bt_context.verbose)
		hlog_info(BTLOG, "Device [%s] got %s SERVICE [%X]: "UUID_128_FMT, dev->name,
				  btsvc->primary?"primary":"secondary", btsvc->gat_svc.uuid16, UUID_128_PARAM((uint8_t *)btsvc->gat_svc.uuid128));
	if (dev->user_cb) {
		api_svc.svc_id = btsvc->id;
		api_svc.primary = btsvc->primary;
		api_svc.uuid16 = btsvc->gat_svc.uuid16;
		memcpy(api_svc.uuid128, btsvc->gat_svc.uuid128, BT_UUID128_LEN);
		dev->user_cb(dev->id, BT_NEW_SERVICE, &api_svc, sizeof(api_svc), dev->user_context);
	}
	dev->svc_count++;
	dev->state_time = to_ms_since_boot(get_absolute_time());
}

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	gatt_client_characteristic_t characteristic;
	gatt_client_service_t svc;
	struct bt_device_t *dev;

	UNUSED(packet_type);
	UNUSED(channel);
	UNUSED(size);
	BT_LOCAL_LOCK;
		dev = bt_context.current_device;
		if (!dev)
			goto out;

		switch (hci_event_packet_get_type(packet)) {
		case GATT_EVENT_SERVICE_QUERY_RESULT:
			gatt_event_service_query_result_get_service(packet, &svc);
			bt_new_service(dev, &svc);
			if (bt_context.verbose)
				dump_service(&svc);
			break;
		case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
			gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
			bt_new_characteristic(&characteristic);
			if (bt_context.verbose)
				dump_characteristic(&characteristic);
			break;
		case GATT_EVENT_QUERY_COMPLETE:
			bt_context.current_device->discovering = false;
			break;
		default:
			if (bt_context.verbose)
				hlog_info(BTLOG, "handle client event for %s: %X", dev->name, hci_event_packet_get_type(packet));
			break;
	}
out:
	BT_LOCAL_UNLOCK;
}

static void bt_wlist_all_devices(void)
{
	int i = 0;

	while (i < bt_context.dev_count) {
		if (gap_whitelist_add(BD_ADDR_TYPE_LE_PUBLIC, bt_context.devcies[i].btaddress))
			hlog_info(BTLOG, "Error adding device %s to the whitelist", bt_context.devcies[i].name);
		else
			if (bt_context.verbose)
				hlog_info(BTLOG, "Whitelisted device %0.2X:%0.2X:%0.2X:%0.2X:%0.2X:%0.2X [%s]",
						  bt_context.devcies[i].btaddress[0], bt_context.devcies[i].btaddress[1],
						  bt_context.devcies[i].btaddress[2], bt_context.devcies[i].btaddress[3], bt_context.devcies[i].btaddress[4],
						  bt_context.devcies[i].btaddress[5], bt_context.devcies[i].pin);
		i++;
	}
}

static void trigger_scanning(void)
{
	bool scan = false;
	int i;

	for (i = 0; i < bt_context.dev_count; i++) {
		if (bt_context.devcies[i].state == BT_DEV_DISCONNECTED) {
			scan = true;
			break;
		}
	}

	if (scan && !bt_context.scanning) {
		if (bt_context.verbose)
			hlog_info(BTLOG, "Scanning started ...");
		bt_context.scanning = true;
		gap_start_scan();
	} else if (!scan && bt_context.scanning) {
		if (bt_context.verbose)
			hlog_info(BTLOG, "Scanning stopped");
		bt_context.scanning = false;
		gap_stop_scan();
	}
}

static void bt_reset_device(struct bt_device_t *dev, enum bt_dev_state_t state)
{
	if (state == BT_DEV_DISCONNECTED && dev->user_cb)
		dev->user_cb(dev->id, BT_DISCONNECTED, NULL, 0, dev->user_context);
	BT_LOCAL_LOCK;
		dev->svc_count = 0;
		dev->state = state;
		dev->discovering = false;
		dev->svc_current = -1;
		memset(dev->services, 0, BT_MAX_SERVICES * sizeof(struct bt_svc_t));
	BT_LOCAL_UNLOCK;
}

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
	struct advertising_report_t report;
	hci_con_handle_t handle;
	bd_addr_t btaddr;
	struct bt_device_t *dev;

	UNUSED(channel);
	UNUSED(size);
	if (packet_type != HCI_EVENT_PACKET)
		return;

	switch (hci_event_packet_get_type(packet)) {
	case BTSTACK_EVENT_STATE:
		// BTstack activated, get started
		if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
			break;
		BT_LOCAL_LOCK
			bt_context.running = true;
		BT_LOCAL_UNLOCK;
		bt_wlist_all_devices();
		gap_set_scan_params(1, 0x0030, 0x0030, 0);
		trigger_scanning();
		hlog_info(BTLOG, "BTstack activated");
		break;
	case GAP_EVENT_ADVERTISING_REPORT:
		fill_advertising_report_from_packet(&report, packet);
		dev = bt_get_device_by_address(report.address);
		if (dev && dev->state == BT_DEV_DISCONNECTED) {
			parse_advertising_report(dev, &report);
			if (bt_context.verbose)
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
			BT_LOCAL_LOCK;
				dev->state = BT_DEV_CONNECTED;
				dev->svc_count = 0;
				memset(dev->services, 0, BT_MAX_SERVICES * sizeof(struct bt_svc_t));
				dev->connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
				dev->state_time = to_ms_since_boot(get_absolute_time());
				if (dev->user_cb)
					dev->user_cb(dev->id, BT_CONNECTED, dev->name, strlen(dev->name)+1, dev->user_context);
			BT_LOCAL_UNLOCK;
		}
		trigger_scanning();
		break;
	case HCI_EVENT_DISCONNECTION_COMPLETE:
		handle = hci_event_disconnection_complete_get_connection_handle(packet);
		dev = bt_get_device_by_handle(handle);
		// reason 0x3B - Unacceptable Connection Parameters
		if (bt_context.verbose)
			hlog_info(BTLOG, "GATT browser - DISCONNECTED %s: status 0x%2X, reason 0x%2X", dev?dev->name:"Uknown",
					  hci_event_disconnection_complete_get_status(packet), hci_event_disconnection_complete_get_reason(packet));
		if (dev)
			bt_reset_device(dev, BT_DEV_DISCONNECTED);
		trigger_scanning();
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
		if (bt_context.verbose)
			hlog_info(BTLOG, "Command status : %d", hci_event_command_complete_get_command_opcode(packet));
		break;
	case HCI_EVENT_TRANSPORT_PACKET_SENT:
	case HCI_EVENT_COMMAND_COMPLETE:
	case BTSTACK_EVENT_SCAN_MODE_CHANGED:
	case HCI_SUBEVENT_LE_SCAN_REQUEST_RECEIVED:
		break;
	default:
		if (bt_context.verbose)
			hlog_info(BTLOG, "Got unknown HCI event 0x%0.2X", hci_event_packet_get_type(packet));
		break;
	}
}

int bt_add_known_device(bt_addr_t addr, char *pin, bt_event_handler_t cb, void *context)
{
	bd_addr_t null_addr;
	int i;

	memset(null_addr, 0, BD_ADDR_LEN);
	if (!memcmp(addr, null_addr, BD_ADDR_LEN)) {
		bt_context.force_init = true;
		return -1;
	}

	for (i = 0; i < BT_MAX_DEVICES; i++) {
		if (!memcmp(bt_context.devcies[i].btaddress, null_addr, BD_ADDR_LEN))
			break;
	}
	if (i == BT_MAX_DEVICES)
		return -1;

	memcpy(bt_context.devcies[i].btaddress, addr, sizeof(bd_addr_t));
	bt_context.devcies[i].pin = strdup(pin);
	bt_context.devcies[i].user_cb = cb;
	bt_context.devcies[i].user_context = context;
	bt_context.devcies[i].id = (i+1)<<16;
	snprintf(bt_context.devcies[i].name, BT_DEV_MAX_NAME, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
			 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	bt_context.dev_count++;
	return bt_context.devcies[i].id;
}

static int bt_discover_next_char(void)
{
	bt_context.current_device->svc_current++;
	if (bt_context.current_device->svc_current >= bt_context.current_device->svc_count)
		return 0;
	bt_context.current_device->state = BT_DEV_DISCOVERING_CHARACTERISTIC;
	bt_context.current_device->discovering = true;
	bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
	if (bt_context.verbose)
		hlog_info(BTLOG, "Device [%s], discovery characteristic for service "UUID_128_FMT, bt_context.current_device->name,
				  UUID_128_PARAM(bt_context.current_device->services[bt_context.current_device->svc_current].gat_svc.uuid128));
	if (!gatt_client_discover_characteristics_for_service(handle_gatt_client_event, bt_context.current_device->connection_handle,
														  &bt_context.current_device->services[bt_context.current_device->svc_current].gat_svc))
		return 1;

	return -1;
}

static int bt_device_state(void)
{
	uint32_t now;
	int ret;

	now = to_ms_since_boot(get_absolute_time());
	switch (bt_context.current_device->state) {
	case BT_DEV_CONNECTED:
		bt_context.current_device->discovering = false;
//		hlog_info(BTLOG,"Discover primary BT services of [%s] ... ", bt_context.current_device->name);
		if (gatt_client_discover_primary_services(handle_gatt_client_event, bt_context.current_device->connection_handle))
			return -1;
		bt_context.current_device->discovering = true;
		bt_context.current_device->state = BT_DEV_DISCOVERING_PRIMARY;
		bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
		break;
	case BT_DEV_DISCOVERING_PRIMARY:
	case BT_DEV_DISCOVERING_SECONDARY:
	case BT_DEV_DISCOVERING_CHARACTERISTIC:
		if (bt_context.current_device->discovering) {
			if ((now - bt_context.current_device->state_time) > CONNECT_TIMEOUT_MS) {
				hlog_info(BTLOG, "Timeout discovering BT services of [%s] ... ", bt_context.current_device->name);
				return -1;
			}
		} else { /* discovery completed */
			if (bt_context.current_device->state == BT_DEV_DISCOVERING_PRIMARY) {
				if (bt_context.verbose)
					hlog_info(BTLOG, "Discover secondary BT services of [%s] ... ", bt_context.current_device->name);
				if (gatt_client_discover_secondary_services(handle_gatt_client_event, bt_context.current_device->connection_handle))
					return -1;
				bt_context.current_device->state = BT_DEV_DISCOVERING_SECONDARY;
				bt_context.current_device->discovering = true;
				bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
			} else { /* BT_DEV_DISCOVERING_SECONDARY or BT_DEV_DISCOVERING_CHARACTERISTIC */
				if (bt_context.current_device->state == BT_DEV_DISCOVERING_SECONDARY)
					bt_context.current_device->svc_current = -1;
				ret = bt_discover_next_char();
				if (ret < 0)
					return ret;
				if (!ret) {
					bt_context.current_device->state = BT_DEV_READY;
					bt_context.current_device->svc_current = -1;
					//bms_bt_char_notify_enable();
					bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
					if (bt_context.verbose)
						hlog_info(BTLOG, "Discovery of [%s] completed, device is ready", bt_context.current_device->name);
					if (bt_context.current_device->user_cb)
						bt_context.current_device->user_cb(bt_context.current_device->id, BT_READY, NULL, 0, bt_context.current_device->user_context);
				}
			}
		}
		break;
	case BT_DEV_DISCONNECTED:
	default:
		break;
	}

	return 0;
}

static void bt_statck_init(void)
{
	l2cap_init();
	sdp_init();
	sm_init();
	sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
	gatt_client_init();

	bt_context.hci_event_cb_reg.callback = &bt_packet_handler;
	hci_add_event_handler(&bt_context.hci_event_cb_reg);
}

static struct bt_device_t *get_device_by_id(uint32_t char_id)
{
	int dev_index, svc_index, char_index;

	GET_INDEX_FROM_ID(char_id, dev_index, svc_index, char_index);

	if (!bt_context.running)
		return NULL;
	if (dev_index < 0 || dev_index >= bt_context.dev_count)
		return NULL;
	return &(bt_context.devcies[dev_index]);
}

static struct bt_svc_t *get_service_by_id(uint32_t svc_id)
{
	int dev_index, svc_index, char_index;
	struct bt_device_t *dev;

	GET_INDEX_FROM_ID(svc_id, dev_index, svc_index, char_index);
	if (!bt_context.running)
		return NULL;
	if (dev_index < 0 || dev_index >= bt_context.dev_count)
		return NULL;
	dev = &(bt_context.devcies[dev_index]);
	if (dev->state == BT_DEV_DISCONNECTED)
		return NULL;
	if (svc_index < 0 || svc_index >= bt_context.devcies[dev_index].svc_count)
		return NULL;

	return &(dev->services[svc_index]);
}

static struct bt_char_t *get_characteristic_by_id(uint32_t char_id)
{
	int dev_index, svc_index, char_index;
	struct bt_device_t *dev;
	struct bt_svc_t *svc;

	GET_INDEX_FROM_ID(char_id, dev_index, svc_index, char_index);

	if (!bt_context.running)
		return NULL;
	if (dev_index < 0 || dev_index >= bt_context.dev_count)
		return NULL;
	dev = &(bt_context.devcies[dev_index]);
	if (dev->state == BT_DEV_DISCONNECTED)
		return NULL;
	if (svc_index < 0 || svc_index >= bt_context.devcies[dev_index].svc_count)
		return NULL;
	svc = &(dev->services[svc_index]);
	if (char_index < 0 || char_index >= bt_context.devcies[dev_index].services[svc_index].char_count)
		return NULL;

	return &(svc->chars[char_index]);
}

/************************ API ************************/
void bt_run(void)
{
	struct bt_device_t *dev = NULL;
	int i;

	if (!hlog_remoute() || (!bt_context.dev_count && !bt_context.force_init))
		return;

	if (!bt_context.started) {
		hlog_info(BTLOG, "Init BT stack");
		bt_statck_init();
		if (!hci_power_control(HCI_POWER_ON))
			bt_context.started = true;
		return;
	}
	if (!bt_context.running)
		return;

	if (bt_context.current_device) {
		if (bt_device_state()) {
			bt_reset_device(bt_context.current_device, BT_DEV_CONNECTED);
			bt_context.current_device->state_time = to_ms_since_boot(get_absolute_time());
			bt_context.current_device = NULL;
		} else if (bt_context.current_device->state == BT_DEV_READY)
			bt_context.current_device = NULL;
	}
	if (!bt_context.current_device) {
		for (i = 0; i < bt_context.dev_count; i++) {
			if (bt_context.devcies[i].state == BT_DEV_READY || bt_context.devcies[i].state == BT_DEV_DISCONNECTED)
				continue;
			if (!dev || (dev->state_time > bt_context.devcies[i].state_time))
				dev = &bt_context.devcies[i];
		}
	}
	if (dev)
		bt_context.current_device = dev;
}

void bt_log_status(void)
{
	int i;

	if (!bt_context.started)
		return;

	hlog_info(BTLOG, "BT stack started, %s, %s.",
				bt_context.running?"running":"not running yet",
				bt_context.scanning?"scanning for devices":"not scanning for devices");
	for (i = 0; i < bt_context.dev_count; i++) {
		if (bt_context.devcies[i].state != BT_DEV_DISCONNECTED)
			hlog_info(BTLOG, "\t  %s to [%s].",
					bt_context.devcies[i].state < BT_DEV_READY?"Connecting":"Connected",
					bt_context.devcies[i].name);
		else
			hlog_info(BTLOG, "\t  Looking for [%s] ...", bt_context.devcies[i].name);
	}
}

bool bt_init(void)
{
	memset(&bt_context, 0, sizeof(bt_context));
	mutex_init(&bt_context.lock);
#ifdef BT_DEBUG
	bt_context.verbose = true;
#endif
	return true;
}

static int notify_characteristic_enable(uint32_t char_id)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	BT_LOCAL_LOCK;
		dev = get_device_by_id(char_id);
		charc = get_characteristic_by_id(char_id);
		if (dev && charc && !charc->notify) {
			gatt_client_listen_for_characteristic_value_updates(&(charc->gat_notify), handle_gatt_client_event, dev->connection_handle, &(charc->gat_char));
			ret = gatt_client_write_client_characteristic_configuration(handle_gatt_client_event, dev->connection_handle,
																		&(charc->gat_char), GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
			if (!ret)
				charc->notify = true;
		}
	BT_LOCAL_UNLOCK;

	return ret;
}


static int notify_characteristic_disable(uint32_t char_id)
{
	struct bt_char_t *charc;

	BT_LOCAL_LOCK;
		charc = get_characteristic_by_id(char_id);
		if (charc && charc->notify)
			gatt_client_stop_listening_for_characteristic_value_updates(&(charc->gat_notify));
	BT_LOCAL_UNLOCK;

	return 0;
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

	BT_LOCAL_LOCK;
		charc = get_characteristic_by_id(id);
		if (charc) {
			if (u128)
				memcpy(*u128, charc->gat_char.uuid128, BT_UUID128_LEN);
			if (u16)
				*u16 = charc->gat_char.uuid16;
			ret = 0;
		}
	BT_LOCAL_UNLOCK;

	return ret;
}

int bt_service_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16)
{
	struct bt_svc_t *svc;
	int ret = -1;

	BT_LOCAL_LOCK;
		svc = get_service_by_id(id);
		if (svc) {
			if (u128)
				memcpy(*u128, svc->gat_svc.uuid128, BT_UUID128_LEN);
			if (u16)
				*u16 = svc->gat_svc.uuid16;
			ret = 0;
		}
	BT_LOCAL_UNLOCK;

	return ret;
}

int bt_characteristic_read(uint32_t char_id)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	BT_LOCAL_LOCK;
		dev = get_device_by_id(char_id);
		charc = get_characteristic_by_id(char_id);
		if (dev && charc)
			ret = gatt_client_read_value_of_characteristic(handle_gatt_client_read_value,
														   dev->connection_handle, &(charc->gat_char));
	BT_LOCAL_UNLOCK;

	return ret;
}

int bt_characteristic_write(uint32_t char_id, uint8_t *data, uint16_t data_len)
{
	struct bt_char_t *charc;
	struct bt_device_t *dev;
	int ret = -1;

	BT_LOCAL_LOCK;
		dev = get_device_by_id(char_id);
		charc = get_characteristic_by_id(char_id);
		if (dev && charc)
			ret = gatt_client_write_value_of_characteristic_without_response(dev->connection_handle,
																			 charc->gat_char.value_handle,
																			 data_len, data);
	BT_LOCAL_UNLOCK;

	return ret;
}

void bt_debug_set(uint32_t lvl)
{
	bt_context.debug = lvl;
}
