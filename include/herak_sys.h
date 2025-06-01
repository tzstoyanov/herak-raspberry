// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_HERAK_SYS_H_
#define _LIB_HERAK_SYS_H_

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "common_lib.h"

#include "lcd/lcd_api.h"
#include "bt/bt_api.h"
#include "syscmd/syscmd_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void system_common_main(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_HERAK_SYS_H_ */
