// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "lwip/inet.h"
#include "lwip/altcp.h"

#include "base64.h"
#include "params.h"

#pragma GCC diagnostic ignored "-Wstringop-truncation"

#define WSLOG		"webserv"
#define HELP_CMD	"help"
#define HELP_URL	"/help"
// #define WS_DEBUG

#define HTTP_RESPONCE_HEAD	"HTTP/1.1 %d %s\r\nDate: %s\r\nUser-Agent: %s\r\nContent-Type: text/plain; charset=UTF-8\r\nConnection: keep-alive\r\n\r\n"
#define MAX_HANDLERS		3
#define MAX_CLIENTS			3
#define WS_POLL_INTERVAL	2
#define WEBSRV_PRIO			TCP_PRIO_NORMAL
#define PACKET_BUFF_SIZE	512
#define HTTP_CMD_LEN		5
#define HTTP_URL_LEN		32

#define IP_TIMEOUT_MS	20000

#define WC_LOCK(W)		mutex_enter_blocking(&((W)->cl_lock))
#define WC_UNLOCK(W)	mutex_exit(&((W)->cl_lock))
#define WH_LOCK(W)		mutex_enter_blocking(&((W)->h_lock))
#define WH_UNLOCK(W)	mutex_exit(&((W)->h_lock))
#define WS_LOCK(W)		mutex_enter_blocking(&((W)->slock))
#define WS_UNLOCK(W)	mutex_exit(&((W)->slock))

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

struct webcmd_t {
	int count;
	int web_handler;
	char *description;
	void *user_data;
	app_command_t *commands;
};

struct webhandler_t {
	char url[HTTP_URL_LEN];
	webserv_request_cb_t user_cb;
	void *user_data;
	mutex_t h_lock;
};

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
};

static struct {
	struct webcmd_t commands[MAX_HANDLERS];
	int wcmd_count;
	struct webhandler_t handle[MAX_HANDLERS];
	int wh_count;
	struct webclient_t client[MAX_CLIENTS];
	uint32_t port;
	bool init;
	mutex_t slock;
	struct altcp_pcb *tcp_srv;
} werbserv_context;

static int webserv_add_handler(char *url, webserv_request_cb_t user_cb, void *user_data)
{
	int i;

	if (!werbserv_context.port)
		return -1;

	WS_LOCK(&werbserv_context);
		for (i = 0; i < MAX_HANDLERS; i++) {
			if (!werbserv_context.handle[i].user_cb)
				break;
		}
	WS_UNLOCK(&werbserv_context);

	if (i >= MAX_HANDLERS)
		return -1;

	WS_LOCK(&werbserv_context);
		werbserv_context.handle[i].user_cb = user_cb;
		werbserv_context.handle[i].user_data = user_data;
		if (url[0] != '/') {
			werbserv_context.handle[i].url[0] = '/';
			strncpy(werbserv_context.handle[i].url + 1, url, HTTP_URL_LEN - 1);
		} else {
			strncpy(werbserv_context.handle[i].url, url, HTTP_URL_LEN);
		}
		werbserv_context.handle[i].url[HTTP_URL_LEN - 1] = 0;
		mutex_init(&werbserv_context.handle[i].h_lock);
		werbserv_context.wh_count++;
	WS_UNLOCK(&werbserv_context);

	hlog_info(WSLOG, "New Web Handler added [%s]", url);
	return i;
}

#define HELP_SIZE	128
static void commands_help(int client_idx, struct webcmd_t *handlers)
{
	char help[HELP_SIZE];
	int i;

	if (handlers->web_handler >= werbserv_context.wh_count)
		return;

	for (i = 0; i < handlers->count; i++) {
		snprintf(help, HELP_SIZE, "\t%s?%s%s\r\n",
				werbserv_context.handle[handlers->web_handler].url,
				handlers->commands[i].command, handlers->commands[i].help ? handlers->commands[i].help : "");
		weberv_client_send_data(client_idx, help, strlen(help));
	}
}

static enum http_response_id commands_handler(run_context_web_t *wctx, char *cmd, char *url, void *context)
{
	struct webcmd_t *handlers = (struct webcmd_t *)context;
	int ret = HTTP_RESP_NOT_FOUND;
	cmd_run_context_t r_ctx = {0};
	char *request, *params;
	size_t len;
	int i;

	if (!cmd)
		return HTTP_RESP_INTERNAL_ERROR;

	request = strchr(url, '?');
	if (!request)
		request = strchr(url + 1, '/');
	r_ctx.type = CMD_CTX_WEB;
	r_ctx.context.web.client_idx = wctx->client_idx;
	if (request) {
		len = strlen(request);
		if (len >= strlen(HELP_CMD) && !strncmp(request + 1, HELP_CMD, strlen(HELP_CMD))) {
			weberv_client_send_data(wctx->client_idx, handlers->description, strlen(handlers->description));
			weberv_client_send_data(wctx->client_idx, ":\n\r", strlen(":\n\r"));
			commands_help(wctx->client_idx, handlers);
			ret = HTTP_RESP_OK;
			r_ctx.context.web.hret = 0;
			goto out;
		}
		for (i = 0; i < handlers->count; i++) {
			if (len < strlen(handlers->commands[i].command))
				continue;
			if (!strncmp(request + 1, handlers->commands[i].command, strlen(handlers->commands[i].command))) {
				params = NULL;
				if (strlen(request + 1) > strlen(handlers->commands[i].command)) {
					params = request + 1 + strlen(handlers->commands[i].command);
					if (*params != ':')
						continue;
				}
				r_ctx.context.web.hret = handlers->commands[i].cb(&r_ctx, handlers->commands[i].command, params, handlers->user_data);
				break;
			}
		}
		if (i < handlers->count)
			ret = HTTP_RESP_OK;
	}
out:
	memcpy(wctx, &r_ctx.context.web, sizeof(run_context_web_t));
	return ret;
}

int webserv_add_commands(char *url, app_command_t *commands, int commands_cont, char *description, void *user_data)
{
	struct webcmd_t *cmd;

	if (werbserv_context.wcmd_count >= MAX_HANDLERS)
		return -1;
	cmd = &(werbserv_context.commands[werbserv_context.wcmd_count]);
	cmd->web_handler = webserv_add_handler(url, commands_handler, cmd);
	if (cmd->web_handler < 0)
		return -1;
	cmd->commands = commands;
	cmd->count = commands_cont;
	cmd->user_data = user_data;
	cmd->description = description;

	werbserv_context.wcmd_count++;
	return werbserv_context.wcmd_count-1;
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

#define HTTP_REPLY_SIZE	64
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

#define CMD_OK_STR			"done\n\r"
#define CMD_FAIL_STR		"fail\n\r"
#define CMD_WRONG_STR		"invalid command\n\r"
#define CMD_NOT_FOUND_STR	"command not found n\r"
static enum http_response_id client_parse_incoming(struct webclient_t *client, struct pbuf *p)
{
	enum http_response_id resp = HTTP_RESP_INTERNAL_ERROR;
	char cmd[HTTP_CMD_LEN], url[HTTP_URL_LEN];
	run_context_web_t wctx = {0};
	int handled = 0;
	int i;

#ifdef WS_DEBUG
	{
		struct pbuf *bp = p;

		hlog_info(WSLOG, "Received %d bytes from %d:", p->tot_len, client->idx);
		while (bp) {
			dump_char_data(WSLOG, bp->payload, bp->len);
			bp = bp->next;
		}
	}
#endif
	wctx.client_idx = client->idx;
	wctx.keep_open = false;
	wctx.keep_silent = false;
	weberv_client_send(client->idx, WEB_CMD_NR, strlen(WEB_CMD_NR), HTTP_RESP_OK);
	if (parse_http_request(p, cmd, HTTP_CMD_LEN, url, HTTP_URL_LEN)) {
		cmd[HTTP_CMD_LEN - 1] = 0;
		url[HTTP_URL_LEN - 1] = 0;
		for (i = 0; i < MAX_HANDLERS; i++) {
			if (!werbserv_context.handle[i].user_cb)
				continue;
			if (!strncmp(url, werbserv_context.handle[i].url, strlen(werbserv_context.handle[i].url))) {
				resp = werbserv_context.handle[i].user_cb(&wctx, cmd, url,
														  werbserv_context.handle[i].user_data);
				if (resp == HTTP_RESP_OK)
					handled++;
			}
		}
		if (!handled)
			resp = HTTP_RESP_NOT_FOUND;
		else
			resp = HTTP_RESP_OK;
	} else {
		resp = HTTP_RESP_INTERNAL_ERROR;
	}
	if (!wctx.keep_silent) {
		weberv_client_send_data(client->idx, WEB_CMD_NR, strlen(WEB_CMD_NR));
		if (wctx.hret)
			weberv_client_send(client->idx, CMD_FAIL_STR, strlen(CMD_FAIL_STR), HTTP_RESP_BAD);
		else if (resp == HTTP_RESP_OK)
			weberv_client_send(client->idx, CMD_OK_STR, strlen(CMD_OK_STR), HTTP_RESP_OK);
		else if (resp == HTTP_RESP_NOT_FOUND)
			weberv_client_send(client->idx, CMD_NOT_FOUND_STR, strlen(CMD_NOT_FOUND_STR), HTTP_RESP_NOT_FOUND);
		else
			weberv_client_send(client->idx, CMD_WRONG_STR, strlen(CMD_WRONG_STR), HTTP_RESP_INTERNAL_ERROR);
	}
	if (!wctx.keep_open)
		weberv_client_close(client->idx);

	return resp;
}

static void webclient_disconnect(struct webclient_t *client, char *reason)
{
	if (!client || !client->init)
		return;

#ifdef WS_DEBUG
	hlog_info(WSLOG, "Closed connection to client %d: [%s]", client->idx, reason);
#else
	UNUSED(reason);
#endif

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
		weberv_client_close(client->idx);

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

int weberv_client_close(int client_idx)
{
	if (client_idx >= MAX_CLIENTS || !werbserv_context.client[client_idx].tcp_client)
		return -1;
	WC_LOCK(&(werbserv_context.client[client_idx]));
		werbserv_context.client[client_idx].close = true;
	WC_UNLOCK(&(werbserv_context.client[client_idx]));
	return 0;
}

int weberv_client_send(int client_idx, char *data, int datalen, enum http_response_id rep)
{
	struct webclient_t *client;
	char date[32];
	uint32_t now;

	if (client_idx >= MAX_CLIENTS || !werbserv_context.client[client_idx].tcp_client)
		return -1;
	if (rep >= HTTP_RESP_MAX)
		return -1;

	client = &werbserv_context.client[client_idx];
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

int weberv_client_send_data(int client_idx, char *data, int datalen)
{
	struct webclient_t *client;
	uint32_t now;
	int len;

	if (client_idx >= MAX_CLIENTS ||
		!werbserv_context.client[client_idx].tcp_client || !data || !datalen)
		return -1;

	client = &werbserv_context.client[client_idx];
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

static void webclient_close_check(void)
{
	uint32_t now;
	bool close;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!werbserv_context.client[i].init)
			continue;
		WC_LOCK(&(werbserv_context.client[i]));
			close = werbserv_context.client[i].close;
			if (werbserv_context.client[i].sending &&
			   (now - werbserv_context.client[i].last_send) > IP_TIMEOUT_MS) {
				close = true;
			}
		WC_UNLOCK(&(werbserv_context.client[i]));
		if (close)
			webclient_disconnect(&(werbserv_context.client[i]), "normal timeout");
	}
}

static void webserv_log_status(void *context)
{
	int i, cnt;

	UNUSED(context);

	if (!werbserv_context.wh_count)
		return;

	if (!werbserv_context.init) {
		hlog_info(WSLOG, "Web server at port %d not init yet", werbserv_context.port);
	} else {
		cnt = 0;
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (werbserv_context.client[i].tcp_client)
				cnt++;
		}
		hlog_info(WSLOG, "Web server is running at port %d, %d clients attached",
						 werbserv_context.port, cnt);
	}
	hlog_info(WSLOG, "  %d hook(s) registered", werbserv_context.wh_count);
	for (i = 0; i < MAX_HANDLERS; i++) {
		if (werbserv_context.handle[i].user_cb)
			hlog_info(WSLOG, "    [%s]", werbserv_context.handle[i].url);
	}
}

void webserv_reconnect(void)
{
	int i;

	WS_LOCK(&werbserv_context);
		for (i = 0; i < MAX_CLIENTS; i++)
			webclient_disconnect(&(werbserv_context.client[i]), "reconnect");
	WS_UNLOCK(&werbserv_context);
}

static bool webserv_read_config(void)
{
	char *str;
	int port;

	if (WEBSERVER_PORT_len <= 0)
		return false;
	str = param_get(WEBSERVER_PORT);
	port = (int)strtol(str, NULL, 10);
	free(str);
	if (port <= 0 || port > 0xFFFF)
		return false;
	werbserv_context.port = port;
	return true;
}

static err_t webserv_accept(void *arg, struct altcp_pcb *pcb, err_t err)
{
	int i;

	UNUSED(arg);
	if (err != ERR_OK || pcb == NULL)
		return ERR_VAL;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (!werbserv_context.client[i].tcp_client)
			break;
#ifdef WS_DEBUG
	hlog_info(WSLOG, "Accepted new client %d / %d", i, MAX_CLIENTS);
#endif
	if (i >= MAX_CLIENTS)
		return ERR_MEM;
	if (!werbserv_context.client[i].init) {
		mutex_init(&(werbserv_context.client[i].cl_lock));
		werbserv_context.client[i].idx = i;
		werbserv_context.client[i].init = true;
	}

	LWIP_LOCK_START;
		altcp_setprio(pcb, WEBSRV_PRIO);
		altcp_arg(pcb, &(werbserv_context.client[i]));
		altcp_recv(pcb, ws_tcp_recv_cb);
		altcp_err(pcb, ws_tcp_err_cb);
	LWIP_LOCK_END;
	werbserv_context.client[i].tcp_client = pcb;
	return ERR_OK;
}

static enum http_response_id webserv_help_cb(run_context_web_t *wctx, char *cmd, char *url, void *context)
{
	struct webcmd_t *web_cmd;
	char help[HELP_SIZE];
	int i;

	UNUSED(context);
	UNUSED(cmd);
	UNUSED(url);

	weberv_client_send_data(wctx->client_idx, "\n\r", strlen("\n\r"));
	for (i = 0; i < werbserv_context.wcmd_count; i++) {
		web_cmd = &werbserv_context.commands[i];
		if (web_cmd->web_handler >= werbserv_context.wh_count)
			continue;
		snprintf(help, HELP_SIZE, "  %s     [%s]\n\r",
				 werbserv_context.handle[web_cmd->web_handler].url,  web_cmd->description);
		weberv_client_send_data(wctx->client_idx, help, strlen(help));
		commands_help(wctx->client_idx, web_cmd);
	}

	return HTTP_RESP_OK;
}

bool webserv_init(void)
{
	bool ret;

	memset(&werbserv_context, 0, sizeof(werbserv_context));
	mutex_init(&werbserv_context.slock);
	ret = webserv_read_config();
	if (ret)
		webserv_add_handler(HELP_URL, webserv_help_cb, NULL);

	add_status_callback(webserv_log_status, NULL);

	return ret;
}

static bool webserv_open(void)
{
	struct altcp_pcb *pcb = NULL;
	bool ret = false;

	LWIP_LOCK_START;
		pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_ANY);
	LWIP_LOCK_END;
	if (!pcb)
		return false;

	LWIP_LOCK_START;
		altcp_setprio(pcb, WEBSRV_PRIO);
		if (altcp_bind(pcb, IP_ANY_TYPE, werbserv_context.port) == ERR_OK) {
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

	werbserv_context.tcp_srv = pcb;
	return ret;
}

static void webclient_send_poll(void)
{
	bool send;
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!werbserv_context.client[i].init)
			continue;
		send = false;
		WC_LOCK(&(werbserv_context.client[i]));
			if (werbserv_context.client[i].tcp_client &&
				!werbserv_context.client[i].sending &&
				(werbserv_context.client[i].buff_len - werbserv_context.client[i].buff_p) > 0)
				send = true;
		WC_UNLOCK(&(werbserv_context.client[i]));
		if (send)
			ws_tcp_send(&(werbserv_context.client[i]), werbserv_context.client[i].tcp_client);
	}
}

void webserv_run(void)
{
	static bool connected;

	if (!werbserv_context.wh_count)
		return;

	if (!werbserv_context.init) {
		werbserv_context.init = webserv_open();
		if (!werbserv_context.init)
			return;
	}

	if (!wifi_is_connected()) {
		if (connected) {
			webserv_reconnect();
			connected = false;
		}
		return;
	}

	connected = true;
	webclient_close_check();
	webclient_send_poll();
}

int webserv_port(void)
{
	return werbserv_context.port;
}
