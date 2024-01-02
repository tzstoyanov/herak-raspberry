// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "common_internal.h"

/* Log app status every hour */
__attribute__ ((weak)) void main_log()
{

}

/* USB dymmy APIs */
__attribute__ ((weak)) void usb_log_status()
{

}
__attribute__ ((weak)) bool usb_init(void)
{
	return false;
}
__attribute__ ((weak)) void usb_run()
{

}
__attribute__ ((weak)) int usb_send_to_device(int idx, char *buf, int len)
{
	return 0;
}
__attribute__ ((weak)) int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context)
{
	return 0;
}
