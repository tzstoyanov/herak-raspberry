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

#define RLOG_DEFULT_PORT	514

#define FACILITY 1

struct {
	char *server_url;
	int server_port;
	bool resolved;
	ip_addr_t server_addr;
	ip_resolve_state_t sever_ip_state;
	struct udp_pcb *log_pcb;
	char *hostname;
	int log_level;
	mutex_t lock;
} static log_context;

static void log_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	mutex_enter_blocking(&log_context.lock);
		memcpy(&(log_context.server_addr), ipaddr, sizeof(ip_addr_t));
		log_context.sever_ip_state = IP_RESOLVED;
	mutex_exit(&log_context.lock);
}

bool hlog_remoute(void)
{
	bool res;

	mutex_enter_blocking(&log_context.lock);
		res = (log_context.sever_ip_state == IP_RESOLVED);
	mutex_exit(&log_context.lock);

	return res;
}

void hlog_init(int level)
{
	char *str, *rest, *tok;

	memset(&log_context, 0, sizeof(log_context));
	mutex_init(&log_context.lock);

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

	log_context.hostname = param_get(DEV_HOSTNAME);
	log_context.log_level = level;

	printf("\n\n\r");
}

void hlog_status(void)
{
	ip_resolve_state_t sever_ip_state;
	ip_addr_t server_addr;

	if (!log_context.server_url) {
		hlog_info(LLOG, "Logs are not forwarded to an external server");
		return;
	}

	mutex_enter_blocking(&log_context.lock);
		memcpy(&server_addr, &(log_context.server_addr), sizeof(ip_addr_t));
		sever_ip_state = log_context.sever_ip_state;
	mutex_exit(&log_context.lock);

	switch (sever_ip_state) {
	case IP_NOT_RESOLEVED:
		hlog_info(LLOG, "Not connected to server %s", log_context.server_url);
		break;
	case IP_RESOLVING:
		hlog_info(LLOG, "Resolving %s ... ", log_context.server_url);
		break;
	case IP_RESOLVED:
		hlog_info(LLOG, "Forwarding logs to %s (%s)", log_context.server_url, inet_ntoa(server_addr));
		break;
	}
}

void hlog_connect(void)
{
	bool resolving = false;
	bool conncted = false;
	int res;

	if (!log_context.server_url || !wifi_is_connected())
		return;

	mutex_enter_blocking(&log_context.lock);
		if (log_context.sever_ip_state == IP_RESOLVED)
			goto out;

		if (!log_context.log_pcb) {
			LWIP_LOCK_START;
				log_context.log_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
			LWIP_LOCK_END;
		}
		if (!log_context.log_pcb)
			goto out;

		switch (log_context.sever_ip_state) {
		case IP_NOT_RESOLEVED:
			mutex_exit(&log_context.lock);
				res = dns_gethostbyname(log_context.server_url, &log_context.server_addr, log_server_found, NULL);
			mutex_enter_blocking(&log_context.lock);
			if (res != ERR_OK) {
				log_context.sever_ip_state = IP_RESOLVING;
				resolving = true;
				goto out;
			} else {
				log_context.sever_ip_state = IP_RESOLVED;
			}
			break;
		case IP_RESOLVED:
			conncted = true;
			break;
		case IP_RESOLVING:
			goto out;
		default:
			goto out;
		}
out:
	mutex_exit(&log_context.lock);
	if (resolving)
		hlog_info(LLOG, "Resolving %s ...", log_context.server_url);
	if (conncted)
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
	char *data;
	err_t err;

	LWIP_LOCK_START;
		p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	LWIP_LOCK_END;
	if (!p)
		return;

	memcpy(p->payload, log_buff, len);
	LWIP_LOCK_START;
		err = udp_sendto(log_context.log_pcb, p, &log_context.server_addr, log_context.server_port);
	LWIP_LOCK_END;
	pbuf_free(p);
	if (err != ERR_OK && err != ERR_MEM) {
		LWIP_LOCK_START;
			udp_remove(log_context.log_pcb);
		LWIP_LOCK_END;
		log_context.log_pcb = NULL;
		log_context.sever_ip_state = IP_NOT_RESOLEVED;
	}
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

	mutex_enter_blocking(&log_context.lock);
		LBUFF_PRINT("<%d>", FACILITY*8 + severity);
		LBUFF_PRINT("%s ", get_current_time_str(time_buff, 32));
		LBUFF_PRINT("%s ", log_context.hostname);
		LBUFF_PRINT("%s: ", topic);

		va_start(ap, fmt);
		LBUFF_VPRINT(fmt, ap);
		va_end(ap);

		printf("%s\n\r", log_buff);

		if (log_context.sever_ip_state == IP_RESOLVED)
			slog_send(log_buff);
	mutex_exit(&log_context.lock);
}
