// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"

#include "solar.h"

#define WH_DEFAULT_PORT	80
#define WH_HTTP_CMD		"POST"
#define WH_HTTP_TYPE	"application/json"

#define	WH_PAYLOAD_TEMPLATE "{ \"temperature\":%d}"
#define	WH_PAYLOAD_MAX_SIZE	32

#define WH_SEND_DELAY_MS	5000

#define WHLOG	"notify"
#define HTTP_OK	200

static struct {
	int wh_idx;
	uint64_t last_send;
} wh_notify_context;

static bool wh_notify_get_config(char **server, char **endpoint, int *port)
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

int wh_notify(int level)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE, level);

	if (wh_notify_context.wh_idx >= 0)
		return webhook_send(wh_notify_context.wh_idx, notify_buff, strlen(notify_buff));

	return -1;
}

void wh_notify_send(void)
{
	uint64_t now;

	now = time_ms_since_boot();
	if ((now - wh_notify_context.last_send) > WH_SEND_DELAY_MS) {
		wh_notify(temperature_internal_get());
		wh_notify_context.last_send = now;
	}
}

static void wh_callback(int idx, int http_code, void *context)
{
	UNUSED(idx);
	UNUSED(context);
	switch (http_code) {
	case 0:
		hlog_info(WHLOG, "http timeout");
		break;
	case HTTP_OK:
		break;
	default:
		hlog_info(WHLOG, "http error [%d]", http_code);
		break;
	}
}

bool wh_notify_init(void)
{
	char *server, *endpoint;
	int port;

	memset(&wh_notify_context, 0, sizeof(wh_notify_context));

	if (!wh_notify_get_config(&server, &endpoint, &port))
		return false;
	wh_notify_context.wh_idx = webhook_add(server, port, WH_HTTP_TYPE, endpoint, WH_HTTP_CMD, true, wh_callback, NULL);
	free(server);
	free(endpoint);
	if (wh_notify_context.wh_idx < 0)
		return false;

	return true;
}
