// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "tusb.h"
#include "host/hcd.h"
#include "bsp/board.h"
#include "pio_usb_configuration.h"
#include "pio_usb.h"

#include "common_lib.h"

#include "base64.h"
#include "params.h"

#define USBLOG	"usb"

#define IS_DEBUG	(usb_context.debug != 0)

#define RAW_INTERFACE
//#define CDC_INTERFACE

// English
#define LANGUAGE_ID	0x0409
#define BUF_COUNT	4
#define BUFF_SIZE	64

#define MAX_USB_DEVICES	2
#define USB_RCV_REUQEST_PING_MS	200

#define USB_LOCK	mutex_enter_blocking(&usb_context.lock)
#define USB_UNLOCK	mutex_exit(&usb_context.lock)

struct usb_port_t {
	int pin_dp;
	int pin_dm;
};

struct usb_dev_t {
	int			index;
	uint8_t		dev_addr;
	uint8_t		instance;
	uint8_t		cdc_index;
	bool		hid_mount;
	bool		cdc_mount;
	void		*user_context;
	uint32_t	connect_count;
	usb_dev_desc_t desc;
	usb_event_handler_t user_cb;
};

static struct {
	struct usb_dev_t	devices[MAX_USB_DEVICES];
	int			dev_count;
	struct usb_port_t	ports[PIO_USB_DEVICE_CNT];
	int			port_count;
	mutex_t		lock;
	bool		force_init;

	uint8_t buf_pool[BUF_COUNT][BUFF_SIZE];
	uint8_t buf_owner[BUF_COUNT]; // device address that owns buffer
	tusb_desc_device_t desc_device;
	uint32_t debug;
} usb_context;

static struct usb_dev_t *get_usb_device_by_vidpid(uint16_t vid, uint16_t pid)
{
	int i;

	for (i = 0; i < usb_context.dev_count; i++) {
		if (usb_context.devices[i].desc.vid == vid && usb_context.devices[i].desc.pid == pid)
			return &usb_context.devices[i];
	}

	return NULL;
}

int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context)
{
	int i = 0;

	USB_LOCK;
		if (vid) {
			for (i = 0; i < MAX_USB_DEVICES; i++) {
				if (!usb_context.devices[i].desc.vid && !usb_context.devices[i].desc.pid) {
					usb_context.devices[i].index = i;
					usb_context.devices[i].desc.vid = vid;
					usb_context.devices[i].desc.pid = pid;
					usb_context.devices[i].user_cb = cb;
					usb_context.devices[i].user_context = context;
					usb_context.dev_count++;
					if (IS_DEBUG)
						hlog_info(USBLOG, "New known device added: %0.4X:%0.4X", vid, pid);
					break;
				}
			}
		} else {
			usb_context.force_init = true;
		}
	USB_UNLOCK;

	if (i >= MAX_USB_DEVICES) {
		if (IS_DEBUG)
			hlog_info(USBLOG, "Cannot add new known device %0.4X:%0.4X, limit reached", vid, pid);
		i = -1;
	}

	return i;
}

int usb_send_to_device(int idx, char *buf, int len)
{
	int ret;

	if (idx < 0 || idx >= usb_context.dev_count)
		return -1;
	if (!usb_context.devices[idx].hid_mount)
		return -1;

	USB_LOCK;

//		report_id = 0, report_type = HID_REPORT_TYPE_OUTPUT, report = led bitmask (1 for each LED), len = 1
		ret = tuh_hid_set_report(usb_context.devices[idx].dev_addr, usb_context.devices[idx].instance, 0, HID_REPORT_TYPE_OUTPUT, buf, len);
//		ret = tuh_cdc_write(usb_context.devices[idx].cdc_index, buf, len);
//		tuh_cdc_write_flush(usb_context.devices[idx].cdc_index);
	USB_UNLOCK;
	if (IS_DEBUG)
		hlog_info(USBLOG, "Sent %d bytes to device %0.4X:%0.4X: %d",
				  len, usb_context.devices[idx].desc.vid, usb_context.devices[idx].desc.pid, ret);
	if (ret)
		return 0;
	return -1;
}

static void usb_log_status(void *context)
{
	bool mounted;
	int i;

	UNUSED(context);

	USB_LOCK;
		hlog_info(USBLOG, "Initialized on %d, USB ports:", BOARD_TUH_RHPORT);
		hlog_info(USBLOG, "Status 0: %d %d", hcd_port_connect_status(0), hcd_port_speed_get(0));
		hlog_info(USBLOG, "Status 1: %d %d", hcd_port_connect_status(1), hcd_port_speed_get(1));
		for (i = 0; i < usb_context.port_count; i++)
			hlog_info(USBLOG, "\t%d,%d", usb_context.ports[i].pin_dp, usb_context.ports[i].pin_dm);
		for (i = 0; i < usb_context.dev_count; i++) {
			mounted = tuh_hid_mounted(usb_context.devices[i].dev_addr, usb_context.devices[i].instance);
			if (usb_context.devices[i].hid_mount || usb_context.devices[i].cdc_mount)
				hlog_info(USBLOG, "Connected to %s device %0.4X:%0.4X, mounted %d, connect count %d",
						  usb_context.devices[i].hid_mount?"HID":"CDC",
						  usb_context.devices[i].desc.vid, usb_context.devices[i].desc.pid, mounted,
						  usb_context.devices[i].connect_count);
			else
				hlog_info(USBLOG, "Looking for %0.4X:%0.4X ... connect count %d",
						 usb_context.devices[i].desc.vid, usb_context.devices[i].desc.pid,
						 usb_context.devices[i].connect_count);
		}
	USB_UNLOCK;
}

void usb_debug_set(uint32_t lvl)
{
	usb_context.debug = lvl;
}

static void usb_read_config(void)
{
	char *str, *rest, *tok, *tok1;
	int i;

	if (USB_PORTS_len < 1)
		return;
	str = param_get(USB_PORTS);
	rest = str;
	usb_context.port_count = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && usb_context.port_count < PIO_USB_DEVICE_CNT) {
		i = 0;
		while (i < 2 && (tok1 = strtok_r(tok, ",", &tok))) {
			if (i == 0)
				usb_context.ports[usb_context.port_count].pin_dp = atoi(tok1);
			else if (i == 1)
				usb_context.ports[usb_context.port_count].pin_dm = atoi(tok1);
			i++;
		}
		usb_context.port_count++;
	}
	free(str);
	if (!usb_context.port_count)
		return;
	for (i = 0; i < usb_context.port_count; i++)
		hlog_info(USBLOG, "Got port %d,%d", usb_context.ports[i].pin_dp, usb_context.ports[i].pin_dm);
}

void usb_bus_restart(void)
{

	tuh_rhport_reset_bus(BOARD_TUH_RHPORT, true);
	sleep_ms(50);
	tuh_rhport_reset_bus(BOARD_TUH_RHPORT, false);

	if (IS_DEBUG)
		hlog_info(USBLOG, "BUS restarted.");
}

static bool usb_stack_init(void)
{
	static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
	int i;

	board_init();
	if (usb_context.port_count)
		config.pin_dp = usb_context.ports[0].pin_dp;
	if (usb_context.ports[0].pin_dm > usb_context.ports[0].pin_dp)
		config.pinout = PIO_USB_PINOUT_DPDM;
	else
		config.pinout = PIO_USB_PINOUT_DMDP;
	if (!tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config))
		goto err;
	if (!tuh_init(BOARD_TUH_RHPORT))
		goto err;
	for (i = 1; i < usb_context.port_count; i++) {
		if (usb_context.ports[i].pin_dm > usb_context.ports[i].pin_dp)
			pio_usb_host_add_port(usb_context.ports[i].pin_dp, PIO_USB_PINOUT_DPDM);
		else
			pio_usb_host_add_port(usb_context.ports[i].pin_dp, PIO_USB_PINOUT_DMDP);
	}

	hlog_info(USBLOG, "USB initialized, looking for %d known devices", usb_context.dev_count);
	for (i = 0; i < usb_context.dev_count; i++)
		hlog_info(USBLOG, "\t%0.4X:%0.4X", usb_context.devices[i].desc.vid, usb_context.devices[i].desc.pid);
	return true;
err:
	hlog_warning(USBLOG, "Failed to init USB subsystem");
	return false;
}

bool usb_init(void)
{
	memset(&usb_context, 0, sizeof(usb_context));
	mutex_init(&usb_context.lock);
	usb_read_config();
	add_status_callback(usb_log_status, NULL);
	return usb_stack_init();
}

void usb_run(void)
{
	static uint32_t last;
	uint32_t now;
	int i;

	if (!usb_context.dev_count && !usb_context.force_init)
		return;

	now = to_ms_since_boot(get_absolute_time());
	if ((now - last) >= USB_RCV_REUQEST_PING_MS) {
		last = now;
		USB_LOCK;
			for (i = 0; i < usb_context.dev_count; i++) {
				if (usb_context.devices[i].hid_mount)
					tuh_hid_receive_report(usb_context.devices[i].dev_addr, usb_context.devices[i].instance);
			}
		USB_UNLOCK;
	}
	tuh_task();
}

#ifdef RAW_INTERFACE
//--------------------------------------------------------------------+
// String Descriptor Helper
//--------------------------------------------------------------------+

static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len)
{

	UNUSED(utf8_len);
	// Get the UTF-16 length out of the data itself.
	for (size_t i = 0; i < utf16_len; i++) {
		uint16_t chr = utf16[i];

		if (chr < 0x80) {
			*utf8++ = chr & 0xffu;
		} else if (chr < 0x800) {
			*utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
			*utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
		} else { // TODO: Verify surrogate.
			*utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
			*utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
			*utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
		}
		// TODO: Handle UTF-16 code points that take two entries.
	}
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t *buf, size_t len)
{
	size_t total_bytes = 0;

	for (size_t i = 0; i < len; i++) {
		uint16_t chr = buf[i];

		if (chr < 0x80)
			total_bytes += 1;
		else if (chr < 0x800)
			total_bytes += 2;
		else
			total_bytes += 3;
		// TODO: Handle UTF-16 code points that take two entries.
	}
	return (int) total_bytes;
}

static void print_utf16(uint16_t *temp_buf, size_t buf_len)
{
	size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
	size_t utf8_len = (size_t) _count_utf8_bytes(temp_buf + 1, utf16_len);

	_convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *) temp_buf, sizeof(uint16_t)*buf_len);
	((uint8_t *) temp_buf)[utf8_len] = '\0';

	printf((char *)temp_buf);
}


//--------------------------------------------------------------------+
// TinyUSB RAW API
//--------------------------------------------------------------------+

static uint16_t count_interface_total_len(tusb_desc_interface_t const *desc_itf, uint8_t itf_count, uint16_t max_len)
{
	uint8_t const *p_desc = (uint8_t const *) desc_itf;
	uint16_t len = 0;

	while (itf_count--) {
		// Next on interface desc
		len += tu_desc_len(desc_itf);
		p_desc = tu_desc_next(p_desc);

		while (len < max_len) {
			// return on IAD regardless of itf count
			if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION)
				return len;

			if ((tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) &&
				 ((tusb_desc_interface_t const *) p_desc)->bAlternateSetting == 0)
				break;

			len += tu_desc_len(p_desc);
			p_desc = tu_desc_next(p_desc);
		}
	}
	return len;
}

static void hid_report_received(tuh_xfer_t *xfer)
{
	// Note: not all field in xfer is available for use (i.e filled by tinyusb stack) in callback to save sram
	// For instance, xfer->buffer is NULL. We have used user_data to store buffer when submitted callback
	uint8_t *buf = (uint8_t *) xfer->user_data;

	if (xfer->result == XFER_RESULT_SUCCESS) {
		hlog_info(USBLOG, "[dev %u: ep %02x] HID Report:", xfer->daddr, xfer->ep_addr);
		for (uint32_t i = 0; i < xfer->actual_len; i++)
			hlog_info(USBLOG, "%02X ", buf[i]);
	}

	// continue to submit transfer, with updated buffer
	// other field remain the same
	xfer->buflen = BUFF_SIZE;
	xfer->buffer = buf;
	tuh_edpt_xfer(xfer);
}


//--------------------------------------------------------------------+
// Buffer helper
//--------------------------------------------------------------------+

// get an buffer from pool
uint8_t *get_hid_buf(uint8_t daddr)
{
	for (size_t i = 0; i < BUF_COUNT; i++) {
		if (usb_context.buf_owner[i] == 0) {
			usb_context.buf_owner[i] = daddr;
			return usb_context.buf_pool[i];
		}
	}

	// out of memory, increase BUF_COUNT
	return NULL;
}

// free all buffer owned by device
void free_hid_buf(uint8_t daddr)
{
	for (size_t i = 0; i < BUF_COUNT; i++)
		if (usb_context.buf_owner[i] == daddr)
			usb_context.buf_owner[i] = 0;
}

static void open_hid_interface(uint8_t daddr, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
	uint8_t const *p_desc = (uint8_t const *) desc_itf;
	uint16_t drv_len;

	// len = interface + hid + n*endpoints
	drv_len = (uint16_t) (sizeof(tusb_desc_interface_t) + sizeof(tusb_hid_descriptor_hid_t) +
											desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));

	// corrupted descriptor
	if (max_len < drv_len)
		return;

	// HID descriptor
	p_desc = tu_desc_next(p_desc);
	tusb_hid_descriptor_hid_t const *desc_hid = (tusb_hid_descriptor_hid_t const *) p_desc;

	if (desc_hid->bDescriptorType != HID_DESC_TYPE_HID)
		return;

	// Endpoint descriptor
	p_desc = tu_desc_next(p_desc);
	tusb_desc_endpoint_t const *desc_ep = (tusb_desc_endpoint_t const *) p_desc;

	for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
		if (desc_ep->bDescriptorType != TUSB_DESC_ENDPOINT)
			return;

		if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
			// skip if failed to open endpoint
			if (!tuh_edpt_open(daddr, desc_ep))
				return;

			uint8_t *buf = get_hid_buf(daddr);

			if (!buf)
				return; // out of memory

			tuh_xfer_t xfer = {
				.daddr       = daddr,
				.ep_addr     = desc_ep->bEndpointAddress,
				.buflen      = BUFF_SIZE,
				.buffer      = buf,
				.complete_cb = hid_report_received,
				.user_data   = (uintptr_t) buf, // since buffer is not available in callback, use user data to store the buffer
			};

			// submit transfer for this EP
			tuh_edpt_xfer(&xfer);

			hlog_info(USBLOG, "Listen to [dev %u: ep %02x]\r\n", daddr, desc_ep->bEndpointAddress);
		}

		p_desc = tu_desc_next(p_desc);
		desc_ep = (tusb_desc_endpoint_t const *) p_desc;
	}
}

// simple configuration parser to open and listen to HID Endpoint IN
void parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const *desc_cfg)
{
	uint8_t const *desc_end	= ((uint8_t const *) desc_cfg) + tu_le16toh(desc_cfg->wTotalLength);
	uint8_t const *p_desc	= tu_desc_next(desc_cfg);

	// parse each interfaces
	while (p_desc < desc_end) {
		uint8_t assoc_itf_count = 1;

		// Class will always starts with Interface Association (if any) and then Interface descriptor
		if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION) {
			tusb_desc_interface_assoc_t const *desc_iad = (tusb_desc_interface_assoc_t const *) p_desc;

			assoc_itf_count = desc_iad->bInterfaceCount;
			p_desc = tu_desc_next(p_desc); // next to Interface
		}

		// must be interface from now
		if (tu_desc_type(p_desc) != TUSB_DESC_INTERFACE)
			return;
		tusb_desc_interface_t const *desc_itf = (tusb_desc_interface_t const *)p_desc;
		uint16_t const drv_len = count_interface_total_len(desc_itf, assoc_itf_count, (uint16_t)(desc_end-p_desc));

		// probably corrupted descriptor
		if (drv_len < sizeof(tusb_desc_interface_t))
			return;

		// only open and listen to HID endpoint IN
		if (desc_itf->bInterfaceClass == TUSB_CLASS_HID)
			open_hid_interface(dev_addr, desc_itf, drv_len);

		// next Interface or IAD descriptor
		p_desc += drv_len;
	}
}

static void print_device_descriptor(tuh_xfer_t *xfer)
{
	uint8_t const daddr = xfer->daddr;
	uint16_t temp_buf[128];

	if (xfer->result != XFER_RESULT_SUCCESS) {
		hlog_info(USBLOG, "Failed to get device descriptor");
		return;
	}

	hlog_info(USBLOG, "Device %u: ID %04x:%04x\r\n", daddr, usb_context.desc_device.idVendor, usb_context.desc_device.idProduct);
	hlog_info(USBLOG, "Device Descriptor:\r\n");
	hlog_info(USBLOG, "  bLength             %u\r\n",	usb_context.desc_device.bLength);
	hlog_info(USBLOG, "  bDescriptorType     %u\r\n",	usb_context.desc_device.bDescriptorType);
	hlog_info(USBLOG, "  bcdUSB              %04x\r\n",	usb_context.desc_device.bcdUSB);
	hlog_info(USBLOG, "  bDeviceClass        %u\r\n",	usb_context.desc_device.bDeviceClass);
	hlog_info(USBLOG, "  bDeviceSubClass     %u\r\n",	usb_context.desc_device.bDeviceSubClass);
	hlog_info(USBLOG, "  bDeviceProtocol     %u\r\n",	usb_context.desc_device.bDeviceProtocol);
	hlog_info(USBLOG, "  bMaxPacketSize0     %u\r\n",	usb_context.desc_device.bMaxPacketSize0);
	hlog_info(USBLOG, "  idVendor            0x%04x\r\n",	usb_context.desc_device.idVendor);
	hlog_info(USBLOG, "  idProduct           0x%04x\r\n",	usb_context.desc_device.idProduct);
	hlog_info(USBLOG, "  bcdDevice           %04x\r\n",	usb_context.desc_device.bcdDevice);

	// Get String descriptor using Sync API
	hlog_info(USBLOG, "  iManufacturer       %u     ", usb_context.desc_device.iManufacturer);
	if (tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)) == XFER_RESULT_SUCCESS)
		print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));

	hlog_info(USBLOG, "  iProduct            %u     ", usb_context.desc_device.iProduct);
	if (tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)) == XFER_RESULT_SUCCESS)
		print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));

	hlog_info(USBLOG, "  iSerialNumber       %u     ", usb_context.desc_device.iSerialNumber);
	if (tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)) == XFER_RESULT_SUCCESS)
		print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));

	hlog_info(USBLOG, "  bNumConfigurations  %u\r\n", usb_context.desc_device.bNumConfigurations);

	// Get configuration descriptor with sync API
	if (tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf)) == XFER_RESULT_SUCCESS)
		parse_config_descriptor(daddr, (tusb_desc_configuration_t *) temp_buf);
}

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t daddr)
{
	hlog_info(USBLOG, "RAW Device attached, address = %d\r\n", daddr);

	// Get Device Descriptor
	// TODO: invoking control transfer now has issue with mounting hub with multiple devices attached, fix later
	tuh_descriptor_get_device(daddr, &usb_context.desc_device, 18, print_device_descriptor, 0);
}
#endif /* RAW_INTERFACE */

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

#define MAX_REPORT  4

// Each HID instance can has multiple reports
static struct
{
	uint8_t report_count;
	tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
	struct usb_dev_t *dev = NULL;
	uint8_t itf_protocol;
	uint16_t vid, pid;

	tuh_vid_pid_get(dev_addr, &vid, &pid);
	itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

	if (IS_DEBUG)
		hlog_info(USBLOG, "hid_mount_cb HID device %0.4X:%0.4X is mounted: address = %X, instance = %d, proto %d",
				  vid, pid, dev_addr, instance, itf_protocol);

	USB_LOCK;
		dev = get_usb_device_by_vidpid(vid, pid);
		if (dev) {
			dev->dev_addr = dev_addr;
			dev->instance = instance;
			if (!dev->hid_mount)
				dev->connect_count++;
			dev->hid_mount = true;
		}
		if (dev->user_cb)
			dev->user_cb(dev->index, HID_MOUNT, &(dev->desc), sizeof(dev->desc), dev->user_context);
	USB_UNLOCK;

	if (!dev) {
		uint8_t pr, inst;

		pr = tuh_hid_get_protocol(dev_addr, instance);
		inst = tuh_hid_instance_count(dev_addr);

		hlog_info(USBLOG, "Unknown HID device %0.4X:%0.4X is mounted: address = %X, instance = %d, proto %d, pr %d, inst %d",
				  vid, pid, dev_addr, instance, itf_protocol, pr, inst);
	}

	// By default host stack will use activate boot protocol on supported interface.
	// Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
	if (itf_protocol == HID_ITF_PROTOCOL_NONE) {
		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
		hlog_info(USBLOG, "HID has %u reports, desc len %d", hid_info[instance].report_count, desc_len);
	}

	// request to receive report
	// tuh_hid_report_received_cb() will be invoked when report is available
	if (!tuh_hid_receive_report(dev_addr, instance))
		hlog_info(USBLOG, "Error: cannot request to receive report");
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	struct usb_dev_t *dev = NULL;
	uint16_t vid, pid;

	tuh_vid_pid_get(dev_addr, &vid, &pid);

	if (IS_DEBUG)
		hlog_info(USBLOG, "hid_unmount_cb HID device %0.4X:%0.4X is mounted: address = %X, instance = %d",
				  vid, pid, dev_addr, instance);
	USB_LOCK;
		dev = get_usb_device_by_vidpid(vid, pid);
		if (dev)
			dev->hid_mount = false;
		if (dev->user_cb)
			dev->user_cb(dev->index, HID_UNMOUNT, &(dev->desc), sizeof(dev->desc), dev->user_context);
	USB_UNLOCK;

	if (!dev)
		hlog_info(USBLOG, "Unknown HID device %0.4X:%0.4X is unmounted: address = %X, instance = %d",
				  vid, pid, dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
	struct usb_dev_t *dev = NULL;
	uint16_t vid, pid;

	tuh_vid_pid_get(dev_addr, &vid, &pid);

	if (IS_DEBUG)
		hlog_info(USBLOG, "hid_report_received_cb HID device %0.4X:%0.4X is mounted: address = %X, instance = %d",
				  vid, pid, dev_addr, instance);

	USB_LOCK;
		dev = get_usb_device_by_vidpid(vid, pid);
		if (dev->user_cb)
			dev->user_cb(dev->index, HID_REPORT, report, len, dev->user_context);
	USB_UNLOCK;

	if (!dev) {
		char print_buff[32], buf[4];
		int i = 0, j = 0;

		hlog_info(USBLOG, "Got HID report from unknown device (%0.4X:%0.4X): address %X instance = %d, report len %d",
				  vid, pid, dev_addr, instance, len);
		print_buff[0] = 0;
		while (i < len) {
			snprintf(buf, 4, "%.2X ", report[i++]);
			strcat(print_buff, buf);
			j += 4;
			if (j >= 32 || i >= len) {
				hlog_info(USBLOG, "\t %s", print_buff);
				j = 0;
				print_buff[0] = 0;
			}
		}
	}

	// continue to request to receive report
	if (!tuh_hid_receive_report(dev_addr, instance))
		hlog_info(USBLOG, "Error: cannot request to receive report");
}

#ifdef CDC_INTERFACE
void tuh_cdc_mount_cb(uint8_t idx)
{
	tuh_cdc_itf_info_t itf_info = { 0 };

	tuh_cdc_itf_get_info(idx, &itf_info);

	hlog_info(USBLOG, "CDC Interface is mounted %d: address = %X, itf_num = %u, subclass %X, proto %X",
			  idx, itf_info.daddr, itf_info.bInterfaceNumber, itf_info.bInterfaceSubClass, itf_info.bInterfaceProtocol);

#ifdef CFG_TUH_CDC_LINE_CODING_ON_ENUM
	// CFG_TUH_CDC_LINE_CODING_ON_ENUM must be defined for line coding is set by tinyusb in enumeration
	// otherwise you need to call tuh_cdc_set_line_coding() first
	cdc_line_coding_t line_coding = { 0 };

	if (tuh_cdc_get_local_line_coding(idx, &line_coding)) {
		hlog_info(USBLOG, "  Baudrate: %lu, Stop Bits : %u", line_coding.bit_rate, line_coding.stop_bits);
		hlog_info(USBLOG, "  Parity  : %u, Data Width: %u", line_coding.parity, line_coding.data_bits);
	}
#endif
}

void tuh_cdc_umount_cb(uint8_t idx)
{
	tuh_cdc_itf_info_t itf_info = { 0 };

	tuh_cdc_itf_get_info(idx, &itf_info);
	hlog_info(USBLOG, "CDC Interface is unmounted %d: address = %X, itf_num = %u, subclass %X, proto %X",
			  idx, itf_info.daddr, itf_info.bInterfaceNumber, itf_info.bInterfaceSubClass, itf_info.bInterfaceProtocol);
}

// Invoked when received new data
void tuh_cdc_rx_cb(uint8_t idx)
{
	tuh_cdc_itf_info_t itf_info = { 0 };

	tuh_cdc_itf_get_info(idx, &itf_info);
	uint8_t buf[BUFF_SIZE+1];

	// forward cdc interfaces -> console
	uint32_t count = tuh_cdc_read(idx, buf, BUFF_SIZE);

	buf[count] = 0;

	hlog_info(USBLOG, "Received %d bytes from device %d: address = %X, itf_num = %u",
			   count, idx, itf_info.daddr, itf_info.bInterfaceNumber);
}

#endif /* CDC_INTERFACE */
