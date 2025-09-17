// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "hardware/rtc.h"
#include "lwip/inet.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/util/datetime.h"
#include "pico/mutex.h"

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define SYS_STAT_MODULE	"sys_state"

#define LOG_STATUS_HOOKS_COUNT	128
#define PERIODIC_LOG_MS	(60*60*1000) // 1 hour default

typedef struct {
	log_status_cb_t hook;
	void *user_context;
} log_status_hook_t;

struct sys_state_context_t {
	sys_module_t mod;
	uint32_t periodic_log_ms;
	uint64_t last_log;
	uint32_t debug;

	log_status_hook_t log_status[LOG_STATUS_HOOKS_COUNT];
	uint8_t log_status_count;
	int log_status_progress;
};

static struct sys_state_context_t *__sys_state_context;

static struct sys_state_context_t *sys_state_get_context(void)
{
	return __sys_state_context;
}


static bool syslog_init(struct sys_state_context_t **ctx)
{
	char *cfg;

	(*ctx) = calloc(1, sizeof(struct sys_state_context_t));
	if (!(*ctx))
		return false;

	cfg = USER_PRAM_GET(SYS_STATE_LOG_SEC);
	if (cfg && strlen(cfg) >= 1) {
		(*ctx)->periodic_log_ms = (uint16_t)(strtol(cfg, NULL, 0));
		(*ctx)->periodic_log_ms *= 1000;
	} else {
		(*ctx)->periodic_log_ms = PERIODIC_LOG_MS;
	}

	if (cfg)
		free(cfg);

	(*ctx)->log_status_progress = -1;
	__sys_state_context = (*ctx);
	return true;
}

static void sys_state_debug_set(uint32_t lvl, void *context)
{
	struct sys_state_context_t *ctx = (struct sys_state_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_state_log_status(void *context)
{
	struct sys_state_context_t  *ctx = (struct sys_state_context_t *)context;

	if (ctx->periodic_log_ms)
		hlog_info(SYS_STAT_MODULE, "Periodic system log is enabled on %d sec", ctx->periodic_log_ms);
	else
		hlog_info(SYS_STAT_MODULE, "Periodic system log is disabled");

	return true;
}

static void sys_state_log_start(struct sys_state_context_t  *ctx)
{
	hlog_info(SYS_STAT_MODULE, "----------- Status -----------");
	hlog_info(SYS_STAT_MODULE, "Uptime: %s; free RAM: %d bytes; chip temperature: %3.2f *C",
			  get_uptime(), get_free_heap(),
#ifdef HAVE_CHIP_TEMP
			  temperature_internal_get()
#else
			  0
#endif
			  );
	log_sys_health();
	sys_modules_log();
	ctx->log_status_progress = 0;
}

static void sys_state_periodic(struct sys_state_context_t  *ctx)
{
	uint64_t now;

	if (ctx->periodic_log_ms <= 0)
		return;

	now = time_ms_since_boot();
	if ((now - ctx->last_log) > ctx->periodic_log_ms) {
		ctx->last_log = now;
		sys_state_log_start(ctx);
	}
}

static void sys_state_log_run(void *context)
{
	struct sys_state_context_t  *ctx = (struct sys_state_context_t *)context;
	int idx = ctx->log_status_progress;
	bool ret = true;

	if (idx < 0 || idx >= ctx->log_status_count) {
		sys_state_periodic(ctx);
		return;
	}

	if (ctx->log_status[idx].hook)
		ret = ctx->log_status[idx].hook(ctx->log_status[idx].user_context);

	if (ret)
		ctx->log_status_progress++;
	if (ctx->log_status_progress >= ctx->log_status_count) {
		hlog_info(SYS_STAT_MODULE, "----------- Status end--------");
		ctx->last_log = time_ms_since_boot();
		ctx->log_status_progress = -1;
	}
}

void sys_state_register(void)
{
	struct sys_state_context_t  *ctx = NULL;

	if (!syslog_init(&ctx))
		return;

	ctx->mod.name = SYS_STAT_MODULE;
	ctx->mod.run = sys_state_log_run;
	ctx->mod.log = sys_state_log_status;
	ctx->mod.debug = sys_state_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

/* API */
int add_status_callback(log_status_cb_t cb, void *user_context)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return -1;

	int idx = ctx->log_status_count;

	if (ctx->log_status_count >= LOG_STATUS_HOOKS_COUNT)
		return -1;

	ctx->log_status[idx].hook = cb;
	ctx->log_status[idx].user_context = user_context;
	ctx->log_status_count++;

	return idx;
}

void system_log_status(void)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return;

	if (ctx->log_status_progress >= 0 && ctx->log_status_progress < ctx->log_status_count)
		return;

	sys_state_log_start(ctx);
}

void system_set_periodic_log_ms(int ms)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return;

	if (ms >= 0)
		ctx->periodic_log_ms = ms;
	else
		ctx->periodic_log_ms = PERIODIC_LOG_MS;
}

bool system_log_in_progress(void)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return false;

	if (ctx->log_status_progress >= 0 && ctx->log_status_progress < ctx->log_status_count)
		return true;

	return false;
}
