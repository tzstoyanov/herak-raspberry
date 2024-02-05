// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _INTERNAL_COMMON_H_
#define _INTERNAL_COMMON_H_

#include "pico/util/datetime.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "common_lib.h"
#include "lwip/sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_USER_AGENT		"PicoW"
#define LWIP_LOCK_START { \
		SYS_ARCH_DECL_PROTECT(__lev__); \
		SYS_ARCH_PROTECT(__lev__); \
		cyw43_arch_lwip_begin();

#define LWIP_LOCK_END \
	cyw43_arch_lwip_end(); \
	SYS_ARCH_UNPROTECT(__lev__); }

#define SYS_LOCK_START { \
		SYS_ARCH_DECL_PROTECT(__lev__); \
		SYS_ARCH_PROTECT(__lev__);

#define SYS_LOCK_END \
	SYS_ARCH_UNPROTECT(__lev__); }

#define __weak	__attribute__((__weak__))

void system_reconect(void);

typedef enum {
	IP_NOT_RESOLEVED = 0,
	IP_RESOLVING,
	IP_RESOLVED
} ip_resolve_state_t;

bool wifi_init(void);
bool wifi_connect(void);
bool wifi_is_connected(void);
void wifi_log_status(void);

bool mqtt_init(void);
void mqtt_connect(void);
bool mqtt_is_connected(void);
void mqtt_log_status(void);
void mqtt_reconnect(void);

bool bt_init(void);
void bt_run(void);
void bt_log_status(void);

bool ntp_init(void);
void ntp_connect(void);
bool ntp_connected(void);

bool temperature_init(void);
void temperature_measure(void);

bool sw_out_init(void);
void sw_out_set(bool state);

void usb_log_status(void);

bool lcd_init(void);
void lcd_refresh(void);

void main_log(void);

void hlog_init(int level);
void hlog_connect(void);
void hlog_status(void);
void hlog_reconnect(void);

void system_log_status(void);
char *get_uptime(void);
uint32_t get_free_heap(void);
uint32_t get_total_heap(void);

bool webhook_init(void);
void webhook_run(void);
void webhook_log_status(void);
void webhook_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* _INTERNAL_COMMON_H_ */
