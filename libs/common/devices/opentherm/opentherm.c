// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#include "opentherm.h"

static void opentherm_debug_set(uint32_t debug, void *context)
{
	opentherm_context_t *ctx = (opentherm_context_t *)context;

	ctx->log_mask = debug;
}

bool opentherm_log(void *context)
{
	opentherm_context_t *ctx = (opentherm_context_t *)context;
	static bool in_progress;

	if (!in_progress) {
		opentherm_dev_pio_log(&ctx->pio);
		in_progress = true;
	} else {
		in_progress = opentherm_dev_log(ctx);
	}

	return !in_progress;
}

static void opentherm_run(void *context)
{
	opentherm_context_t *ctx = (opentherm_context_t *)context;

	opentherm_dev_run(ctx);
	opentherm_mqtt_send(ctx);
}

static void opentherm_data_init(opentherm_data_t *data)
{
	data->status.ch_enabled = false;
	data->status.dhw_enabled = false;
	data->status.dhw_enabled = false;
	data->status.ch2_enabled = false;
	data->status.cooling_enabled = false;
	data->status.otc_active = false;
	data->param_desired.ch_temperature_setpoint = 25.0;
	data->param_desired.dhw_temperature_setpoint = 40.0;
	data->param_desired.ch_max = 40.0;
	data->param_desired.dhw_max = 60.0;
}

static int opentherm_config_get(opentherm_context_t **ctx)
{
	char *config = param_get(OPENTHERM_PINS);
	int rx_pin, tx_pin;
	char *rest, *tok;
	int ret = -1;

	if (!config || strlen(config) < 1)
		goto out;
	rest = config;
	tok = strtok_r(rest, ";", &rest);
	rx_pin = (int)strtol(tok, NULL, 10);
	if (rx_pin < GPIO_PIN_MIN || rx_pin > GPIO_PIN_MAX)
		goto out;
	tx_pin = (int)strtol(rest, NULL, 10);
	if (tx_pin < GPIO_PIN_MIN || tx_pin > GPIO_PIN_MAX)
		goto out;
	(*ctx) = calloc(1, sizeof(opentherm_context_t));
	if (!(*ctx))
		goto out;
	(*ctx)->pio.pio_rx.pin = rx_pin;
	(*ctx)->pio.pio_tx.pin = tx_pin;
	ret = 0;

out:
	free(config);
	return ret;
}

static bool opentherm_init(opentherm_context_t **ctx)
{
	if (opentherm_config_get(ctx))
		return false;

	if (opentherm_dev_pio_init(&(*ctx)->pio))
		goto out_err;

	opentherm_data_init(&((*ctx)->data));
	opentherm_dev_init(*ctx);
	opentherm_mqtt_init(*ctx);

	hlog_info(OTHM_MODULE, "Initialise successfully OpenTherm module");
	return true;

out_err:
	free(*ctx);
	*ctx = NULL;
	return false;
}

void opentherm_register(void)
{
	opentherm_context_t *ctx = NULL;

	if (!opentherm_init(&ctx))
		return;

	ctx->mod.name = OTHM_MODULE;
	ctx->mod.run = opentherm_run;
	ctx->mod.log = opentherm_log;
	ctx->mod.debug = opentherm_debug_set;
	ctx->mod.commands.hooks = opentherm_user_comands_get(&ctx->mod.commands.count);
	ctx->mod.commands.description = "OpenTherm control";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
