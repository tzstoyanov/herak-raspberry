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

#include "base64.h"
#include "params.h"

#define MAX_WIFI_NETS	3
#define CONNECT_TIMEOUT_MS	30000
#define WIFILOG	"wifi"

struct wifi_net_t {
	char *ssid;
	char *pass;
	bool connected;
};

static struct {
	absolute_time_t connect_time;
	bool connect_in_progress;
	int net_id;
	struct wifi_net_t *all_nets[MAX_WIFI_NETS];
} wifi_context;

void get_wifi_networks(void)
{
	char *rest;
	char *tok;
	int idx;

	if (WIFI_SSD_len < 1)
		return;

	rest = param_get(WIFI_SSD);
	idx = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && idx < MAX_WIFI_NETS) {
		wifi_context.all_nets[idx] = malloc(sizeof(struct wifi_net_t));
		if (!wifi_context.all_nets[idx])
			return;
		memset(wifi_context.all_nets[idx], 0, sizeof(struct wifi_net_t));
		wifi_context.all_nets[idx]->ssid = tok;
		idx++;
	}
	if (!idx)
		return;
	hlog_info(WIFILOG, "Got %d wifi networks", idx);
	rest = param_get(WIFI_PASS);
	idx = 0;
	while ((tok = strtok_r(rest, ";", &rest)) && idx < MAX_WIFI_NETS)
		wifi_context.all_nets[idx++]->pass = tok;
}

static bool wifi_log_status(void *context)
{
	int i;

	UNUSED(context);

	if (wifi_context.net_id < 0 || wifi_context.net_id  > MAX_WIFI_NETS ||
		(wifi_context.all_nets[wifi_context.net_id] &&
		wifi_context.all_nets[wifi_context.net_id]->connected == false)) {

		hlog_info(WIFILOG, "Not connected to a WiFi network, looking for:");
		for (i = 0; i < MAX_WIFI_NETS; i++) {
			if (wifi_context.all_nets[i])
				hlog_info(WIFILOG, "\t%s", wifi_context.all_nets[i]->ssid);
		}
		return true;
	}

	hlog_info(WIFILOG, "Connected to %s -> %s", wifi_context.all_nets[wifi_context.net_id]->ssid, inet_ntoa(cyw43_state.netif[0].ip_addr));

	return true;
}


bool wifi_init(void)
{
	int i = 0;

	memset(&wifi_context, 0, sizeof(wifi_context));
	get_wifi_networks();
	while (i < MAX_WIFI_NETS && wifi_context.all_nets[i]) {
		hlog_info(WIFILOG, "  [%s]", wifi_context.all_nets[i]->ssid);
		i++;
	}
	if (!i)
		return false;

	wifi_context.connect_time = nil_time;
	wifi_context.connect_in_progress = false;
	wifi_context.net_id = -1;

	add_status_callback(wifi_log_status, NULL);

	return true;
}

bool wifi_is_connected(void)
{
	bool bret;

	if (!wifi_context.all_nets[0])
		return false;
	LWIP_LOCK_START;
		bret = (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP);
	LWIP_LOCK_END;
	return bret;
}

void wifi_connect(void)
{
	int ret;

	if (wifi_is_connected()) {
		if (wifi_context.connect_in_progress) {
			hlog_info(WIFILOG, "Connected to %s -> got %s", wifi_context.all_nets[wifi_context.net_id]->ssid, inet_ntoa(cyw43_state.netif[0].ip_addr));
			system_reconnect();
		}
		wifi_context.connect_in_progress = false;
		wifi_context.all_nets[wifi_context.net_id]->connected = true;
		return;
	}

	if (!wifi_context.connect_in_progress) {
		if (wifi_context.net_id >= 0 && wifi_context.net_id < MAX_WIFI_NETS && wifi_context.all_nets[wifi_context.net_id])
			wifi_context.all_nets[wifi_context.net_id]->connected = false;

		wifi_context.net_id++;
		if (wifi_context.net_id < 0 || wifi_context.net_id >= MAX_WIFI_NETS || !wifi_context.all_nets[wifi_context.net_id])
			wifi_context.net_id = 0;
		LWIP_LOCK_START;
			ret = cyw43_arch_wifi_connect_async(wifi_context.all_nets[wifi_context.net_id]->ssid,
												wifi_context.all_nets[wifi_context.net_id]->pass,
												CYW43_AUTH_WPA2_AES_PSK);
		LWIP_LOCK_END;
		if (ret) {
			hlog_info(WIFILOG, "FAILED to start wifi scan for %s: %d", wifi_context.all_nets[wifi_context.net_id]->ssid, ret);
		} else {
			wifi_context.connect_in_progress = true;
			wifi_context.connect_time = make_timeout_time_ms(CONNECT_TIMEOUT_MS);
			hlog_info(WIFILOG, "Connecting to %s ...", wifi_context.all_nets[wifi_context.net_id]->ssid);
		}
	} else if (absolute_time_diff_us(get_absolute_time(), wifi_context.connect_time) < 0) {
		wifi_context.connect_in_progress = false;
		LWIP_LOCK_START;
			ret = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
		LWIP_LOCK_END;
		hlog_info(WIFILOG, "TimeOut connecting to %s: %d",
				wifi_context.all_nets[wifi_context.net_id]->ssid, ret);
	}

}
