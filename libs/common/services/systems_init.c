// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"

#define SYSMODLOG	"sys_reg"

#define SYS_REG_DEBUG	false
#define SYS_REGISTER(F) {\
		extern void F(void);\
		if (SYS_REG_DEBUG)\
			hlog_info(SYSMODLOG,"Call %s", #F);\
		F();\
		wd_update();\
	}

void systems_register_and_init(void)
{

#ifdef HAVE_SYS_FS
	SYS_REGISTER(sys_fs_register);
#endif /* HAVE_SYS_FS */

#ifdef HAVE_SYS_CFG_STORE
	SYS_REGISTER(sys_cfg_store_register);
#endif /* HAVE_SYS_FS */

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

#ifdef HAVE_SYS_NTP
	SYS_REGISTER(sys_ntp_register);
#endif /* HAVE_SYS_NTP */

#ifdef HAVE_SYS_WEBSERVER
	SYS_REGISTER(sys_webserver_register);
#endif /* HAVE_SYS_WEBSERVER */

#ifdef HAVE_SYS_COMMANDS
	SYS_REGISTER(sys_syscmd_register);
#endif /* HAVE_SYS_COMMANDS */

#ifdef HAVE_SYS_WEBHOOK
	SYS_REGISTER(sys_webhook_register);
#endif /* HAVE_SYS_WEBHOOK */

#ifdef HAVE_SYS_TFTP_CLIENT
	SYS_REGISTER(sys_tftp_client_register);
#endif /* HAVE_SYS_SCRIPTS */

#ifdef HAVE_COMMANDS
	SYS_REGISTER(sys_commands_register);
#endif /* HAVE_SYS_SCRIPTS */

#ifdef HAVE_SYS_SCRIPTS
	SYS_REGISTER(sys_scripts_register);
#endif /* HAVE_SYS_SCRIPTS */

#ifdef HAVE_SYS_STATE
	SYS_REGISTER(sys_state_register);
#endif /* HAVE_SYS_STATE */

#ifdef HAVE_OTA
	SYS_REGISTER(sys_ota_register);
#endif /* HAVE_OTA */

#ifdef HAVE_WOL
	SYS_REGISTER(sys_wol_register);
#endif /* HAVE_WOL */

}
