// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _MAIN_SHAFT_H_
#define _MAIN_SHAFT_H_

#include "common_lib.h"
#include "base64.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SSR_COUNT 28
#define MAX_SOIL_SENSORS_COUNT 4

void mqtt_irrig_init(int soil_count, int ssr_count);
void mqtt_irrig_send(void);
void mqtt_data_soil(int id, uint32_t analog, uint8_t digital);
void mqtt_data_ssr_data(int id, uint32_t time, uint32_t delay);
void mqtt_data_ssr_state(unsigned int state);
void mqtt_data_internal_temp(float temp);
int soil_init(void);
void soil_measure(void);
int ssr_init(void);
void ssr_run(void);
bool ssr_log(void *context);
int ssr_state_set(uint8_t id, bool value, uint32_t time, uint32_t delay);
void ssr_reset_all(void);

int ssr_cmd_exec(char *cmd);
uint32_t ssr_get_time(int id);
int cmd_irrig_init(void);

#endif /* _MAIN_SHAFT_H_ */
