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
	struct ssr_t relays[MAX_SSR_COUNT];
} ssr_context;

void ssr_reset_all(void)
{
	int off = !ssr_context.on_state;
	int i;

	for (i = 0; i < ssr_context.count; i++) {
		gpio_put(ssr_context.relays[i].gpio_pin, off);
		ssr_context.relays[i].state = off;
		ssr_context.relays[i].time_ms = 0;
		ssr_context.relays[i].delay_ms = 0;
		ssr_context.relays[i].last_switch = to_ms_since_boot(get_absolute_time());
	}

}

int ssr_state_set(uint8_t id, bool state, uint32_t time, uint32_t delay)
{
	int value = state ? ssr_context.on_state : !ssr_context.on_state;

	if (id >= ssr_context.count)
		return -1;
	if (!delay)
		gpio_put(ssr_context.relays[id].gpio_pin, value);
	ssr_context.relays[id].state = value;
	ssr_context.relays[id].time_ms = time;
	ssr_context.relays[id].delay_ms = delay;
	ssr_context.relays[id].last_switch = to_ms_since_boot(get_absolute_time());

	return 0;
}

bool ssr_log(void *context)
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

	return true;
}

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
	if (tok && strlen(tok) >= 1)
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
