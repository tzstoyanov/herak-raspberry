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

#ifdef HAVE_SYS_BT
	SYS_REGISTER(sys_bt_register);
#endif /* HAVE_SYS_BT */

}
