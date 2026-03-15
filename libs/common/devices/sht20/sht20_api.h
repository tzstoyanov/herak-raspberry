// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _SHT20_API_H_
#define _SHT20_API_H_

int sht20_get_count(uint8_t *count);
int sht20_get_data(int id, float *temperature,
				   float *humidity, float *vpd, float *dew_point);

#endif /* _SHT20_API_H_ */
