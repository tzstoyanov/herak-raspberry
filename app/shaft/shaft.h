// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _MAIN_SHAFT_H_
#define _MAIN_SHAFT_H_

#include "common_lib.h"
#include "base64.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_data_sonar(float distance);
void mqtt_data_internal_temp(float temp);
bool sonar_init(void);
void sonar_measure(void);

#endif /* _MAIN_SHAFT_H_ */
