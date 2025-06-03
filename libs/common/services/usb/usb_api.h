// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_USB_API_H_
#define _LIB_SYS_USB_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* USB API */
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

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_USB_API_H_*/

