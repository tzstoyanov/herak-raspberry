// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _INTERNAL_COMMON_H_
#define _INTERNAL_COMMON_H_

#include "pico/util/datetime.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "herak_sys.h"
#include "lwip/sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_USER_AGENT		"PicoW"

//#define LWIP_LOCK_START	cyw43_arch_lwip_begin();
//#define LWIP_LOCK_END	cyw43_arch_lwip_end();

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

#ifndef __weak
#define __weak	__attribute__((__weak__))
#endif

//#define FUNC_TIME_LOG_THRESHOLD_US	1000000 // log more than 1sec
#ifdef FUNC_TIME_LOG_THRESHOLD_US
#define LOOP_FUNC_RUN(N, F, args...) {\
	uint64_t __end__, __start__ = to_us_since_boot(get_absolute_time());\
		if (FUNC_TIME_LOG_THRESHOLD_US == 0)\
			printf("\n\r Enter [%s]", N);\
		F(args);\
		__end__ = to_us_since_boot(get_absolute_time()); \
		wd_update();\
		if ((__end__ - __start__) >= FUNC_TIME_LOG_THRESHOLD_US)\
			printf(" [%s] took %lld usec\n\r", N, __end__ - __start__);\
	}
#define LOOP_RET_FUNC_RUN(N, R, F, args...) {\
	uint64_t __end__, __start__ = to_us_since_boot(get_absolute_time());\
		if (FUNC_TIME_LOG_THRESHOLD_US == 0)\
			printf("\n\r Enter [%s]", N);\
		(R) = F(args);\
		__end__ = to_us_since_boot(get_absolute_time()); \
		wd_update();\
		if ((__end__ - __start__) >= FUNC_TIME_LOG_THRESHOLD_US)\
			printf("\n\r\t--->[%s] took %lld usec\n\r", N, __end__ - __start__);\
	}
#else
#define LOOP_FUNC_RUN(N, F, args...) { F(args); wd_update(); }
#define LOOP_RET_FUNC_RUN(N, F, args...) { (R) = F(args); wd_update(); }
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
void sys_modules_debug_set(int debug);

void system_reconnect(void);
void system_set_periodic_log_ms(uint32_t ms);

typedef enum {
	IP_NOT_RESOLEVED = 0,
	IP_RESOLVING,
	IP_RESOLVED
} ip_resolve_state_t;

void log_sys_health(void);

void system_log_status(void);
bool system_log_in_progress(void);

char *get_uptime(void);
uint32_t get_free_heap(void);
uint32_t get_total_heap(void);

#ifdef __cplusplus
}
#endif

#endif /* _INTERNAL_COMMON_H_ */
