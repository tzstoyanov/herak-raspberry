// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/rtc.h"
#include "lwip/inet.h"
#include "lwip/udp.h"
#include "lwip/dns.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define MAX_LOG_SIZE	512
#define LOG_MODULE		"log"

#define IS_DEBUG(C)	(!(C) || ((C) && (C)->debug != 0))

#define RLOG_DEFULT_PORT	514

#define FACILITY 1
#define LOG_LOCK(C)		do { if ((C)) mutex_enter_blocking(&((C)->lock)); } while (0)
#define LOG_UNLOCK(C)	do { if ((C)) mutex_exit(&((C)->lock)); } while (0)


#define IP_TIMEOUT_MS	10000

struct log_context_t {
	sys_module_t mod;
	char *server_url;
	int server_port;
	ip_addr_t server_addr;
	bool	http_log;
	uint32_t connect_count;
	uint32_t last_send;
	ip_resolve_state_t sever_ip_state;
	struct udp_pcb *log_pcb;
	char *hostname;
	int log_level;
	mutex_t lock;
	uint32_t debug;
};

static struct log_context_t *__log_context;

static struct log_context_t *log_context_get(void)
{
	return __log_context;
}

static void log_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	struct log_context_t *ctx = (struct log_context_t *)arg;

	UNUSED(hostname);
	LOG_LOCK(ctx);
		memcpy(&(ctx->server_addr), ipaddr, sizeof(ip_addr_t));
		ctx->sever_ip_state = IP_RESOLVED;
		ctx->connect_count++;
	LOG_UNLOCK(ctx);
}

bool hlog_remoute(void)
{
	struct log_context_t *ctx = log_context_get();
	bool res;

	if (!ctx)
		return false;

	LOG_LOCK(ctx);
		res = (ctx->sever_ip_state == IP_RESOLVED);
	LOG_UNLOCK(ctx);

	return res;
}

void hlog_web_enable(bool set)
{
	struct log_context_t *ctx = log_context_get();

	if (!ctx)
		return;

	LOG_LOCK(ctx);
		ctx->http_log = set;
	LOG_UNLOCK(ctx);
}

void log_level_set(uint32_t level)
{
	struct log_context_t *ctx = log_context_get();

	if (!ctx)
		return;

	LOG_LOCK(ctx);
		ctx->log_level = level;
	LOG_UNLOCK(ctx);
}

static bool sys_log_status(void *context)
{
	struct log_context_t *ctx = (struct log_context_t *)context;
	ip_resolve_state_t sever_ip_state;
	ip_addr_t server_addr;
	int dcount;

	if (!ctx->server_url) {
		hlog_info(LOG_MODULE, "Logs are not forwarded to an external server");
		return true;
	}

	LOG_LOCK(ctx);
		memcpy(&server_addr, &(ctx->server_addr), sizeof(ip_addr_t));
		sever_ip_state = ctx->sever_ip_state;
		dcount = ctx->connect_count;
	LOG_UNLOCK(ctx);

	switch (sever_ip_state) {
	case IP_NOT_RESOLEVED:
		hlog_info(LOG_MODULE, "Not connected to server %s, connect count %d", ctx->server_url, dcount);
		break;
	case IP_RESOLVING:
		hlog_info(LOG_MODULE, "Resolving %s ... connect count %d", ctx->server_url, dcount);
		break;
	case IP_RESOLVED:
		hlog_info(LOG_MODULE, "Forwarding logs to %s (%s), connect count %d",
				  ctx->server_url, inet_ntoa(server_addr), dcount);
		break;
	}

	return true;
}

static bool sys_log_init(struct log_context_t **ctx)
{
	char *str, *rest, *tok;

	(*ctx) = (struct log_context_t *)calloc(1, sizeof(struct log_context_t));
	if (!(*ctx))
		return false;

	str = USER_PRAM_GET(SYSLOG_SERVER_ENDPOINT);
	if (str) {
		rest = str;
		tok = strtok_r(rest, ":", &rest);
		(*ctx)->server_url = strdup(tok);
		if (rest)
			(*ctx)->server_port = atoi(rest);
		else
			(*ctx)->server_port = RLOG_DEFULT_PORT;
		free(str);
	}
	mutex_init(&((*ctx)->lock));
	(*ctx)->hostname = USER_PRAM_GET(DEV_HOSTNAME);
	(*ctx)->log_level = HLOG_INFO;
	(*ctx)->http_log = false;
	__log_context = (*ctx);
	return true;
}

static void sys_log_reconnect(void *context)
{
	struct log_context_t *ctx = (struct log_context_t *)context;

	LOG_LOCK(ctx);
		ctx->sever_ip_state = IP_NOT_RESOLEVED;
		if (ctx->log_pcb) {
			LWIP_LOCK_START;
				udp_remove(ctx->log_pcb);
			LWIP_LOCK_END;
			ctx->log_pcb = NULL;
		}
	LOG_UNLOCK(ctx);
	if (IS_DEBUG(ctx))
		hlog_info(LOG_MODULE, "Log server reconnect");
}

static void sys_log_connect(void *context)
{
	struct log_context_t *ctx = (struct log_context_t *)context;
	bool resolving = false;
	bool connected = false;
	uint32_t now;
	int res;

	if (!ctx->server_url || !wifi_is_connected())
		return;

	LOG_LOCK(ctx);
		if (ctx->sever_ip_state == IP_RESOLVED)
			goto out;

		if (IS_DEBUG(ctx))
			hlog_info(LOG_MODULE, "Log server connect");

		now = to_ms_since_boot(get_absolute_time());
		if (!ctx->log_pcb) {
			LWIP_LOCK_START;
				ctx->log_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
			LWIP_LOCK_END;
		}
		if (!ctx->log_pcb)
			goto out;

		switch (ctx->sever_ip_state) {
		case IP_NOT_RESOLEVED:
			LOG_UNLOCK(ctx);
			LWIP_LOCK_START;
				res = dns_gethostbyname(ctx->server_url, &ctx->server_addr, log_server_found, ctx);
			LWIP_LOCK_END;
			LOG_LOCK(ctx);
			if (res == ERR_INPROGRESS) {
				ctx->sever_ip_state = IP_RESOLVING;
				ctx->last_send = to_ms_since_boot(get_absolute_time());
				resolving = true;
			} else if (res == ERR_OK) {
				ctx->sever_ip_state = IP_RESOLVED;
				ctx->connect_count++;
				connected = true;
				if (IS_DEBUG(ctx))
					hlog_info(LOG_MODULE, "Resolved %s", ctx->server_url);
			}
			break;
		case IP_RESOLVED:
			connected = true;
			break;
		case IP_RESOLVING:
			if ((now - ctx->last_send) > IP_TIMEOUT_MS) {
				ctx->sever_ip_state = IP_NOT_RESOLEVED;
				if (IS_DEBUG(ctx))
					hlog_info(LOG_MODULE, "Resolving %s timeout", ctx->server_url);
			}
			break;
		default:
			break;
		}
out:
	LOG_UNLOCK(ctx);
	if (resolving)
		hlog_info(LOG_MODULE, "Resolving %s ...", ctx->server_url);
	if (connected)
		system_log_status();
}

#define LBUFF_PRINT(A...) {\
	if (psize > 0) {\
		len = snprintf(log_buff+p, psize, A); \
		if (len > 0) { \
			psize -= len; p += len; \
		} \
	}}
#define LBUFF_VPRINT(F, A) {\
	if (psize > 0) {\
		len = vsnprintf(log_buff+p, psize, F, A); \
		if (len > 0) {\
			psize -= len; p += len;\
		} \
	}}

static void slog_send(struct log_context_t *ctx, char *log_buff)
{
	int len = strlen(log_buff)+1;
	struct pbuf *p;
	err_t err;

	LWIP_LOCK_START;
		p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	LWIP_LOCK_END;
	if (!p)
		return;

	memcpy(p->payload, log_buff, len);
	LWIP_LOCK_START;
		err = udp_sendto(ctx->log_pcb, p, &ctx->server_addr, ctx->server_port);
		pbuf_free(p);
	LWIP_LOCK_END;
	if (err != ERR_OK && err != ERR_MEM)
		ctx->sever_ip_state = IP_NOT_RESOLEVED;
	else
		ctx->last_send = to_ms_since_boot(get_absolute_time());
}

void hlog_any(int severity, const char *topic, const char *fmt, ...)
{
	struct log_context_t *ctx = log_context_get();
	static char log_buff[MAX_LOG_SIZE];
	static char time_buff[32];
	int psize = MAX_LOG_SIZE;
	int  len, p = 0;
	va_list ap;

	if (severity < 0)
		severity = HLOG_INFO;
	if (!topic)
		topic = "system";

	if (ctx && ctx->log_level < severity)
		return;

	LOG_LOCK(ctx);
		LBUFF_PRINT("<%d>", FACILITY*8 + severity);
		LBUFF_PRINT("%s ", get_current_time_str(time_buff, 32));
		LBUFF_PRINT("%s ", ctx ? ctx->hostname : "pico");
		LBUFF_PRINT("%s: ", topic);

		va_start(ap, fmt);
		LBUFF_VPRINT(fmt, ap);
		va_end(ap);
		LBUFF_PRINT("\r\n");

		/* Console */
		printf("%s", log_buff);
		/* rsyslog server */
		if (ctx && ctx->sever_ip_state == IP_RESOLVED)
			slog_send(ctx, log_buff);
		/* http */
#ifdef HAVE_SYS_COMMANDS
		if (ctx && ctx->http_log) {
			if (syscmd_log_send(log_buff) < 0)
				ctx->http_log = false;
		}
#endif /* HAVE_SYS_COMMANDS */
	LOG_UNLOCK(ctx);
}

static void sys_log_debug_set(uint32_t lvl, void *context)
{
	struct log_context_t *ctx = (struct log_context_t *)context;

	ctx->debug = lvl;
}

void sys_log_register(void)
{
	struct log_context_t  *ctx = NULL;

	if (!sys_log_init(&ctx))
		return;

	ctx->mod.name = LOG_MODULE;
	ctx->mod.run = sys_log_connect;
	ctx->mod.reconnect = sys_log_reconnect;
	ctx->mod.log = sys_log_status;
	ctx->mod.debug = sys_log_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
