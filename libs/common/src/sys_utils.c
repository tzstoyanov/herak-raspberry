// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_lib.h"
#include "common_internal.h"
#include "pico/stdlib.h"

int sys_asprintf(char **strp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	int size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (size < 0)
		return -1;

	*strp = calloc(1, size + 1);
	if (!*strp)
		return -1;

	va_start(args, fmt);
	vsnprintf(*strp, size + 1, fmt, args);
	va_end(args);
	return size;
}

uint8_t sys_value_to_percent(uint32_t range_min, uint32_t range_max, uint32_t val)
{
	if (val <= range_min)
		return 0;
	if (val >= range_max)
		return 100;

	return	(100 * (val - range_min)) / (range_max - range_min);
}
