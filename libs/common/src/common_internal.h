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

#define LWIP_LOCK_START	cyw43_arch_lwip_begin();
#define LWIP_LOCK_END	cyw43_arch_lwip_end();
/*
#define LWIP_LOCK_START { \
		SYS_ARCH_DECL_PROTECT(__lev__); \
		SYS_ARCH_PROTECT(__lev__); \
		cyw43_arch_lwip_begin();

#define LWIP_LOCK_END \
	cyw43_arch_lwip_end(); \
	SYS_ARCH_UNPROTECT(__lev__); }
*/

#define SYS_LOCK_START { \
		SYS_ARCH_DECL_PROTECT(__lev__); \
		SYS_ARCH_PROTECT(__lev__);

#define SYS_LOCK_END \
	SYS_ARCH_UNPROTECT(__lev__); }

#ifndef __weak
#define __weak	__attribute__((__weak__))
#endif

typedef struct {
	app_command_t *hooks;
	int count;
	char *description;
} sys_commands_t;

typedef void (*sys_module_run_cb_t) (void *context);
typedef void (*sys_module_debug_cb_t) (uint32_t debug, void *context);
typedef struct {
	char *name;
	sys_commands_t commands;
	void *context;
	/* Callbacks */
	sys_module_run_cb_t run;
	sys_module_run_cb_t reconnect;
	log_status_cb_t	log;
	sys_module_debug_cb_t debug;
} sys_module_t;
int sys_module_register(sys_module_t *module);

void sys_modules_init(void);
void sys_modules_run(void);
void sys_modules_log(void);
void sys_modules_reconnect(void);

void system_reconnect(void);
void system_set_periodic_log_ms(uint32_t ms);

typedef enum {
	IP_NOT_RESOLEVED = 0,
	IP_RESOLVING,
	IP_RESOLVED
} ip_resolve_state_t;

bool wifi_init(void);
void wifi_connect(void);
bool wifi_is_connected(void);

bool mqtt_init(void);
void mqtt_run(void);
bool mqtt_is_connected(void);
void mqtt_reconnect(void);
void mqtt_debug_set(uint32_t lvl);

bool bt_init(void);
void bt_run(void);
void bt_debug_set(uint32_t lvl);

bool ntp_init(void);
void ntp_connect(void);
bool ntp_connected(void);

bool temperature_init(void);
void temperature_measure(void);

bool sw_out_init(void);
void sw_out_set(bool state);

void usb_debug_set(uint32_t lvl);

bool lcd_init(void);
void lcd_refresh(void);

void main_log(void);

void hlog_init(int level);
void hlog_connect(void);
void hlog_reconnect(void);
void hlog_web_enable(bool set);
void log_level_set(uint32_t level);
void log_debug_set(uint32_t dbg);

void system_log_status(void);
bool system_log_in_progress(void);

char *get_uptime(void);
uint32_t get_free_heap(void);
uint32_t get_total_heap(void);

bool webhook_init(void);
void webhook_run(void);
void webhook_reconnect(void);

bool webserv_init(void);
void webserv_run(void);
int webserv_port(void);
void webserv_reconnect(void);

bool webdebug_init(void);
void webdebug_run(void);
int webdebug_log_send(char *logbuff);

#ifdef __cplusplus
}
#endif

#endif /* _INTERNAL_COMMON_H_ */
