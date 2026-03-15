// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _ONE_WIRE_API_H_
#define _ONE_WIRE_API_H_

int one_wire_get_lines(uint8_t *count);
int one_wire_get_sensors_on_lines(uint8_t line, uint8_t *count);
int one_wire_get_sensor_address(int line_id, int sensor_id, uint64_t *address);
int one_wire_get_sensor_data(int line_id, int sensor_id, float *temperature);

#endif /* _ONE_WIRE_API_H_ */
