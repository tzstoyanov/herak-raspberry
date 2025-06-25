// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "lwip/inet.h"
#include "lwip/altcp.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#pragma GCC diagnostic ignored "-Wstringop-truncation"

#define WS_MODULE	"webserv"
#define WEB_CMD_NR   "\r\n"
#define WS_DEBUG(C)	((C) && (C)->debug)

#define HTTP_RESPONCE_HEAD	"HTTP/1.1 %d %s\r\nDate: %s\r\nUser-Agent: %s\r\nContent-Type: text/plain; charset=UTF-8\r\nConnection: keep-alive\r\n\r\n"
#define MAX_HANDLERS		64
#define MAX_CLIENTS			3
#define WS_POLL_INTERVAL	2
#define WEBSRV_PRIO			TCP_PRIO_NORMAL
#define PACKET_BUFF_SIZE	1024
#define HTTP_CMD_LEN		10
#define HTTP_URL_LEN		128

#define IP_TIMEOUT_MS	20000

#define WC_LOCK(W)		mutex_enter_blocking(&((W)->cl_lock))
#define WC_UNLOCK(W)	mutex_exit(&((W)->cl_lock))
#define WH_LOCK(W)		mutex_enter_blocking(&((W)->h_lock))
#define WH_UNLOCK(W)	mutex_exit(&((W)->h_lock))
#define WS_LOCK(W)		mutex_enter_blocking(&((W)->slock))
#define WS_UNLOCK(W)	mutex_exit(&((W)->slock))

enum http_response_id {
	HTTP_RESP_OK = 0,
	HTTP_RESP_BAD,
	HTTP_RESP_NOT_FOUND,
	HTTP_RESP_INTERNAL_ERROR,
	HTTP_RESP_TOO_MANY_ERROR,
	HTTP_RESP_MAX
};
struct http_responses_t {
	int code;
	char *desc;
} http_responses[] = {
		{200, "OK"},					// HTTP_RESP_OK
		{400, "Bad Request"},			// HTTP_RESP_BAD
		{404, "Not Found"},				// HTTP_RESP_NOT_FOUND
		{500, "Internal Server Error"},	// HTTP_RESP_INTERNAL_ERROR
		{429, "Too Many Requests"},		// HTTP_RESP_TOO_MANY_ERROR
};

struct werbserv_context_t;

struct webclient_t {
	int idx;
	bool init;
	bool sending;
	bool close;
	mutex_t cl_lock;
	char buff[PACKET_BUFF_SIZE];
	int buff_p;
	int buff_len;
	uint32_t last_send;
	struct altcp_pcb *tcp_client;
	struct werbserv_context_t *ctx;
};

struct werbserv_context_t {
	sys_module_t mod;
	struct webclient_t client[MAX_CLIENTS];
	uint32_t port;
	bool init;
	mutex_t slock;
	struct altcp_pcb *tcp_srv;
	uint32_t debug;
};

static struct werbserv_context_t *__werbserv_context;

static struct werbserv_context_t *webserv_get_context(void)
{
	return __werbserv_context;
}

static void ws_tcp_send(struct webclient_t *client, struct altcp_pcb *tpcb)
{
	u16_t data_len;
	u16_t send_len;
	bool sending;
	err_t err;

	WC_LOCK(client);
		data_len = client->buff_len - client->buff_p;
		sending = client->sending;
	WC_UNLOCK(client);
	if (!sending || data_len <= 0)
		return;
	LWIP_LOCK_START;
		send_len = altcp_sndbuf(tpcb);
	LWIP_LOCK_END;
	if (send_len <= 0)
		return;

	/* Check if the space in TCP output buffer is larger enough for all data */
	if (send_len > data_len)
		send_len = data_len;
	LWIP_LOCK_START;
		err = altcp_write(tpcb, client->buff + client->buff_p, send_len, TCP_WRITE_FLAG_COPY);
	LWIP_LOCK_END;
	if (err == ERR_OK) {
		WC_LOCK(client);
			client->buff_p += send_len;
			if (client->buff_p >= client->buff_len) {
				client->sending = false;
				client->buff_p = 0;
				client->buff_len = 0;
			}
		WC_UNLOCK(client);
		/* Flush */
		LWIP_LOCK_START;
			altcp_output(tpcb);
		LWIP_LOCK_END;
	}
}

#define HTTP_REPLY_SIZE	128
static bool parse_http_request(struct pbuf *p, char *cmd, int cmd_len, char *url, int url_len)
{
	char reply_line[HTTP_REPLY_SIZE];
	struct pbuf *bp = p;
	bool ret = false;
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
			switch (i) {
			case 0:
				if (cmd)
					strncpy(cmd, tok, cmd_len);
				break;
			case 1:
				ret = true;
				if (url)
					strncpy(url, tok, url_len);
				break;
			}
			i++;
		}
	}

	return ret;
}

static enum http_response_id web_cmd_exec(run_context_web_t *wctx, char *cmd)
{
	enum http_response_id resp = HTTP_RESP_NOT_FOUND;
	cmd_run_context_t cmd_ctx = {0};

	if (strlen(cmd) < 1 || cmd[0] != '/')
		return HTTP_RESP_BAD;

#ifdef HAVE_COMMANDS
	cmd_ctx.type = CMD_CTX_WEB;
	cmd_ctx.context = wctx;
	debug_log_forward(wctx->client_idx);
	if (!cmd_exec(&cmd_ctx, cmd + 1))
		resp = HTTP_RESP_OK;
	else
		resp = HTTP_RESP_BAD;
#else
	UNUSED(wctx);
	UNUSED(cmd_ctx);
#endif /* HAVE_COMMANDS */

	return resp;
}

static int webserv_client_send(int client_idx, char *data, int datalen, enum http_response_id rep)
{
	struct werbserv_context_t *ctx = webserv_get_context();
	struct webclient_t *client;
	char date[32];
	uint32_t now;

	if (!ctx || client_idx >= MAX_CLIENTS || !ctx->client[client_idx].tcp_client)
		return -1;
	if (rep >= HTTP_RESP_MAX)
		return -1;

	client = &ctx->client[client_idx];
	now = to_ms_since_boot(get_absolute_time());
	WC_LOCK(client);
		if (client->sending)
			goto out_err;

		client->buff[0] = 0;
		snprintf(client->buff, PACKET_BUFF_SIZE, HTTP_RESPONCE_HEAD,
				 http_responses[rep].code, http_responses[rep].desc,
				 get_current_time_str(date, 32), HTTP_USER_AGENT);
		client->buff_len = strlen(client->buff);
		if (data && datalen) {
			if (client->buff_len + datalen >= PACKET_BUFF_SIZE)
				goto out_err;
			memcpy(client->buff + client->buff_len, data, datalen);
			client->buff_len += datalen;
		}
		client->sending = true;
		client->last_send = now;
	WC_UNLOCK(client);
	ws_tcp_send(client, client->tcp_client);
	return client->buff_len;

out_err:
	WC_UNLOCK(client);
	return -1;
}

static enum http_response_id client_parse_incoming(struct webclient_t *client, struct pbuf *p)
{
	enum http_response_id resp = HTTP_RESP_INTERNAL_ERROR;
	char cmd[HTTP_CMD_LEN], url[HTTP_URL_LEN];
	run_context_web_t wctx = {0};

	if (WS_DEBUG(client->ctx)) {
		struct pbuf *bp = p;

		hlog_info(WS_MODULE, "Received %d bytes from %d:", p->tot_len, client->idx);
		while (bp) {
			dump_char_data(WS_MODULE, bp->payload, bp->len);
			bp = bp->next;
		}
	}
	wctx.client_idx = client->idx;
	wctx.keep_open = false;
	wctx.keep_silent = false;
	webserv_client_send(client->idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	if (parse_http_request(p, cmd, HTTP_CMD_LEN, url, HTTP_URL_LEN)) {
		cmd[HTTP_CMD_LEN - 1] = 0;
		url[HTTP_URL_LEN - 1] = 0;
		resp = web_cmd_exec(&wctx, url);
	} else {
		resp = HTTP_RESP_INTERNAL_ERROR;
	}
	if (!wctx.keep_silent) {
		webserv_client_send_data(client->idx, WEB_CMD_NR, strlen(WEB_CMD_NR));
		if (wctx.hret)
			webserv_client_send(client->idx, http_responses[HTTP_RESP_BAD].desc,
								strlen(http_responses[HTTP_RESP_BAD].desc), HTTP_RESP_BAD);
		else
			webserv_client_send(client->idx, WEB_CMD_NR,
								strlen(WEB_CMD_NR), resp);
	}
	if (!wctx.keep_open)
		webserv_client_close(client->idx);

	return resp;
}

static void webclient_disconnect(struct webclient_t *client, char *reason)
{
	if (!client || !client->init)
		return;

	if (WS_DEBUG(client->ctx))
		hlog_info(WS_MODULE, "Closed connection to client %d: [%s]", client->idx, reason);

	WC_LOCK(client);
		if (client->tcp_client) {
			LWIP_LOCK_START;
				altcp_recv(client->tcp_client, NULL);
				altcp_err(client->tcp_client,  NULL);
				if (altcp_close(client->tcp_client) != ERR_OK)
					altcp_abort(client->tcp_client);
				client->tcp_client = NULL;
			LWIP_LOCK_END;
		}

		client->buff_p = 0;
		client->buff_len = 0;
		client->close = false;
		client->init = false;
		client->sending = false;
	WC_UNLOCK(client);
}

static err_t ws_tcp_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct webclient_t *client = (struct webclient_t *)arg;
	enum http_response_id ret;

	if (p == NULL) {
		/* remote has closed connection */
		webclient_disconnect(client, "Remote closed");
		return ERR_OK;
	}

	if (err != ERR_OK) {
		pbuf_free(p);
		return err;
	}

	/* Tell remote that data has been received */
	LWIP_LOCK_START;
		altcp_recved(pcb, p->tot_len);
	LWIP_LOCK_END;
	ret = client_parse_incoming(client, p);
	pbuf_free(p);
	if (ret != HTTP_RESP_OK)
		webserv_client_close(client->idx);

	return ERR_OK;
}

static void ws_tcp_err_cb(void *arg, err_t err)
{
	struct webclient_t *client = (struct webclient_t *)arg;

	UNUSED(err);
	WC_LOCK(client);
		/* Set conn to null as pcb is already deallocated*/
		client->tcp_client = NULL;
	WC_UNLOCK(client);
	webclient_disconnect(client, "tcp error");
}

int webserv_client_close(int client_idx)
{
	struct werbserv_context_t *ctx = webserv_get_context();

	if (!ctx || client_idx >= MAX_CLIENTS || !ctx->client[client_idx].tcp_client)
		return -1;
	WC_LOCK(&(ctx->client[client_idx]));
		ctx->client[client_idx].close = true;
	WC_UNLOCK(&(ctx->client[client_idx]));
	debug_log_forward(-1);
	return 0;
}

int webserv_client_send_data(int client_idx, char *data, int datalen)
{
	struct werbserv_context_t *ctx = webserv_get_context();
	struct webclient_t *client;
	uint32_t now;
	int len;

	if (!ctx || client_idx >= MAX_CLIENTS ||
		!ctx->client[client_idx].tcp_client || !data || !datalen)
		return -1;

	client = &ctx->client[client_idx];
	now = to_ms_since_boot(get_absolute_time());
	WC_LOCK(client);
		if (client->sending)
			goto out_err;

		client->buff[0] = 0;
		len = datalen < PACKET_BUFF_SIZE ? datalen : PACKET_BUFF_SIZE;
		memcpy(client->buff, data, len);
		client->buff_len = len;
		client->sending = true;
		client->last_send = now;
	WC_UNLOCK(client);
	ws_tcp_send(client, client->tcp_client);
	return len;
out_err:
	WC_UNLOCK(client);
	return -1;
}

static void webclient_close_check(struct werbserv_context_t *ctx)
{
	uint32_t now;
	bool close;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!ctx->client[i].init)
			continue;
		WC_LOCK(&(ctx->client[i]));
			close = ctx->client[i].close;
			if (ctx->client[i].sending &&
			   (now - ctx->client[i].last_send) > IP_TIMEOUT_MS) {
				close = true;
			}
		WC_UNLOCK(&(ctx->client[i]));
		if (close)
			webclient_disconnect(&(ctx->client[i]), "normal timeout");
	}
}

static bool sys_webserv_log_status(void *context)
{
	struct werbserv_context_t *ctx = (struct werbserv_context_t *)context;
	int i, cnt;

	if (!ctx->init) {
		hlog_info(WS_MODULE, "Web server at port %d not init yet", ctx->port);
	} else {
		cnt = 0;
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (ctx->client[i].tcp_client)
				cnt++;
		}
		hlog_info(WS_MODULE, "Web server is running at port %d, %d clients attached",
						 ctx->port, cnt);
	}

	return true;
}

static void sys_webserv_reconnect(void *context)
{
	struct werbserv_context_t *ctx = (struct werbserv_context_t *)context;
	int i;

	WS_LOCK(ctx);
		for (i = 0; i < MAX_CLIENTS; i++)
			webclient_disconnect(&(ctx->client[i]), "reconnect");
	WS_UNLOCK(ctx);
}

static bool webserv_read_config(struct werbserv_context_t **ctx)
{
	char *str;
	int port;

	str = USER_PRAM_GET(WEBSERVER_PORT);
	if (!str)
		return false;

	port = (int)strtol(str, NULL, 0);
	free(str);

	if (port <= 0 || port > 0xFFFF)
		return false;

	(*ctx) = (struct werbserv_context_t *)calloc(1, sizeof(struct werbserv_context_t));
	if (!(*ctx))
		return false;
	(*ctx)->port = port;

	return true;
}

static err_t webserv_accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
	struct werbserv_context_t *ctx = (struct werbserv_context_t *)arg;
	int i;

	if (err != ERR_OK || pcb == NULL)
		return ERR_VAL;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (!ctx->client[i].tcp_client)
			break;
	if (WS_DEBUG(ctx))
		hlog_info(WS_MODULE, "Accepted new client %d / %d", i, MAX_CLIENTS);

	if (i >= MAX_CLIENTS)
		return ERR_MEM;
	if (!ctx->client[i].init) {
		mutex_init(&(ctx->client[i].cl_lock));
		ctx->client[i].idx = i;
		ctx->client[i].ctx = ctx;
		ctx->client[i].init = true;
	}

	LWIP_LOCK_START;
		altcp_setprio(pcb, WEBSRV_PRIO);
		altcp_arg(pcb, &(ctx->client[i]));
		altcp_recv(pcb, ws_tcp_recv_cb);
		altcp_err(pcb, ws_tcp_err_cb);
	LWIP_LOCK_END;
	ctx->client[i].tcp_client = pcb;
	return ERR_OK;
}

static bool sys_webserv_init(struct werbserv_context_t **ctx)
{
	if (!webserv_read_config(ctx))
		return false;

	mutex_init(&((*ctx)->slock));
	__werbserv_context = (*ctx);

	return true;
}

static bool webserv_open(struct werbserv_context_t *ctx)
{
	struct altcp_pcb *pcb = NULL;
	bool ret = false;

	LWIP_LOCK_START;
		pcb = altcp_new_ip_type(NULL, IPADDR_TYPE_ANY);
	LWIP_LOCK_END;
	if (!pcb)
		return false;

	LWIP_LOCK_START;
		altcp_arg(pcb, ctx);
		altcp_setprio(pcb, WEBSRV_PRIO);
		if (altcp_bind(pcb, IP_ANY_TYPE, ctx->port) == ERR_OK) {
			pcb = altcp_listen(pcb);
			if (pcb) {
				altcp_accept(pcb, webserv_accept);
				ret = true;
			}
		}
	LWIP_LOCK_END;

	if (!ret && pcb) {
		if (altcp_close(pcb) != ERR_OK)
			altcp_abort(pcb);
		pcb = NULL;
	}

	ctx->tcp_srv = pcb;
	return ret;
}

static void webclient_send_poll(struct werbserv_context_t *ctx)
{
	bool send;
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!ctx->client[i].init)
			continue;
		send = false;
		WC_LOCK(&(ctx->client[i]));
			if (ctx->client[i].tcp_client &&
				!ctx->client[i].sending &&
				(ctx->client[i].buff_len - ctx->client[i].buff_p) > 0)
				send = true;
		WC_UNLOCK(&(ctx->client[i]));
		if (send)
			ws_tcp_send(&(ctx->client[i]), ctx->client[i].tcp_client);
	}
}

static void sys_webhook_run(void *context)
{
	struct werbserv_context_t *ctx = (struct werbserv_context_t *)context;
	static bool connected;

	if (!ctx->init) {
		ctx->init = webserv_open(ctx);
		if (!ctx->init)
			return;
	}

	if (!wifi_is_connected()) {
		if (connected) {
			sys_webserv_reconnect(ctx);
			connected = false;
		}
		return;
	}

	connected = true;
	webclient_close_check(ctx);
	webclient_send_poll(ctx);
}

int webserv_port(void)
{
	struct werbserv_context_t *ctx = webserv_get_context();

	if (!ctx)
		return 0;

	return ctx->port;
}

static void sys_webserv_debug_set(uint32_t lvl, void *context)
{
	struct werbserv_context_t *ctx = (struct werbserv_context_t *)context;

	ctx->debug = lvl;
}

void sys_webserver_register(void)
{
	struct werbserv_context_t *ctx = NULL;

	if (!sys_webserv_init(&ctx))
		return;

	ctx->mod.name = WS_MODULE;
	ctx->mod.run = sys_webhook_run;
	ctx->mod.log = sys_webserv_log_status;
	ctx->mod.debug = sys_webserv_debug_set;
	ctx->mod.reconnect = sys_webserv_reconnect;
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
