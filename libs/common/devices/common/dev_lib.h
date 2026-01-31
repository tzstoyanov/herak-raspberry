// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _DEV_LIB_H_
#define _DEV_LIB_H_

struct adc_sensor_t;

struct adc_sensor_t *adc_sensor_init(int pin, double a, double b);
bool adc_sensor_measure(struct adc_sensor_t *sensor);
float adc_sensor_get_value(struct adc_sensor_t *sensor);    // a : (a + b*4095)
float adc_sensor_get_volt(struct adc_sensor_t *sensor);     // a : (a + b*3.3) V
int adc_sensor_get_percent(struct adc_sensor_t *sensor);    // 0 : 100 %
uint32_t adc_sensor_get_raw(struct adc_sensor_t *sensor);

#endif /* _DEV_LIB_H_ */

