// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
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
#include "lwip/altcp.h"

#include "base64.h"
#include "params.h"

#define WHLOG	"webhook"

//#define WH_DEBUG

#define WH_HTTP_HEAD		"%s %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Length: %d\r\n%sUser-Agent: %s\r\nContent-Type: %s\r\n\r\n"
#define MAX_HOOKS			3
#define PACKET_BUFF_SIZE	512

#define HTTP_CONNECTION_CLOSE	"Connection: close\r\n"

#define IP_TIMEOUT_MS	20000
#define WH_LOCK(W)		mutex_enter_blocking(&((W)->lock))
#define WH_UNLOCK(W)	mutex_exit(&((W)->lock))

enum tcp_state_t {
	TCP_DISCONNECTED = 0,
	TCP_CONNECTING,
	TCP_CONNECTED
};

struct webhook_t {
	int idx;
	char *addr_str;			// example.com or 192.168.1.1
	char *content_type;		// application/json
	char *endpoint;			// /api/webhook/-0vJZOQ7D9NCj3Iz3p63uSMAI
	char *http_command;		// POST
	int port;				// webhook server TCP port
	ip_addr_t addr;
	ip_resolve_state_t ip_resolve;
	uint32_t last_send;
	uint32_t conn_count;
	uint32_t send_count;
	uint32_t recv_count;
	uint32_t last_reply;
	bool sending;
	bool keep_open;
	enum tcp_state_t tcp_state;
	struct altcp_pcb *tcp_conn;
	char buff[PACKET_BUFF_SIZE];
	int buff_p;
	int buff_len;
	webhook_reply_t user_cb;
	void *user_data;
	mutex_t lock;
};

static struct {
	struct webhook_t whooks[MAX_HOOKS];
	int wh_count;
} wh_context;

int webhook_state(int idx, bool *connected, bool *sending)
{
	if (idx < 0 || idx >= wh_context.wh_count)
		return -1;

	WH_LOCK(&(wh_context.whooks[idx]));
		if (connected) {
			if (wh_context.whooks[idx].tcp_state == TCP_CONNECTED)
				*connected = true;
			else
				*connected = false;
		}
		if (sending)
			*sending = wh_context.whooks[idx].sending;
	WH_UNLOCK(&(wh_context.whooks[idx]));

	return 0;
}

int webhook_add(char *addr, int port, char *content_type, char *endpoint, char *http_command,
				bool keep_open, webhook_reply_t user_cb, void *user_data)
{
	int i;

	for (i = 0; i < MAX_HOOKS; i++) {
		if (!wh_context.whooks[i].addr_str)
			break;
	}
	if (i >= MAX_HOOKS)
		return -1;

	wh_context.whooks[i].idx = i;
	wh_context.whooks[i].addr_str = strdup(addr);
	wh_context.whooks[i].port = port;
	wh_context.whooks[i].content_type = strdup(content_type);
	wh_context.whooks[i].endpoint = strdup(endpoint);
	wh_context.whooks[i].http_command = strdup(http_command);
	wh_context.whooks[i].ip_resolve = IP_NOT_RESOLEVED;
	wh_context.whooks[i].tcp_state = TCP_DISCONNECTED;
	wh_context.whooks[i].keep_open = keep_open;
	wh_context.whooks[i].last_reply = -1;
	wh_context.whooks[i].user_cb = user_cb;
	wh_context.whooks[i].user_data = user_data;
	mutex_init(&wh_context.whooks[i].lock);
	wh_context.wh_count++;

	hlog_info(WHLOG, "New WH added %s:%d%s", addr, port, endpoint);
	return i;
}

static void wh_tcp_send(struct webhook_t *wh, struct altcp_pcb *tpcb)
{
	u16_t data_len;
	u16_t send_len;
	err_t err;

	WH_LOCK(wh);
		data_len = wh->buff_len - wh->buff_p;
		send_len = altcp_sndbuf(tpcb);

		if (!wh->sending || send_len <= 0 || data_len <= 0)
			goto out;

		/* Check if the space in TCP output buffer is larger enough for all data */
		if (send_len > data_len)
			send_len = data_len;

		err = altcp_write(tpcb, wh->buff + wh->buff_p, send_len, TCP_WRITE_FLAG_COPY);
		if (err == ERR_OK) {
			wh->buff_p += send_len;
			if (wh->buff_p >= wh->buff_len) {
				wh->sending = false;
				wh->buff_p = 0;
				wh->buff_len = 0;
				wh->send_count++;
			}
			/* Flush */
			altcp_output(tpcb);
		}
out:
	WH_UNLOCK(wh);
}

static void wh_abort(struct webhook_t *wh)
{
	WH_LOCK(wh);
		if (wh->tcp_conn) {
			LWIP_LOCK_START;
				altcp_abort(wh->tcp_conn);
			LWIP_LOCK_END;
			wh->tcp_conn = NULL;
		}
		wh->tcp_state = TCP_DISCONNECTED;
	WH_UNLOCK(wh);
}

static void webhook_disconnect(struct webhook_t *wh)
{
	if (!wh)
		return;

	WH_LOCK(wh);
		if (wh->tcp_conn) {
			altcp_recv(wh->tcp_conn, NULL);
			altcp_err(wh->tcp_conn,  NULL);
			altcp_sent(wh->tcp_conn, NULL);
			if (altcp_close(wh->tcp_conn) != ERR_OK)
				altcp_abort(wh->tcp_conn);
		}
		wh->tcp_conn = NULL;
		wh->buff_p = 0;
		wh->buff_len = 0;
		wh->tcp_state = TCP_DISCONNECTED;
		wh->ip_resolve = IP_NOT_RESOLEVED;
		if (!wh->keep_open)
			hlog_info(WHLOG, "Disconnected form %s:%d", wh->addr_str, wh->port);
	WH_UNLOCK(wh);
}

#define HTTP_REPLY_SIZE	32
static int wh_parse_http_reply(struct pbuf *p)
{
	char reply_line[HTTP_REPLY_SIZE];
	struct pbuf *bp = p;
	int http_code = -1;
	char *rest, *tok;
	int i, j = 0;
	char *data;

	while (bp) {
		data = (char *)bp->payload;
		for (i = 0; i < bp->len; i++) {
			if (data[i] == '\n' || data[i] == '\r') {
				data[i] = 0;
				break;
			}
			if (j >= (HTTP_REPLY_SIZE-1))
				break;
			reply_line[j++] = data[i];
		}
		if (i < bp->len)
			break;
		bp = bp->next;
	}

	if (bp && j < (HTTP_REPLY_SIZE-1)) {
		reply_line[j] = 0;
		rest = reply_line;
		i = 0;
		while (i < 2 && (tok = strtok_r(rest, " ", &rest))) {
			if (i == 1) {
				http_code = atoi(tok);
				break;
			}
			i++;
		}
	}

	return http_code;
}

static int wh_parse_incoming(struct webhook_t *wh, struct pbuf *p)
{
	int hcode;

	WH_LOCK(wh);
		wh->recv_count++;
	WH_UNLOCK(wh);
#ifdef WH_DEBUG
	{
		struct pbuf *bp = p;

		hlog_info(WHLOG, "Received %d bytes from %s:", p->tot_len, wh->addr_str);
		while (bp) {
			dump_char_data(WHLOG, bp->payload, bp->len);
			bp = bp->next;
		}
	}
#else
	hcode = wh_parse_http_reply(p);
	if (hcode >= 0) {
		WH_LOCK(wh);
			wh->last_reply = hcode;
			if (wh->user_cb)
				wh->user_cb(wh->idx, hcode, wh->user_data);
		WH_UNLOCK(wh);
	}
#endif

	return 0;
}

static err_t wh_tcp_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct webhook_t *wh = (struct webhook_t *)arg;
	int ret = 0;

	if (p == NULL) {
		/* remote has closed connection */
		webhook_disconnect(wh);
		return ERR_OK;
	}

	if (err != ERR_OK) {
		pbuf_free(p);
		return err;
	}

	/* Tell remote that data has been received */
	altcp_recved(pcb, p->tot_len);
	ret = wh_parse_incoming(wh, p);
	pbuf_free(p);
	if (ret)
		webhook_disconnect(wh);

	return ERR_OK;
}

static void wh_tcp_err_cb(void *arg, err_t err)
{
	struct webhook_t *wh = (struct webhook_t *)arg;

	UNUSED(err);
	WH_LOCK(wh);
		/* Set conn to null as pcb is already deallocated*/
		wh->tcp_conn = NULL;
	WH_UNLOCK(wh);
	webhook_disconnect(wh);
}

static err_t wh_tcp_connect_cb(void *arg, struct altcp_pcb *tpcb, err_t err)
{
	struct webhook_t *wh = (struct webhook_t *)arg;

	if (err != ERR_OK)
		return err;

	/* Setup TCP callbacks */
	LWIP_LOCK_START;
		altcp_recv(tpcb, wh_tcp_recv_cb);
	LWIP_LOCK_END;

	WH_LOCK(wh);
		wh->tcp_state = TCP_CONNECTED;
		wh->conn_count++;
		wh->last_send = to_ms_since_boot(get_absolute_time());
		if (!wh->keep_open)
			hlog_info(WHLOG, "Connected to %s:%d", wh->addr_str, wh->port);
	WH_UNLOCK(wh);

	return ERR_OK;
}

static void webhook_connect(struct webhook_t *wh)
{
	uint32_t last, now;
	int st_tcp, st_res;
	err_t err;

	now = to_ms_since_boot(get_absolute_time());
	WH_LOCK(wh);
		st_tcp = wh->tcp_state;
		st_res = wh->ip_resolve;
		last = wh->last_send;
	WH_UNLOCK(wh);

	if (st_res != IP_RESOLVED)
		return;

	switch (st_tcp) {
	case TCP_DISCONNECTED:
		wh_abort(wh);
		WH_LOCK(wh);
			LWIP_LOCK_START;
				wh->tcp_conn = altcp_tcp_new_ip_type(IP_GET_TYPE(&(wh->addr)));
				if (wh->tcp_conn) {
					altcp_arg(wh->tcp_conn, wh);
					err = altcp_bind(wh->tcp_conn, IP_ADDR_ANY, 0);
					if (err == ERR_OK)
						err = altcp_connect(wh->tcp_conn, &(wh->addr),
											wh->port, wh_tcp_connect_cb);
					if (err == ERR_OK) {
						altcp_err(wh->tcp_conn, wh_tcp_err_cb);
						wh->tcp_state = TCP_CONNECTING;
						wh->last_send = now;
					}
				}
			LWIP_LOCK_END;
		WH_UNLOCK(wh);
		break;
	case TCP_CONNECTING:
		if ((now - last) > IP_TIMEOUT_MS)
			wh_abort(wh);
		break;
	case TCP_CONNECTED:
	default:
		break;
	}
}

int webhook_send(int idx, char *data, int datalen)
{
	struct webhook_t *wh;
	enum tcp_state_t st;
	uint32_t now;

	if (idx >= MAX_HOOKS || !wh_context.whooks[idx].addr_str)
		return -1;

	wh = &wh_context.whooks[idx];
	now = to_ms_since_boot(get_absolute_time());
	WH_LOCK(wh);
		if (wh->sending)
			goto out_err;

		wh->buff[0] = 0;
		snprintf(wh->buff, PACKET_BUFF_SIZE, WH_HTTP_HEAD, wh->http_command, wh->endpoint,
				 wh->addr_str, wh->port, datalen, wh->keep_open?"":HTTP_CONNECTION_CLOSE,
				 HTTP_USER_AGENT, wh->content_type);
		wh->buff_len = strlen(wh->buff);
		if (wh->buff_len + datalen >= PACKET_BUFF_SIZE)
			goto out_err;
		memcpy(wh->buff + wh->buff_len, data, datalen);
		wh->buff_len += datalen;
		wh->sending = true;
		wh->last_send = now;
		st = wh->tcp_state;
	WH_UNLOCK(wh);

	if (st != TCP_CONNECTED) {
		webhook_connect(wh);
		return -1;
	}

	LWIP_LOCK_START;
		wh_tcp_send(wh, wh->tcp_conn);
	LWIP_LOCK_END;

	return 0;
out_err:
	WH_UNLOCK(wh);
	return -1;
}

static void wh_server_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	struct webhook_t *wh = (struct webhook_t *)arg;

	UNUSED(hostname);
	if (!arg)
		return;

	WH_LOCK(wh);
		memcpy(&(wh->addr), ipaddr, sizeof(ip_addr_t));
		wh->ip_resolve = IP_RESOLVED;
	WH_UNLOCK(wh);
}

static void webhook_resolve(void)
{
	uint32_t last, now;
	int st, i;
	int ret;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < wh_context.wh_count; i++) {
		WH_LOCK(&wh_context.whooks[i]);
			st = wh_context.whooks[i].ip_resolve;
			last = wh_context.whooks[i].last_send;
		WH_UNLOCK(&wh_context.whooks[i]);
		switch (st) {
		case IP_NOT_RESOLEVED:
			LWIP_LOCK_START;
				ret = dns_gethostbyname(wh_context.whooks[i].addr_str, &wh_context.whooks[i].addr,
										wh_server_found, &wh_context.whooks[i]);
			LWIP_LOCK_END;
			if (ret == ERR_INPROGRESS) {
				hlog_info(WHLOG, "Resolving %s ...", wh_context.whooks[i].addr_str);
				WH_LOCK(&wh_context.whooks[i]);
					wh_context.whooks[i].last_send = to_ms_since_boot(get_absolute_time());
					wh_context.whooks[i].ip_resolve = IP_RESOLVING;
				WH_UNLOCK(&wh_context.whooks[i]);
			} else if (ret == ERR_OK) {
				WH_LOCK(&wh_context.whooks[i]);
					wh_context.whooks[i].ip_resolve = IP_RESOLVED;
				WH_UNLOCK(&wh_context.whooks[i]);
			}
			break;
		case IP_RESOLVING:
			if ((now - last) > IP_TIMEOUT_MS) {
				WH_LOCK(&wh_context.whooks[i]);
					wh_context.whooks[i].ip_resolve = IP_NOT_RESOLEVED;
				WH_UNLOCK(&wh_context.whooks[i]);
			}
			break;
		case IP_RESOLVED:
		default:
			break;
		}
	}
}

static void webhook_connect_all(void)
{
	int i;

	for (i = 0; i < wh_context.wh_count; i++) {
		if (wh_context.whooks[i].keep_open)
			webhook_connect(&(wh_context.whooks[i]));
	}
}

void webhook_timeot_check(void)
{
	uint32_t now;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < wh_context.wh_count; i++) {
		WH_LOCK(&(wh_context.whooks[i]));
			if (wh_context.whooks[i].sending &&
			   (now - wh_context.whooks[i].last_send) > IP_TIMEOUT_MS) {
				wh_context.whooks[i].sending = false;
				wh_context.whooks[i].buff_len = 0;
				wh_context.whooks[i].buff_p = 0;
				wh_context.whooks[i].last_reply = 0;
				if (wh_context.whooks[i].user_cb)
					wh_context.whooks[i].user_cb(i, 0, wh_context.whooks[i].user_data);
			}
		WH_UNLOCK(&(wh_context.whooks[i]));
	}
}

static bool webhook_log_status(void *context)
{
	struct webhook_t *wh;
	int i;

	UNUSED(context);

	for (i = 0; i < wh_context.wh_count; i++) {
		wh = &(wh_context.whooks[i]);
		WH_LOCK(wh);
		hlog_info(WHLOG, "[%s:%d%s], %s, %s", wh->addr_str, wh->port, wh->endpoint,
				  wh->ip_resolve == IP_RESOLVED ? "resolved" : "not resolved",
				  wh->tcp_state == TCP_CONNECTED ? "connected" : "not connected");
		hlog_info(WHLOG, "   server [%s], [%s], data [%s], http [%s]",
				  inet_ntoa(wh->addr), wh->keep_open ? "permanent" : "one time",
				  wh->content_type, wh->http_command);
		hlog_info(WHLOG, "   stats: connected %d, send %d, received %d, last http [%d]",
				  wh->conn_count, wh->send_count, wh->recv_count, wh->last_reply);
		WH_UNLOCK(wh);
	}

	return true;
}

void webhook_reconnect(void)
{
	int i;

	for (i = 0; i < wh_context.wh_count; i++)
		webhook_disconnect(&(wh_context.whooks[i]));
}

bool webhook_init(void)
{
	memset(&wh_context, 0, sizeof(wh_context));
	add_status_callback(webhook_log_status, NULL);

	return true;
}

void webhook_run(void)
{
	static bool connected;

	if (!wifi_is_connected()) {
		if (connected) {
			webhook_reconnect();
			connected = false;
		}
		return;
	}

	connected = true;
	webhook_resolve();
	webhook_connect_all();
	webhook_timeot_check();
}
