// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "lwip/inet.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/aon_timer.h"
#include "pico/mutex.h"

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define NTP_MODULE	"ntp"

#define TIME_LOCK(C)	do { if (C) mutex_enter_blocking(&((C)->lock)); } while (0)
#define TIME_UNLOCK(C)	do { if (C) mutex_exit(&((C)->lock)); } while (0)

struct ntp_context_t {
	sys_module_t mod;
	char *ntp_servers[SNTP_MAX_SERVERS];
	bool init;
	bool in_progress;
	absolute_time_t connect_time;
	absolute_time_t reolve_time;
	ip_addr_t server_addr;
	struct tm datetime;
	bool time_synched;
	bool time_valid;
	uint32_t debug;
	mutex_t lock;
};

static struct ntp_context_t *__ntp_context;

static struct ntp_context_t *ntp_get_context(void)
{
	return __ntp_context;
}

bool ntp_time_valid(void)
{
	struct ntp_context_t *ctx = ntp_get_context();

	if (ctx)
		return ctx->time_valid;

	return false;
}

static bool get_ntp_servers(struct ntp_context_t **ctx)
{
	char *rest;
	char *tok;
	int idx;

	(*ctx) = NULL;
	rest = USER_PRAM_GET(NTP_SERVERS);
	if (!rest)
		return false;

	(*ctx) = (struct ntp_context_t *)calloc(1, sizeof(struct ntp_context_t));
	if (!(*ctx)) {
		free(rest);
		return false;
	}

	idx = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && idx < SNTP_MAX_SERVERS)
		(*ctx)->ntp_servers[idx++] = tok;

	return true;
}

static bool sys_ntp_init(struct ntp_context_t **ctx)
{
	int i = 0;

	if (!get_ntp_servers(ctx))
		return false;

	while (i < SNTP_MAX_SERVERS && (*ctx)->ntp_servers[i])
		i++;

	if (!i) {
		free(*ctx);
		(*ctx) = NULL;
		return false;
	}

	mutex_init(&((*ctx)->lock));
	hlog_info(NTP_MODULE, "Got %d NTP servers", i);
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_servermode_dhcp(1);
	i = 0;
	while ((*ctx)->ntp_servers[i] && i < SNTP_MAX_SERVERS) {
		sntp_setservername(i, (*ctx)->ntp_servers[i]);
		hlog_info(NTP_MODULE, "  [%s]", (*ctx)->ntp_servers[i]);
		i++;
	}
	__ntp_context = (*ctx);
	return true;
}

static void sys_ntp_reconnect(void *context)
{
	struct ntp_context_t *ctx = (struct ntp_context_t *)context;

	LWIP_LOCK_START;
		sntp_stop();
	LWIP_LOCK_END;
	ctx->init = false;
	ctx->time_synched = false;
	ctx->time_valid = false;
}

static void sys_ntp_connect(void *context)
{
	struct ntp_context_t *ctx = (struct ntp_context_t *)context;
	static char buff[64];

	if (ctx->init) {
		TIME_LOCK(ctx);
			if (ctx->time_synched) {
				ctx->time_synched = false;
				ctx->time_valid = true;
				time_to_str(buff, 64, &ctx->datetime);
				hlog_info(NTP_MODULE, "Time synched to [%s] UTC", buff);
				sys_state_log_status();
			}
		TIME_UNLOCK(ctx);
		return;
	}
	if (!wifi_is_connected())
		return;
	LWIP_LOCK_START;
		sntp_init();
	LWIP_LOCK_END;
	ctx->init = true;
}

void herak_set_system_time(uint32_t sec)
{
	struct ntp_context_t *ctx  = ntp_get_context();
	time_t epoch = sec;

	if (!ctx)
		return;

	gmtime_r(&epoch, &ctx->datetime);

	/* Set time in UTC */
	SYS_LOCK_START;
		aon_timer_set_time_calendar(&(ctx->datetime));
	SYS_LOCK_END;

	TIME_LOCK(ctx);
		ctx->time_synched = true;
	TIME_UNLOCK(ctx);
}

static void sys_ntp_debug_set(uint32_t lvl, void *context)
{
	struct ntp_context_t *ctx = (struct ntp_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_ntp_log_status(void *context)
{
	struct ntp_context_t  *ctx = (struct ntp_context_t *)context;
	const ip_addr_t *addr;
	const char *name;
	int i;

	UNUSED(ctx);

	if (sntp_enabled())
		hlog_info(NTP_MODULE, "Enabled in %s mode, servers:",
			  sntp_getoperatingmode() == 0 ? "poll" : "listen only");
	else
		hlog_info(NTP_MODULE, "Disabled, servers:");
	for (i = 0; i < SNTP_MAX_SERVERS; i++) {
		name = sntp_getservername(i);
		if (!name)
			continue;
		addr = sntp_getserver(i);
		hlog_info(NTP_MODULE, "\t%s (%d.%d.%d.%d), reachability 0x%X",
				  name, ip4_addr1(addr), ip4_addr2(addr),
				  ip4_addr3(addr), ip4_addr4(addr), sntp_getreachability(i));
	}

	return true;
}

void sys_ntp_register(void)
{
	struct ntp_context_t  *ctx = NULL;

	if (!sys_ntp_init(&ctx))
		return;

	ctx->mod.name = NTP_MODULE;
	ctx->mod.run = sys_ntp_connect;
	ctx->mod.reconnect = sys_ntp_reconnect;
	ctx->mod.log = sys_ntp_log_status;
	ctx->mod.debug = sys_ntp_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
