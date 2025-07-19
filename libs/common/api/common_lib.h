// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_COMMON_H_
#define _LIB_COMMON_H_

#include "pico/util/datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))
#endif

#ifndef UNUSED
#define UNUSED(x) { (void)(x); }
#endif

#define GPIO_PIN_MIN	0
#define GPIO_PIN_MAX	28

#define LED_ON	{ cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); }
#define LED_OFF { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); }
#define param_get(X) base64_decode(X, X ## _len)
#define USER_PRAM_GET(P)	sys_user_param_get(#P, P, P##_len)
char *sys_user_param_get(char *name, const char *def, int def_len);

bool system_common_init(void);
void system_common_run(void);
void system_force_reboot(int delay_ms);
void system_common_main(void);

int sys_asprintf(char **strp, const char *fmt, ...);
uint8_t sys_value_to_percent(uint32_t range_min, uint32_t range_max, uint32_t val);

uint32_t samples_filter(uint32_t *samples, int total_count, int filter_count);
char *get_current_time_str(char *buf, int buflen);
char *get_current_time_log_str(char *buf, int buflen);
bool tz_datetime_get(datetime_t *date);
uint64_t time_msec2datetime(datetime_t *date, uint64_t msec);
char *time_date2str(char *buf, int str_len, datetime_t *date);
uint64_t time_ms_since_boot(void);

float temperature_internal_get(void);
void dump_hex_data(char *topic, const uint8_t *data, int len);
void dump_char_data(char *topic, const uint8_t *data, int len);
void wd_update(void);


/* manchester code  */
uint64_t manchester_encode(uint32_t frame, bool invert);
int manchester_decode(uint64_t mframe, bool invert, uint32_t *value);

typedef bool (*log_status_cb_t) (void *context);
int add_status_callback(log_status_cb_t cb, void *user_context);


typedef void (*gpio_irq_cb_t) (void *context);
int sys_add_irq_callback(int gpio_pin, gpio_irq_cb_t cb, uint32_t event_mask, void *user_context);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_COMMON_H_ */
