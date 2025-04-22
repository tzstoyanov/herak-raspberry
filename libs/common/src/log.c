// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/rtc.h"
#include "lwip/inet.h"
#include "lwip/udp.h"
#include "lwip/dns.h"

#include "base64.h"
#include "params.h"

#define MAX_LOG_SIZE	512
#define LLOG	"log"

#define IS_DEBUG	(log_context.debug != 0)

#define RLOG_DEFULT_PORT	514

#define FACILITY 1

#define LOG_LOCK	mutex_enter_blocking(&log_context.lock)
#define LOG_UNLOCK	mutex_exit(&log_context.lock)

#define IP_TIMEOUT_MS	10000

static struct {
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
} log_context;

static void log_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	UNUSED(hostname);
	UNUSED(arg);
	LOG_LOCK;
		memcpy(&(log_context.server_addr), ipaddr, sizeof(ip_addr_t));
		log_context.sever_ip_state = IP_RESOLVED;
		log_context.connect_count++;
	LOG_UNLOCK;
}

bool hlog_remoute(void)
{
	bool res;

	LOG_LOCK;
		res = (log_context.sever_ip_state == IP_RESOLVED);
	LOG_UNLOCK;

	return res;
}

void hlog_web_enable(bool set)
{
	LOG_LOCK;
	log_context.http_log = set;
	LOG_UNLOCK;
}

static void hlog_status(void *context)
{
	ip_resolve_state_t sever_ip_state;
	ip_addr_t server_addr;
	int dcount;

	UNUSED(context);

	if (!log_context.server_url) {
		hlog_info(LLOG, "Logs are not forwarded to an external server");
		return;
	}

	LOG_LOCK;
		memcpy(&server_addr, &(log_context.server_addr), sizeof(ip_addr_t));
		sever_ip_state = log_context.sever_ip_state;
		dcount = log_context.connect_count;
	LOG_UNLOCK;

	switch (sever_ip_state) {
	case IP_NOT_RESOLEVED:
		hlog_info(LLOG, "Not connected to server %s, connect count %d", log_context.server_url, dcount);
		break;
	case IP_RESOLVING:
		hlog_info(LLOG, "Resolving %s ... connect count %d", log_context.server_url, dcount);
		break;
	case IP_RESOLVED:
		hlog_info(LLOG, "Forwarding logs to %s (%s), connect count %d",
				  log_context.server_url, inet_ntoa(server_addr), dcount);
		break;
	}
}

void hlog_init(int level)
{
	char *str, *rest, *tok;

	memset(&log_context, 0, sizeof(log_context));
	mutex_init(&log_context.lock);
	log_context.http_log = false;

	if (SYSLOG_SERVER_ENDPOINT_len > 0) {
		str = param_get(SYSLOG_SERVER_ENDPOINT);
		rest = str;
		tok = strtok_r(rest, ":", &rest);
		log_context.server_url = strdup(tok);
		if (rest)
			log_context.server_port = atoi(rest);
		else
			log_context.server_port = RLOG_DEFULT_PORT;
		free(str);
	}

	add_status_callback(hlog_status, NULL);

	log_context.hostname = param_get(DEV_HOSTNAME);
	log_context.log_level = level;

	printf("\r\n\r\n");
}

void hlog_reconnect(void)
{
	LOG_LOCK;
		log_context.sever_ip_state = IP_NOT_RESOLEVED;
		if (log_context.log_pcb) {
			LWIP_LOCK_START
				udp_remove(log_context.log_pcb);
			LWIP_LOCK_END
			log_context.log_pcb = NULL;
		}
	LOG_UNLOCK;
	if (IS_DEBUG)
		hlog_info(LLOG, "Log server reconnect");
}

void hlog_connect(void)
{
	bool resolving = false;
	bool connected = false;
	uint32_t now;
	int res;

	if (!log_context.server_url || !wifi_is_connected())
		return;

	LOG_LOCK;
		if (log_context.sever_ip_state == IP_RESOLVED)
			goto out;

		if (IS_DEBUG)
			hlog_info(LLOG, "Log server connect");

		now = to_ms_since_boot(get_absolute_time());
		if (!log_context.log_pcb) {
			LWIP_LOCK_START;
				log_context.log_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
			LWIP_LOCK_END;
		}
		if (!log_context.log_pcb)
			goto out;

		switch (log_context.sever_ip_state) {
		case IP_NOT_RESOLEVED:
			LOG_UNLOCK;
			LWIP_LOCK_START;
				res = dns_gethostbyname(log_context.server_url, &log_context.server_addr, log_server_found, NULL);
			LWIP_LOCK_END;
			LOG_LOCK;
			if (res == ERR_INPROGRESS) {
				log_context.sever_ip_state = IP_RESOLVING;
				log_context.last_send = to_ms_since_boot(get_absolute_time());
				resolving = true;
			} else if (res == ERR_OK) {
				log_context.sever_ip_state = IP_RESOLVED;
				log_context.connect_count++;
				connected = true;
				if (IS_DEBUG)
					hlog_info(LLOG, "Resolved %s", log_context.server_url);
			}
			break;
		case IP_RESOLVED:
			connected = true;
			break;
		case IP_RESOLVING:
			if ((now - log_context.last_send) > IP_TIMEOUT_MS) {
				log_context.sever_ip_state = IP_NOT_RESOLEVED;
				if (IS_DEBUG)
					hlog_info(LLOG, "Resolving %s timeout", log_context.server_url);
			}
			break;
		default:
			break;
		}
out:
	LOG_UNLOCK;
	if (resolving)
		hlog_info(LLOG, "Resolving %s ...", log_context.server_url);
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

static void slog_send(char *log_buff)
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
		err = udp_sendto(log_context.log_pcb, p, &log_context.server_addr, log_context.server_port);
		pbuf_free(p);
	LWIP_LOCK_END;
	if (err != ERR_OK && err != ERR_MEM)
		log_context.sever_ip_state = IP_NOT_RESOLEVED;
	else
		log_context.last_send = to_ms_since_boot(get_absolute_time());
}

void hlog_any(int severity, const char *topic, const char *fmt, ...)
{
	static char log_buff[MAX_LOG_SIZE];
	static char time_buff[32];
	int psize = MAX_LOG_SIZE;
	int  len, p = 0;
	va_list ap;

	if (log_context.log_level < severity)
		return;

	LOG_LOCK;
		LBUFF_PRINT("<%d>", FACILITY*8 + severity);
		LBUFF_PRINT("%s ", get_current_time_str(time_buff, 32));
		LBUFF_PRINT("%s ", log_context.hostname);
		LBUFF_PRINT("%s: ", topic);

		va_start(ap, fmt);
		LBUFF_VPRINT(fmt, ap);
		va_end(ap);
		LBUFF_PRINT("\r\n");

		/* Console */
		printf("%s", log_buff);
		/* rsyslog server */
		if (log_context.sever_ip_state == IP_RESOLVED)
			slog_send(log_buff);
		/* http */
		if (log_context.http_log) {
			if (webdebug_log_send(log_buff) < 0)
				log_context.http_log = false;
		}
	LOG_UNLOCK;
}

void log_debug_set(uint32_t dbg)
{
	log_context.debug = dbg;
}

void log_level_set(uint32_t level)
{
	log_context.log_level = level;
}
