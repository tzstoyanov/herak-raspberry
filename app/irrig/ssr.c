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
#define SSR_STATE_DONE "\r\n"

struct ssr_t {
	int gpio_pin;
	bool state;
	uint32_t last_switch;
	uint32_t time_ms;
	uint32_t delay_ms;
};

static struct {
	int count;
	uint8_t on_state;
	uint32_t state;
	int hindex;
	struct ssr_t relays[MAX_SSR_COUNT];
} ssr_context;

static void ssr_state_set(uint8_t id, bool value, uint32_t time, uint32_t delay)
{
	if (id < ssr_context.count) {
		if (!delay)
			gpio_put(ssr_context.relays[id].gpio_pin, value);
		ssr_context.relays[id].state = value;
		ssr_context.relays[id].time_ms = time;
		ssr_context.relays[id].delay_ms = delay;
		ssr_context.relays[id].last_switch = to_ms_since_boot(get_absolute_time());
	}
}

static void ssr_log(void *context)
{
	uint32_t now, delta_t, delta_d;
	int i;

	UNUSED(context);

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < ssr_context.count; i++) {
		delta_d = ssr_context.relays[i].delay_ms;
		delta_t = ssr_context.relays[i].time_ms;
		if (ssr_context.relays[i].delay_ms > 0) {
			delta_d = now - ssr_context.relays[i].last_switch;
			delta_d = (ssr_context.relays[i].delay_ms - delta_d);
		} else if (ssr_context.relays[i].time_ms > 0) {
			delta_t = now - ssr_context.relays[i].last_switch;
			delta_t = (ssr_context.relays[i].time_ms - delta_t);
		}
		hlog_info(SSRLOG, "Relay %d: gpio %d [%s]; delay: %lu/%lu sec, time %lu/%lu sec",
				  i, ssr_context.relays[i].gpio_pin, (ssr_context.state & (1 << i))?"ON":"OFF",
				  delta_d/1000, ssr_context.relays[i].delay_ms/1000,
				  delta_t/1000, ssr_context.relays[i].time_ms/1000);
	}
}

#define STATUS_STR "\tSSR status: \r\n"
static void ssr_status(int client_idx, char *params, void *user_data)
{
	UNUSED(params);
	UNUSED(user_data);

	weberv_client_send(client_idx, STATUS_STR, strlen(STATUS_STR), HTTP_RESP_OK);
	debug_log_forward(client_idx);
	ssr_log(NULL);
	debug_log_forward(-1);
	weberv_client_send(client_idx, SSR_STATE_DONE, strlen(SSR_STATE_DONE), HTTP_RESP_OK);
	weberv_client_close(client_idx);
}

#define SET_OK_STR "\tSSR switched.\r\n"
#define SET_ERR_STR "\tInvalid parameters.\r\n"
static void ssr_set(int client_idx, char *params, void *user_data)
{
	int id, state, time, delay;
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

	/* Get state time */
	time = 0;
	delay = 0;
	tok = strtok_r(rest, ":", &rest);
	if (tok) {
		time = (int)strtol(tok, NULL, 10);
		if (time < 0)
			time = 0;
		/* sec -> ms */
		time *= 1000;

		/* Get delay */
		tok = strtok_r(rest, ":", &rest);
		if (tok) {
			delay = (int)strtol(tok, NULL, 10);
			if (delay < 0)
				delay = 0;
			/* sec -> ms */
			delay *= 1000;
		}
	}

	ssr_state_set(id, state ? ssr_context.on_state : !ssr_context.on_state, time, delay);

	weberv_client_send(client_idx, SET_OK_STR, strlen(SET_OK_STR), HTTP_RESP_OK);
	weberv_client_close(client_idx);
	return;

out_err:
	weberv_client_send(client_idx, SET_OK_STR, strlen(SET_ERR_STR), HTTP_RESP_BAD);
	weberv_client_close(client_idx);
}

static web_requests_t ssr_requests[] = {
		{"set", ":<ssr_id>:<state_0_1>:<state_time_sec>:<delay_sec>", ssr_set},
		{"status", NULL, ssr_status},
};

void ssr_run(void)
{
	int delta_t, delta_d;
	bool notify = false;
	uint32_t state = 0;
	uint32_t now;
	int i;

	now = to_ms_since_boot(get_absolute_time());
	for (i = 0; i < ssr_context.count; i++) {
		if (gpio_get(ssr_context.relays[i].gpio_pin) == ssr_context.on_state)
			state |= (1 << i);
		else
			state &= ~(1 << i);
		delta_t = ssr_context.relays[i].time_ms;
		delta_d = ssr_context.relays[i].delay_ms;
		if (ssr_context.relays[i].delay_ms > 0) {
			notify = true;
			delta_d = now - ssr_context.relays[i].last_switch;
			delta_d = ssr_context.relays[i].delay_ms - delta_d;
			if (delta_d <= 0)
				ssr_state_set(i, ssr_context.relays[i].state, ssr_context.relays[i].time_ms, 0);
		} else if (ssr_context.relays[i].time_ms > 0) {
			notify = true;
			delta_t = now - ssr_context.relays[i].last_switch;
			delta_t = ssr_context.relays[i].time_ms - delta_t;
			if (delta_t <= 0)
				ssr_state_set(i, !ssr_context.relays[i].state, 0, 0);
		}
		mqtt_data_ssr_data(i, delta_t/1000, delta_d/1000);
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

	add_status_callback(ssr_log, NULL);

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
