// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"

#define SYSMODLOG	"dev_reg"

#define SYS_REG_DEBUG	false
#define DEV_REGISTER(F) {\
		extern void F(void);\
		if (SYS_REG_DEBUG)\
			hlog_info(SYSMODLOG,"Call %s", #F);\
		F();\
		wd_update();\
	}

void devices_register_and_init(void)
{
#ifdef HAVE_ONE_WIRE
	DEV_REGISTER(one_wire_register);
#endif /* HAVE_ONE_WIRE */

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

#ifdef HAVE_BMS_JK
	DEV_REGISTER(bms_jk_register);
#endif /* HAVE_BMS_JK */

#ifdef HAVE_LCD
	DEV_REGISTER(lcd_register);
#endif /* HAVE_LCD */

#ifdef HAVE_CHIP_TEMP
	DEV_REGISTER(chip_temperature_register);
#endif /* HAVE_CHIP_TEMP */

#ifdef HAVE_SONAR
	DEV_REGISTER(sonar_register);
#endif /* HAVE_LCD */

}
