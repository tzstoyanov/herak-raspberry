// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "common_internal.h"

/* Log app status every hour */
__weak void main_log(void)
{

}

/* USB dymmy APIs */
__weak void usb_log_status(void)
{

}
__weak bool usb_init(void)
{
	return false;
}
__weak void usb_run(void)
{

}
__weak int usb_send_to_device(int idx, char *buf, int len)
{
	UNUSED(idx);
	UNUSED(buf);
	UNUSED(len);

	return 0;
}
__weak int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context)
{
	UNUSED(vid);
	UNUSED(pid);
	UNUSED(cb);
	UNUSED(context);

	return 0;
}

__weak  void usb_debug_set(uint32_t lvl)
{
	UNUSED(lvl);
}
