// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "irrig.h"

#define SSRLOG	"ssr"
#define SSR_URL	"/ssr"
#define SSR_DESC	"Solid State Relay controls"
#define WEB_DATA_LEN	64
#define SSR_STATE_DONE "done"

struct ssr_t {
	int gpio_pin;
	bool state;
	uint32_t last_switch;
	uint32_t time_ms;
};

static struct {
	int count;
	uint8_t on_state;
	uint32_t state;
	int hindex;
	struct ssr_t relays[MAX_SSR_COUNT];
} ssr_context;

static void ssr_state_set(uint8_t id, bool value, uint32_t time)
{
	if (id < ssr_context.count) {
		gpio_put(ssr_context.relays[id].gpio_pin, value);
		ssr_context.relays[id].state = value;
		ssr_context.relays[id].time_ms = time;
		ssr_context.relays[id].last_switch = to_ms_since_boot(get_absolute_time());
	}
}

#define STATUS_STR "\tSSR status: \r\n"
static void ssr_status(int client_idx, char *params, void *user_data)
{
	char buff[WEB_DATA_LEN + 1];

	UNUSED(params);
	UNUSED(user_data);
	int count, i;
	uint32_t now, delta;

	now = to_ms_since_boot(get_absolute_time());
	snprintf(buff, WEB_DATA_LEN, "%s", STATUS_STR);
	weberv_client_send(client_idx, buff, strlen(buff), HTTP_RESP_OK);
	for (i = 0; i < ssr_context.count; i++) {
		count = snprintf(buff, WEB_DATA_LEN, "\t\tssr %d: %s;",
				i, ssr_context.state & (1 << i)?"On":"Off");
		if (ssr_context.relays[i].time_ms > 0) {
			delta = now - ssr_context.relays[i].last_switch;
			delta = (ssr_context.relays[i].time_ms - delta)/1000;
			count += snprintf(buff + count, WEB_DATA_LEN - count, " remaining time %lu sec\n", delta);
		} else {
			count += snprintf(buff + count, WEB_DATA_LEN - count, " unlimited\n");
		}
		buff[WEB_DATA_LEN] = 0;
		weberv_client_send_data(client_idx, buff, strlen(buff));
	}
	weberv_client_send(client_idx, SSR_STATE_DONE, strlen(SSR_STATE_DONE), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

#define SET_OK_STR "\tSSR switched.\r\n"
#define SET_ERR_STR "\tInvalid parameters.\r\n"
static void ssr_set(int client_idx, char *params, void *user_data)
{
	int id, state, time;
	char *rest, *tok;

	UNUSED(user_data);

	if (!params || params[0] != ':' || strlen(params) < 2)
		goto out_err;

	rest = params + 1;
	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		goto out_err;
	id = (int)strtol(tok, NULL, 10);
	if (id < 0 || id >= ssr_context.count)
		goto out_err;

	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		goto out_err;
	state = (int)strtol(tok, NULL, 10);

	tok = strtok_r(rest, ":", &rest);
	if (!tok) {
		time = 0;
	} else {
		time = (int)strtol(tok, NULL, 10);
		if (time < 0)
			time = 0;
		/* sec -> ms */
		time *= 1000;
	}

	ssr_state_set(id, state ? ssr_context.on_state : !ssr_context.on_state, time);

	weberv_client_send(client_idx, SET_OK_STR, strlen(SET_OK_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	return;

out_err:
	weberv_client_send(client_idx, SET_OK_STR, strlen(SET_ERR_STR), HTTP_RESP_BAD);
	weberv_client_close(client_idx);
}

static web_requests_t ssr_requests[] = {
		{"set", ":<ssr_id>:<state_0_1>:<time_sec>", ssr_set},
		{"status", NULL, ssr_status},
};

void ssr_run(void)
{
	uint32_t state = 0;
	bool notify = false;
	uint32_t now, delta;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < ssr_context.count; i++) {
		if (gpio_get(ssr_context.relays[i].gpio_pin) == ssr_context.on_state)
			state |= (1 << i);
		else
			state &= ~(1 << i);
		delta = 0;
		if (ssr_context.relays[i].time_ms > 0) {
			notify = true;
			delta = now - ssr_context.relays[i].last_switch;
			delta = ssr_context.relays[i].time_ms - delta;
			if (delta > ssr_context.relays[i].time_ms)
				ssr_state_set(i, !ssr_context.relays[i].state, 0);
		}
		mqtt_data_ssr_data(i, delta/1000);
	}
	if (state != ssr_context.state) {
		ssr_context.state = state;
		notify = true;
	}

	if (notify)
		mqtt_data_ssr_state(state);
}

int ssr_init(void)
{
	char *config = param_get(SSR);
	char *rest1, *tok1;
	char *tok_map[2];
	char *rest, *tok;
	int i, j;
	int id;

	memset(&ssr_context, 0, sizeof(ssr_context));
	if ((!config || strlen(config) < 1))
		goto out_error;

	for (i = 0 ; i < MAX_SSR_COUNT; i++)
		ssr_context.relays[i].gpio_pin = -1;

	tok = param_get(SSR_TRIGGER);
	if (tok && strlen(tok) > 1)
		ssr_context.on_state = (int)strtol(tok, NULL, 10);
	free(tok);

	i = 0;
	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		j = 0;
		rest1 = tok;
		while (j < 2 && (tok1 = strtok_r(rest1, ":", &rest1)))
			tok_map[j++] = tok;
		if (j < 2)
			continue;
		id = (int)strtol(tok_map[0], NULL, 10);
		if (id < 0 || id > MAX_SSR_COUNT)
			continue;
		ssr_context.relays[id].gpio_pin = (int)strtol(tok_map[1], NULL, 10);
		ssr_context.count++;
	}
	if (ssr_context.count < 1)
		goto out_error;

	for (i = 0; i < ssr_context.count; i++) {
		gpio_init(ssr_context.relays[i].gpio_pin);
		gpio_set_dir(ssr_context.relays[i].gpio_pin, GPIO_OUT);
		gpio_put(ssr_context.relays[i].gpio_pin, !ssr_context.on_state);
	}

	i = webserv_add_commands(SSR_URL, ssr_requests, ARRAY_SIZE(ssr_requests), SSR_DESC, NULL);
	if (i < 0)
		return false;
	ssr_context.hindex = i;

	free(config);
	hlog_info(SSRLOG, "%d Solid State Relays initialized", ssr_context.count);
	return ssr_context.count;

out_error:
	free(config);
	hlog_info(SSRLOG, "No valid configuration for SSRs");
	return 0;
}

uint32_t ssr_get_time(int id)
{
	if (id >= ssr_context.count)
		return 0;
	return ssr_context.relays[id].time_ms;
}

void ssr_log(void)
{
	uint32_t now, delta;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < ssr_context.count; i++) {
		if (ssr_context.relays[i].time_ms > 0) {
			delta = now - ssr_context.relays[i].last_switch;
			delta = (ssr_context.relays[i].time_ms - delta)/1000;
		}
		hlog_info(SSRLOG, "Relay %d: gpio %d [%s], time %lu sec, remains: %lu sec", i, ssr_context.relays[i].gpio_pin,
				  (ssr_context.state & (1 << i))?"ON":"OFF", ssr_context.relays[i].time_ms/1000,
				   ssr_context.relays[i].time_ms > 0 ? delta : 0);
	}
}
