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
#ifdef HAVE_SHT20
	DEV_REGISTER(sht20_register);
#endif /* HAVE_SHT20 */

#ifdef HAVE_SSR
	DEV_REGISTER(ssr_register);
#endif /* HAVE_SSR */

#ifdef HAVE_SOIL
	DEV_REGISTER(soil_register);
#endif /* HAVE_SOIL */

#ifdef HAVE_OPENTHERM
	DEV_REGISTER(opentherm_register);
#endif /* HAVE_OPENTHERM */	
}
