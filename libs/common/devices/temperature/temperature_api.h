// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_CHIP_TEMP_API_H_
#define _LIB_CHIP_TEMP_API_H_

enum temp_sensor_type {
	TEMPERATURE_TYPE_INTERNAL,
	TEMPERATURE_TYPE_NTC,
};

float temperature_internal_get(void);
int temperature_get_count(enum temp_sensor_type type, uint8_t *count);
int temperature_get_data(enum temp_sensor_type type, int id, float *temperature);

#endif /* _LIB_CHIP_TEMP_API_H_ */

