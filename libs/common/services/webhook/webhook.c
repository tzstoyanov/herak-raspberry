// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "lwip/inet.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/altcp.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define WH_MODULE	"webhook"
#define WH_DEFAULT_PORT	80

//#define WH_DEBUG

#define WH_HTTP_HEAD		"%s %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Length: %d\r\n%sUser-Agent: %s\r\nContent-Type: %s\r\n\r\n"
#define MAX_HOOKS			5
#define PACKET_BUFF_SIZE	512

#define HTTP_CONNECTION_CLOSE	"Connection: close\r\n"

#define IP_TIMEOUT_MS	20000
#define WH_LOCK(W)		mutex_enter_blocking(&((W)->lock))
#define WH_UNLOCK(W)	mutex_exit(&((W)->lock))
#define IS_DEBUG(C)	((C)->debug != 0)

#define	WH_PAYLOAD_TEMPLATE "{ \"message\":\"(%s) %s\"}"
#define WH_HTTP_CMD		"POST"
#define WH_HTTP_TYPE	"application/json"

enum tcp_state_t {
	TCP_DISCONNECTED = 0,
	TCP_CONNECTING,
	TCP_CONNECTED
};

struct wh_context_t;

struct webhook_t {
	int idx;
	char *addr_str;			// example.com or 192.168.1.1
	char *endpoint;			// /api/webhook/-0vJZOQ7D9NCj3Iz3p63uSMAI
	char *http_command;		// POST
	char *content_type;		// application/json
	int port;				// webhook server TCP port
	ip_addr_t addr;
	ip_resolve_state_t ip_resolve;
	uint64_t last_send;
	uint32_t conn_count;
	uint32_t send_count;
	uint32_t recv_count;
	uint32_t last_reply;
	bool sending;
	bool keep_open;
	enum tcp_state_t tcp_state;
	struct altcp_pcb *tcp_conn;
	char buff[PACKET_BUFF_SIZE];
	char payload[WH_PAYLOAD_MAX_SIZE];
	int buff_p;
	int buff_len;
	mutex_t lock;
	struct wh_context_t *ctx;
};

struct wh_context_t {
	sys_module_t mod;
	struct webhook_t wh_srv;
	uint32_t debug;
};

static struct wh_context_t *__wh_context;

static struct wh_context_t *webhook_context_get(void)
{
	return __wh_context;
}

static void wh_tcp_send(struct webhook_t *wh, struct altcp_pcb *tpcb)
{
	u16_t data_len;
	u16_t send_len;
	err_t err;

	WH_LOCK(wh);
		data_len = wh->buff_len - wh->buff_p;
		LWIP_LOCK_START;
			send_len = altcp_sndbuf(tpcb);
		LWIP_LOCK_END;

		if (!wh->sending || send_len <= 0 || data_len <= 0)
			goto out;

		/* Check if the space in TCP output buffer is larger enough for all data */
		if (send_len > data_len)
			send_len = data_len;
		LWIP_LOCK_START;
			err = altcp_write(tpcb, wh->buff + wh->buff_p, send_len, TCP_WRITE_FLAG_COPY);
		LWIP_LOCK_END;
		if (err == ERR_OK) {
			wh->buff_p += send_len;
			if (wh->buff_p >= wh->buff_len) {
				wh->sending = false;
				wh->buff_p = 0;
				wh->buff_len = 0;
				wh->send_count++;
			}
			/* Flush */
			LWIP_LOCK_START;
				altcp_output(tpcb);
			LWIP_LOCK_END;
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
			hlog_info(WH_MODULE, "Disconnected form %s:%d", wh->addr_str, wh->port);
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

	if (IS_DEBUG(wh->ctx))	{
		struct pbuf *bp = p;

		hlog_info(WH_MODULE, "Received %d bytes from %s:", p->tot_len, wh->addr_str);
		while (bp) {
			dump_char_data(WH_MODULE, bp->payload, bp->len);
			bp = bp->next;
		}
	}

	hcode = wh_parse_http_reply(p);
	if (hcode >= 0) {
		WH_LOCK(wh);
			wh->last_reply = hcode;
		WH_UNLOCK(wh);
	}

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
		wh->last_send = time_ms_since_boot();
		if (!wh->keep_open)
			hlog_info(WH_MODULE, "Connected to %s:%d", wh->addr_str, wh->port);
	WH_UNLOCK(wh);

	return ERR_OK;
}

static void webhook_connect(struct webhook_t *wh)
{
	uint64_t last, now;
	int st_tcp, st_res;
	err_t err;

	now = time_ms_since_boot();
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
				wh->tcp_conn = altcp_new_ip_type(NULL, IP_GET_TYPE(&(wh->addr)));
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

bool webhook_connected(void)
{
	struct wh_context_t *ctx = webhook_context_get();

	if (!ctx)
		return false;
	if (ctx->wh_srv.tcp_state != TCP_CONNECTED)
		return false;

	return true;
}

int webhook_send(char *message)
{
	struct wh_context_t *ctx = webhook_context_get();
	struct webhook_t *wh;
	uint64_t now;

	if (!ctx)
		return -1;

	wh = &ctx->wh_srv;
	now = time_ms_since_boot();
	WH_LOCK(wh);
		if (wh->sending)
			goto out_err;
		if (wh->tcp_state != TCP_CONNECTED) {
			webhook_connect(wh);
			goto out_err;
		}

		wh->payload[0] = 0;
		snprintf(wh->payload, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
				 system_get_hostname(), message);

		wh->buff[0] = 0;
		snprintf(wh->buff, PACKET_BUFF_SIZE, WH_HTTP_HEAD, WH_HTTP_CMD, wh->endpoint,
				 wh->addr_str, wh->port, strlen(wh->payload),
				 wh->keep_open ? "" : HTTP_CONNECTION_CLOSE, HTTP_USER_AGENT, WH_HTTP_TYPE);
		wh->buff_len = strlen(wh->buff);
		if (wh->buff_len + strlen(wh->payload) >= PACKET_BUFF_SIZE)
			goto out_err;
		memcpy(wh->buff + wh->buff_len, wh->payload, strlen(wh->payload));
		wh->buff_len += strlen(wh->payload);
		wh->sending = true;
		wh->last_send = now;
	WH_UNLOCK(wh);

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

static void webhook_resolve(struct wh_context_t *ctx)
{
	uint64_t last, now;
	int ret;
	int st;

	now = time_ms_since_boot();
	WH_LOCK(&ctx->wh_srv);
		st = ctx->wh_srv.ip_resolve;
		last = ctx->wh_srv.last_send;
	WH_UNLOCK(&ctx->wh_srv);

	switch (st) {
	case IP_NOT_RESOLEVED:
		LWIP_LOCK_START;
			ret = dns_gethostbyname(ctx->wh_srv.addr_str, &ctx->wh_srv.addr,
									wh_server_found, &ctx->wh_srv);
		LWIP_LOCK_END;
		if (ret == ERR_INPROGRESS) {
			hlog_info(WH_MODULE, "Resolving %s ...", ctx->wh_srv.addr_str);
			WH_LOCK(&ctx->wh_srv);
				ctx->wh_srv.last_send = time_ms_since_boot();
				ctx->wh_srv.ip_resolve = IP_RESOLVING;
			WH_UNLOCK(&ctx->wh_srv);
		} else if (ret == ERR_OK) {
			WH_LOCK(&ctx->wh_srv);
				ctx->wh_srv.ip_resolve = IP_RESOLVED;
			WH_UNLOCK(&ctx->wh_srv);
		}
		break;
	case IP_RESOLVING:
		if ((now - last) > IP_TIMEOUT_MS) {
			WH_LOCK(&ctx->wh_srv);
				ctx->wh_srv.ip_resolve = IP_NOT_RESOLEVED;
			WH_UNLOCK(&ctx->wh_srv);
		}
		break;
	case IP_RESOLVED:
	default:
		break;
	}
}

void webhook_timeout_check(struct wh_context_t *ctx)
{
	uint64_t now;

	now = time_ms_since_boot();
	WH_LOCK(&ctx->wh_srv);
		if (ctx->wh_srv.sending &&
		   (now - ctx->wh_srv.last_send) > IP_TIMEOUT_MS) {
			ctx->wh_srv.sending = false;
			ctx->wh_srv.buff_len = 0;
			ctx->wh_srv.buff_p = 0;
			ctx->wh_srv.last_reply = 0;
		}
	WH_UNLOCK(&ctx->wh_srv);
}

static bool sys_webhook_log_status(void *context)
{
	struct wh_context_t *ctx = (struct wh_context_t *)context;
	struct webhook_t *wh;

	wh = &ctx->wh_srv;
	WH_LOCK(wh);
	hlog_info(WH_MODULE, "[%s:%d%s], %s, %s", wh->addr_str, wh->port, wh->endpoint,
			  wh->ip_resolve == IP_RESOLVED ? "resolved" : "not resolved",
			  wh->tcp_state == TCP_CONNECTED ? "connected" : "not connected");
	hlog_info(WH_MODULE, "   server [%s], [%s]",
			  inet_ntoa(wh->addr), wh->keep_open ? "permanent" : "one time");
	hlog_info(WH_MODULE, "   stats: connected %d, send %d, received %d, last http [%d]",
			  wh->conn_count, wh->send_count, wh->recv_count, wh->last_reply);
	WH_UNLOCK(wh);

	return true;
}

static bool notify_get_config(char **server, char **endpoint, int *port)
{
	char *port_str = NULL;
	char *srv = NULL;
	char *ep = NULL;
	int port_id = 0;

	srv = USER_PRAM_GET(WEBHOOK_SERVER);
	if (!srv || strlen(srv) < 1)
		goto out_err;
	ep = USER_PRAM_GET(WEBHOOK_ENDPOINT);
	if (!ep || strlen(ep) < 1)
		goto out_err;

	port_str = USER_PRAM_GET(WEBHOOK_PORT);
	if (port_str && strlen(port_str) > 1)
		port_id = atoi(port_str);
	if (!port_id)
		port_id = WH_DEFAULT_PORT;
	free(port_str);

	if (server)
		*server = srv;
	else
		free(srv);
	if (endpoint)
		*endpoint = ep;
	else
		free(ep);
	if (port)
		*port = port_id;

	return true;
out_err:
	free(srv);
	free(ep);
	return false;
}

bool sys_webhook_init(struct wh_context_t **ctx)
{
	char *server = NULL;
	char *endpoint = NULL;
	int port;

	if (!notify_get_config(&server, &endpoint, &port))
		return false;

	(*ctx) = calloc(1, sizeof(struct wh_context_t));
	if (!(*ctx))
		return false;

	(*ctx)->wh_srv.addr_str = server;
	(*ctx)->wh_srv.port = port;
	//ctx->whooks[i]->content_type = strdup(content_type);
	(*ctx)->wh_srv.endpoint = endpoint;
	//ctx->whooks[i]->http_command = strdup(http_command);
	(*ctx)->wh_srv.ip_resolve = IP_NOT_RESOLEVED;
	(*ctx)->wh_srv.tcp_state = TCP_DISCONNECTED;
	(*ctx)->wh_srv.keep_open = true;
	(*ctx)->wh_srv.last_reply = -1;
	(*ctx)->wh_srv.ctx = *ctx;
	mutex_init(&((*ctx)->wh_srv.lock));

	__wh_context = (*ctx);
	return true;
}

static void sys_webhook_run(void *context)
{
	struct wh_context_t *ctx = (struct wh_context_t *)context;
	static bool connected;

	if (!WIFI_IS_CONNECTED) {
		if (connected) {
			webhook_disconnect(&ctx->wh_srv);
			connected = false;
		}
		return;
	}

	connected = true;
	webhook_resolve(ctx);
	if (ctx->wh_srv.keep_open)
		webhook_connect(&ctx->wh_srv);
	webhook_timeout_check(ctx);
}

static void sys_webhook_debug_set(uint32_t lvl, void *context)
{
	struct wh_context_t *ctx = (struct wh_context_t *)context;

	ctx->debug = lvl;
}

static void sys_webhook_reconnect(void *context)
{
	struct wh_context_t *ctx = (struct wh_context_t *)context;

	webhook_disconnect(&ctx->wh_srv);
}

void sys_webhook_register(void)
{
	struct wh_context_t *ctx = NULL;

	if (!sys_webhook_init(&ctx))
		return;

	ctx->mod.name = WH_MODULE;
	ctx->mod.run = sys_webhook_run;
	ctx->mod.log = sys_webhook_log_status;
	ctx->mod.debug = sys_webhook_debug_set;
	ctx->mod.reconnect = sys_webhook_reconnect;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
