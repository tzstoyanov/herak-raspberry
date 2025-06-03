// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"

#define SYS_REGISTER(F) { extern void F(void); F(); wd_update(); }

void systems_register_and_init(void)
{

#ifdef HAVE_SYS_LOG
	SYS_REGISTER(sys_log_register);
#endif /* HAVE_SYS_LOG */

#ifdef HAVE_SYS_WIFI
	SYS_REGISTER(sys_wifi_register);
#endif /* HAVE_SYS_WIFI */

#ifdef HAVE_SYS_BT
	SYS_REGISTER(sys_bt_register);
#endif /* HAVE_SYS_BT */

#ifdef HAVE_SYS_MQTT
	SYS_REGISTER(sys_mqtt_register);
#endif /* HAVE_SYS_BT */

#ifdef HAVE_SYS_USB
	SYS_REGISTER(sys_usb_register);
#endif /* HAVE_SYS_USB */

#ifdef HAVE_SYS_WEBSERVER
	SYS_REGISTER(sys_webserver_register);
#endif /* HAVE_SYS_WEBSERVER */

#ifdef HAVE_SYS_COMMANDS
	SYS_REGISTER(sys_commands_register);
#endif /* HAVE_SYS_COMMANDS */

#ifdef HAVE_SYS_WEBHOOK
	SYS_REGISTER(sys_webhook_register);
#endif /* HAVE_SYS_WEBHOOK */
}
