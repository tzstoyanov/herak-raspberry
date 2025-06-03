// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/inet.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "herak_sys.h"
#include "base64.h"
#include "params.h"

#define MAX_WIFI_NETS	3
#define CONNECT_TIMEOUT_MS	30000
#define WIFI_MODULE	"wifi"

struct wifi_net_t {
	char *ssid;
	char *pass;
	bool connected;
};

struct wifi_context_t {
	sys_module_t mod;
	absolute_time_t connect_time;
	bool connect_in_progress;
	int net_id;
	struct wifi_net_t *all_nets[MAX_WIFI_NETS];
	uint32_t debug;
};

static struct wifi_context_t *__wifi_context;

static struct wifi_context_t *wifi_context_get(void)
{
	return __wifi_context;
}

static bool get_wifi_cfg(struct wifi_context_t **ctx)
{
	char *rest;
	char *tok;
	int idx;
	int i;

	if (WIFI_SSD_len < 1)
		return false;
	(*ctx) = (struct wifi_context_t *)calloc(1, sizeof(struct wifi_context_t));
	if (!(*ctx))
		return false;
	rest = param_get(WIFI_SSD);
	idx = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && idx < MAX_WIFI_NETS) {
		(*ctx)->all_nets[idx] = (struct wifi_net_t *)calloc(1, sizeof(struct wifi_net_t));
		if (!(*ctx)->all_nets[idx])
			goto out_err;
		(*ctx)->all_nets[idx]->ssid = tok;
		idx++;
	}
	if (!idx)
		goto out_err;
	hlog_info(WIFI_MODULE, "Got %d wifi networks", idx);
	rest = param_get(WIFI_PASS);
	idx = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && idx < MAX_WIFI_NETS)
		(*ctx)->all_nets[idx++]->pass = tok;

	return true;
out_err:
	for (i = 0; i < MAX_WIFI_NETS; i++) {
		if ((*ctx)->all_nets[i])
			free((*ctx)->all_nets[i]);
	}
	free((*ctx));
	(*ctx) = NULL;
	return false;
}

static bool sys_wifi_log_status(void *context)
{
	struct wifi_context_t *ctx = (struct wifi_context_t *)context;
	int i;

	UNUSED(context);

	if (ctx->net_id < 0 || ctx->net_id  > MAX_WIFI_NETS ||
		(ctx->all_nets[ctx->net_id] &&
		ctx->all_nets[ctx->net_id]->connected == false)) {

		hlog_info(WIFI_MODULE, "Not connected to a WiFi network, looking for:");
		for (i = 0; i < MAX_WIFI_NETS; i++) {
			if (ctx->all_nets[i])
				hlog_info(WIFI_MODULE, "\t%s", ctx->all_nets[i]->ssid);
		}
		return true;
	}

	hlog_info(WIFI_MODULE, "Connected to %s -> %s", ctx->all_nets[ctx->net_id]->ssid, inet_ntoa(cyw43_state.netif[0].ip_addr));

	return true;
}


static bool sys_wifi_init(struct wifi_context_t **ctx)
{
	int i = 0;

	if (!get_wifi_cfg(ctx))
		return false;

	while (i < MAX_WIFI_NETS && (*ctx)->all_nets[i]) {
		hlog_info(WIFI_MODULE, "  [%s]", (*ctx)->all_nets[i]->ssid);
		i++;
	}
	if (!i)
		return false;

	(*ctx)->connect_time = nil_time;
	(*ctx)->connect_in_progress = false;
	(*ctx)->net_id = -1;
	__wifi_context = (*ctx);
	return true;
}

bool wifi_is_connected(void)
{
	struct wifi_context_t *ctx = wifi_context_get();
	bool bret;

	if (!ctx || !ctx->all_nets[0])
		return false;

	LWIP_LOCK_START;
		bret = (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP);
	LWIP_LOCK_END;

	return bret;
}

static void sys_wifi_connect(void *context)
{
	struct wifi_context_t *ctx = (struct wifi_context_t *)context;
	int ret;

	if (wifi_is_connected()) {
		if (ctx->connect_in_progress) {
			hlog_info(WIFI_MODULE, "Connected to %s -> got %s", ctx->all_nets[ctx->net_id]->ssid, inet_ntoa(cyw43_state.netif[0].ip_addr));
			system_reconnect();
		}
		ctx->connect_in_progress = false;
		ctx->all_nets[ctx->net_id]->connected = true;
		return;
	}

	if (!ctx->connect_in_progress) {
		if (ctx->net_id >= 0 && ctx->net_id < MAX_WIFI_NETS && ctx->all_nets[ctx->net_id])
			ctx->all_nets[ctx->net_id]->connected = false;

		ctx->net_id++;
		if (ctx->net_id < 0 || ctx->net_id >= MAX_WIFI_NETS || !ctx->all_nets[ctx->net_id])
			ctx->net_id = 0;
		LWIP_LOCK_START;
			ret = cyw43_arch_wifi_connect_async(ctx->all_nets[ctx->net_id]->ssid,
												ctx->all_nets[ctx->net_id]->pass,
												CYW43_AUTH_WPA2_AES_PSK);
		LWIP_LOCK_END;
		if (ret) {
			hlog_info(WIFI_MODULE, "FAILED to start wifi scan for %s: %d", ctx->all_nets[ctx->net_id]->ssid, ret);
		} else {
			ctx->connect_in_progress = true;
			ctx->connect_time = make_timeout_time_ms(CONNECT_TIMEOUT_MS);
			hlog_info(WIFI_MODULE, "Connecting to %s ...", ctx->all_nets[ctx->net_id]->ssid);
		}
	} else if (absolute_time_diff_us(get_absolute_time(), ctx->connect_time) < 0) {
		ctx->connect_in_progress = false;
		LWIP_LOCK_START;
			ret = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
		LWIP_LOCK_END;
		hlog_info(WIFI_MODULE, "TimeOut connecting to %s: %d",
				ctx->all_nets[ctx->net_id]->ssid, ret);
	}
}

static void sys_wifi_debug_set(uint32_t lvl, void *context)
{
	struct wifi_context_t *ctx = (struct wifi_context_t *)context;

	ctx->debug = lvl;
}

void sys_wifi_register(void)
{
	struct wifi_context_t *ctx = NULL;

	if (!sys_wifi_init(&ctx))
		return;

	ctx->mod.name = WIFI_MODULE;
	ctx->mod.run = sys_wifi_connect;
	ctx->mod.log = sys_wifi_log_status;
	ctx->mod.debug = sys_wifi_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}