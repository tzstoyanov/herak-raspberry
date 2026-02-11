// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "lwip/inet.h"

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define WOL_MODULE	"wol"

struct wol_context_t {
	sys_module_t mod;
	struct udp_pcb *log_pcb;
	uint32_t debug;
	int err_c;
	int ok_c;
};

static struct wol_context_t *__wol_context_global;

static struct wol_context_t *wol_get_context(void)
{
	return __wol_context_global;
}

static bool wol_init(struct wol_context_t **ctx)
{
	(*ctx) = calloc(1, sizeof(struct wol_context_t));
	if (!(*ctx))
		return false;
	__wol_context_global = (*ctx);

	return true;
}

static void wol_debug_set(uint32_t lvl, void *context)
{
	struct wol_context_t *ctx = (struct wol_context_t *)context;

	ctx->debug = lvl;
}

static bool wol_log_status(void *context)
{
	struct wol_context_t  *ctx = (struct wol_context_t *)context;

	if (!ctx->err_c && !ctx->ok_c)
		hlog_info(WOL_MODULE, "Wake on LAN sender is active ...");
	else
		hlog_info(WOL_MODULE, "Send %d/%d WoL packets", ctx->ok_c, ctx->ok_c + ctx->err_c);


	return true;
}

#define WOL_PACKET_LEN	(17*6)
#define WOL_PORT		9
static int wol_packet_send(uint8_t *mac)
{
	struct wol_context_t *ctx = wol_get_context();
	ip_addr_t wol_addr;
	struct pbuf *p;
	err_t err;
	int i, j;

	if (!ctx->log_pcb) {
		LWIP_LOCK_START;
			ctx->log_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
		LWIP_LOCK_END;
	}
	if (!ctx->log_pcb)
		goto out_err;
	LWIP_LOCK_START;
		p = pbuf_alloc(PBUF_TRANSPORT, WOL_PACKET_LEN, PBUF_RAM);
	LWIP_LOCK_END;
	if (!p)
		goto out_err;

	wol_addr.addr = 0xFFFFFFFF;
	for (i = 0; i < 6; i++)
		((uint8_t *)p->payload)[i] = 0xFF;
	for (i = 1; i <= 16; i++)
		for (j = 0; j < 6; j++)
			((uint8_t *)p->payload)[i * 6 + j] = mac[j];
	LWIP_LOCK_START;
		err = udp_sendto(ctx->log_pcb, p, &wol_addr, WOL_PORT);
		pbuf_free(p);
	LWIP_LOCK_END;

	if (ctx->debug)
		hlog_info(WOL_MODULE, "Send WoL packet to %2X:%2X:%2X:%2X:%2X:%2X [%d]",
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], err);
	if (err != ERR_OK)
		goto out_err;

	ctx->ok_c++;
	return 0;

out_err:
	ctx->err_c++;
	return -1;
}

/*   API  */
int wol_send(uint8_t *mac)
{
	return wol_packet_send(mac);
}

static int cmd_wol_send(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct wol_context_t *wctx = (struct wol_context_t *)user_data;
	char *m, *rest;
	uint8_t mac[6];
	int i = 0;
	int ret;

	UNUSED(ctx);
	UNUSED(cmd);
	UNUSED(wctx);

	if (!params || params[0] != ':' || strlen(params) < 11) {
		hlog_info(WOL_MODULE, "Invalid parameter ...");
		return 0;
	}

	m = strtok_r(params, ":", &rest);
	do {
		if (!m)
			break;
		mac[i++] = (int)strtol(m, NULL, 16);
		m = strtok_r(rest, ":", &rest);
	} while (i < 6);

	if (i != 6) {
		hlog_info(WOL_MODULE, "Invalid MAC address ...");
		return 0;
	}

	ret = wol_packet_send(mac);
	if (ret)
		hlog_info(WOL_MODULE, "Failed to send WoL packet.");
	else
		hlog_info(WOL_MODULE, "WoL packet sent.");

	return 0;
}

static app_command_t wol_requests[] = {
	{"send", ":<mac_address>",	cmd_wol_send},
};

void sys_wol_register(void)
{
	struct wol_context_t  *ctx = NULL;

	if (!wol_init(&ctx))
		return;

	ctx->mod.name = WOL_MODULE;
	ctx->mod.log = wol_log_status;
	ctx->mod.debug = wol_debug_set;
	ctx->mod.commands.hooks = wol_requests;
	ctx->mod.commands.count = ARRAY_SIZE(wol_requests);
	ctx->mod.commands.description = "Wake on LAN";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
