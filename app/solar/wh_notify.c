// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "pico/stdlib.h"

#include "solar.h"

#define	WH_PAYLOAD_TEMPLATE "temperature: %3.2f"
#define WH_SEND_DELAY_MS	5000
#define WHLOG	"notify"

void wh_notify_send(void)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];
	static uint64_t last_send;
	uint64_t now;

	if (!webhook_connected())
		return;
	now = time_ms_since_boot();
	if ((now - last_send) > WH_SEND_DELAY_MS) {
		snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE, temperature_internal_get());
		webhook_send(notify_buff);
		last_send = now;
	}
}

