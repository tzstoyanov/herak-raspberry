// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "lwip/inet.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/mutex.h"

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define SYS_STAT_MODULE	"sys_state"

#define LOG_STATUS_HOOKS_COUNT	128
#define PERIODIC_LOG_MS	(60*60*1000) // 1 hour default

#define LOG_STATUS_DELAY_MS	100

#define TIME_STR	64
#define MQTT_COUNT	2
#define MQTT_DATA_LEN		512

typedef struct {
	log_status_cb_t hook;
	void *user_context;
} log_status_hook_t;

struct sys_state_context_t {
	sys_module_t mod;
	uint32_t periodic_log_ms;
	uint64_t last_log;
	uint64_t last_run;
	uint32_t debug;

	log_status_hook_t log_status[LOG_STATUS_HOOKS_COUNT];
	uint8_t log_status_count;
	int log_status_progress;

	mqtt_component_t mqtt_comp[MQTT_COUNT];
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static struct sys_state_context_t *__sys_state_context;

static struct sys_state_context_t *sys_state_get_context(void)
{
	return __sys_state_context;
}

static void sys_state_mqtt_init(struct sys_state_context_t *ctx)
{
	/* Device uptime */
	ctx->mqtt_comp[0].module = SYS_STAT_MODULE;
	ctx->mqtt_comp[0].platform = "sensor";
	ctx->mqtt_comp[0].value_template = "{{ value_json['sys_uptime'] }}";
	ctx->mqtt_comp[0].name = "sys_uptime";
	mqtt_msg_component_register(&(ctx->mqtt_comp[0]));

	/* Device error state */
	ctx->mqtt_comp[1].module = SYS_STAT_MODULE;
	ctx->mqtt_comp[1].platform = "binary_sensor";
	ctx->mqtt_comp[1].payload_on = "1";
	ctx->mqtt_comp[1].payload_off = "0";
	ctx->mqtt_comp[1].value_template = "{{ value_json['sys_error'] }}";
	ctx->mqtt_comp[1].name = "sys_error";
	ctx->mqtt_comp[1].state_topic = ctx->mqtt_comp[0].state_topic;
	mqtt_msg_component_register(&(ctx->mqtt_comp[1]));
}

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int sys_state_mqtt_send(struct sys_state_context_t *ctx)
{
	char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	int count = 0;
	int ret = -1;

	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\":\"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"sys_uptime\": \"%s\"", get_uptime());
		ADD_MQTT_MSG_VAR(",\"sys_error\": \"%d\"", !sys_state_is_healthy());
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&(ctx->mqtt_comp[0]), ctx->mqtt_payload);

	return ret;
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
	sys_state_mqtt_init(*ctx);

	return true;
}

static void sys_state_debug_set(uint32_t lvl, void *context)
{
	struct sys_state_context_t *ctx = (struct sys_state_context_t *)context;

	ctx->debug = lvl;
}

static bool sys_state_log(void *context)
{
	struct sys_state_context_t  *ctx = (struct sys_state_context_t *)context;

	if (ctx->periodic_log_ms)
		hlog_info(SYS_STAT_MODULE, "Periodic system log is enabled on %d sec", ctx->periodic_log_ms / 1000);
	else
		hlog_info(SYS_STAT_MODULE, "Periodic system log is disabled");

	return true;
}

static void sys_state_log_start(struct sys_state_context_t  *ctx)
{
	hlog_info(SYS_STAT_MODULE, "----------- Status -----------");
	hlog_info(SYS_STAT_MODULE, "Uptime: %s; free RAM: %d bytes; chip temperature: %3.2f *C",
			  get_uptime(), get_free_heap(),
#ifdef HAVE_TEMPERATURE
			  temperature_internal_get()
#else
			  0
#endif
			  );
	sys_state_log_version();
	sys_state_log_resources();
	sys_modules_log();
	ctx->log_status_progress = 0;
}

static bool sys_state_periodic(struct sys_state_context_t  *ctx)
{
	uint64_t now;

	if (ctx->periodic_log_ms <= 0)
		return false;

	now = time_ms_since_boot();
	if ((now - ctx->last_log) > ctx->periodic_log_ms) {
		ctx->last_log = now;
		sys_state_log_start(ctx);
		return true;
	}

	return false;
}

static void sys_state_log_run(void *context)
{
	struct sys_state_context_t  *ctx = (struct sys_state_context_t *)context;
	uint64_t now = time_ms_since_boot();
	int idx = ctx->log_status_progress;
	bool ret = true;

	if (idx < 0 || idx >= ctx->log_status_count) {
		sys_state_periodic(ctx);
		if (ctx->mqtt_comp[0].force)
			sys_state_mqtt_send(ctx);
		return;
	}

	if (now - ctx->last_run < LOG_STATUS_DELAY_MS)
		return;

	if (ctx->log_status[idx].hook)
		ret = ctx->log_status[idx].hook(ctx->log_status[idx].user_context);

	if (ret)
		ctx->log_status_progress++;
	if (ctx->log_status_progress >= ctx->log_status_count) {
		hlog_info(SYS_STAT_MODULE, "----------- Status end--------");
		ctx->last_log = time_ms_since_boot();
		ctx->log_status_progress = -1;
		ctx->mqtt_comp[0].force = true;
		sys_state_mqtt_send(ctx);
	}

	ctx->last_run = now;
}

void sys_state_register(void)
{
	struct sys_state_context_t  *ctx = NULL;

	if (!syslog_init(&ctx))
		return;

	ctx->mod.name = SYS_STAT_MODULE;
	ctx->mod.run = sys_state_log_run;
	ctx->mod.log = sys_state_log;
	ctx->mod.debug = sys_state_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

/* API */
int sys_state_callback_add(log_status_cb_t cb, void *user_context)
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

void sys_state_log_status(void)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return;

	if (ctx->log_status_progress >= 0 && ctx->log_status_progress < ctx->log_status_count)
		return;

	sys_state_log_start(ctx);
}

void sys_state_set_periodic_log_ms(int ms)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return;

	if (ms >= 0)
		ctx->periodic_log_ms = ms;
	else
		ctx->periodic_log_ms = PERIODIC_LOG_MS;
}

bool sys_state_log_in_progress(void)
{
	struct sys_state_context_t *ctx = sys_state_get_context();

	if (!ctx)
		return false;

	if (ctx->log_status_progress >= 0 && ctx->log_status_progress < ctx->log_status_count)
		return true;

	return false;
}

#define LOG_MEM_STAT(M)	\
		hlog_info(SYS_STAT_MODULE, "\tmem [%s]: err %d, used %d / %d, max %d, illegal %d",	\
				(M)->name, (M)->err, (M)->used, (M)->avail, (M)->max, (M)->illegal)

#define LOG_SYS_STAT(N, M)	\
		hlog_info(SYS_STAT_MODULE, "\tsys [%s]: err %d, used %d / %d",	\
				(N), (M)->err, (M)->used, (M)->max)

#define LOG_SYS_PROTO(N, M)	do {\
		hlog_info(SYS_STAT_MODULE, "\tnet [%s]: err %d, rcv %d, xmit %d, fwd %d, drop %d, cachehit %d",	\
				(N), (M)->err, (M)->recv, (M)->xmit, (M)->fw, (M)->drop, (M)->cachehit); \
		hlog_info(SYS_STAT_MODULE, "\t\tchkerr %d, lenerr %d, memerr %d, proterr %d, rterr %d, opterr %d",	\
				(M)->chkerr, (M)->lenerr, (M)->xmit, (M)->memerr, (M)->proterr, (M)->rterr, (M)->opterr); \
	} while (0)

bool sys_state_is_healthy(void)
{
	int errs = 0;

#if MEM_STATS
	if (lwip_stats.mem.err)
		errs++;
	for (int i = 0; i < MEMP_MAX; i++) {
		if (lwip_stats.memp[i]->err)
			errs++;
	}
#endif

#if SYS_STATS
	if (lwip_stats.sys.mbox.err)
		errs++;
	if (lwip_stats.sys.mutex.err)
		errs++;
	if (lwip_stats.sys.sem.err)
		errs++;
#endif

#if TCP_STATS
	if (lwip_stats.tcp.err)
		errs++;
#endif

#if UDP_STATS
	if (lwip_stats.udp.err)
		errs++;
#endif

#if ICMP_STATS
	if (lwip_stats.icmp.err)
		errs++;
#endif

#if IP_STATS
	if (lwip_stats.ip.err)
		errs++;
#endif

#if IPFRAG_STATS
	if (lwip_stats.ip_frag.err)
		errs++;
#endif

#if ETHARP_STATS
	if (lwip_stats.etharp.err)
		errs++;
#endif

#if LINK_STATS
	if (lwip_stats.link.err)
		errs++;
#endif

	return !errs;
}

void sys_state_log_version(void)
{
	hlog_info(SYS_STAT_MODULE, "Image %s %s compiled %s, running on %s",
			  IMAGE_NAME, SYS_VERSION_STR, SYS_BUILD_DATE, DEV_ARCH);
}

void sys_state_log_resources(void)
{
	if (sys_state_is_healthy())
		hlog_info(SYS_STAT_MODULE, "System is healthy, no errors detected.");
	else
		hlog_info(SYS_STAT_MODULE, "System errors detected!");

#if MEM_STATS
	LOG_MEM_STAT(&lwip_stats.mem);
	for (int i = 0; i < MEMP_MAX; i++) {
		LOG_MEM_STAT(lwip_stats.memp[i]);
	}
#endif

#if SYS_STATS
	LOG_SYS_STAT("mbox", &lwip_stats.sys.mbox);
	LOG_SYS_STAT("mutex", &lwip_stats.sys.mutex);
	LOG_SYS_STAT("sem", &lwip_stats.sys.sem);
#endif

#if TCP_STATS
	LOG_SYS_PROTO("TCP", &lwip_stats.tcp);
#endif

#if UDP_STATS
	LOG_SYS_PROTO("UCP", &lwip_stats.udp);
#endif

#if ICMP_STATS
	LOG_SYS_PROTO("ICMP", &lwip_stats.icmp);
#endif

#if IP_STATS
	LOG_SYS_PROTO("IP", &lwip_stats.ip);
#endif

#if IPFRAG_STATS
	LOG_SYS_PROTO("IPfrag", &lwip_stats.ip_frag);
#endif

#if ETHARP_STATS
	LOG_SYS_PROTO("EthArp", &lwip_stats.etharp);
#endif

#if LINK_STATS
	LOG_SYS_PROTO("Link", &lwip_stats.link);
#endif
}
