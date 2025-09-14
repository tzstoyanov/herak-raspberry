// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_HERAK_SYS_H_
#define _LIB_HERAK_SYS_H_

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "common_lib.h"

#include "commands/cmd_api.h"
#include "fs/fs_api.h"
#include "cfg_store/cfg_store_api.h"
#include "log/log_api.h"
#include "wifi/wifi_api.h"
#include "lcd/lcd_api.h"
#include "bt/bt_api.h"
#include "mqtt/mqtt_api.h"
#include "usb/usb_api.h"
#include "ntp/ntp_api.h"
#include "webhook/webhook_api.h"
#include "webserver/webserver_api.h"
#include "syscmd/syscmd_api.h"
#include "temperature/temperature_api.h"
#include "ssr/ssr_api.h"
#include "bms_jk/bms_jk_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void system_common_main(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_HERAK_SYS_H_ */
