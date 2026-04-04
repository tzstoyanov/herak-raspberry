// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _ONE_WIRE_API_H_
#define _ONE_WIRE_API_H_

#define ONEWIRE_LINES_MAX	10
#define ONEWIRE_SENORS_MAX	5

#ifdef __cplusplus
extern "C" {
#endif

int one_wire_get_lines(uint8_t *count);
int one_wire_get_sensors_on_line(uint8_t line_id, uint8_t *count);
int one_wire_get_sensor_address(uint8_t line_id, int sensor_id, uint64_t *address);
int one_wire_get_sensor_data(uint8_t line_id, int sensor_id, float *temperature);

#ifdef __cplusplus
}
#endif


#endif /* _ONE_WIRE_API_H_ */
