// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"

#define DEV_REGISTER(F) {extern void F(void); F(); wd_update();}

void devices_register_and_init(void)
{
	DEV_REGISTER(ssr_register);
}
