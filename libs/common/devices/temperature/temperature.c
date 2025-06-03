// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "base64.h"
#include "params.h"

#define CHIP_T_MODULE	"chip_temperature"

#define ADC_INTERNAL_TEMP	4
#define ADC_CONVERS			(3.28f / (1 << 12))

/* For each measurements, take 50 samples */
#define ADC_MEASURE_COUNT	50
/* Filter out the 5 biggest and the 5 smallest */
#define ADC_MEASURE_DROP	5

struct temperature_chip_t {
	sys_module_t mod;
	uint32_t		samples[ADC_MEASURE_COUNT];
	float			temp_internal;
	int				count;
};

static struct temperature_chip_t *__temperature_chip;

struct temperature_chip_t *temperature_chip_context_get(void)
{
	return __temperature_chip;
}

static bool temperature_init(struct temperature_chip_t **ctx)
{
	(*ctx) = (struct temperature_chip_t *)calloc(1, sizeof(struct temperature_chip_t));
	if (!(*ctx))
		return false;

	adc_init();
	adc_set_round_robin(0);
	adc_set_temp_sensor_enabled(true);

	__temperature_chip = (*ctx);

	return true;
}

float temperature_internal_get(void)
{
	struct temperature_chip_t *ctx = temperature_chip_context_get();

	if (!ctx)
		return 0;
	return ctx->temp_internal;
}

static void temperature_measure(void *context)
{
	struct temperature_chip_t *ctx = (struct temperature_chip_t *)context;
	float result_tmp;
	uint32_t av;
	int i;

	adc_select_input(ADC_INTERNAL_TEMP);
	/* read the samples */
	for (i = 0; i < ADC_MEASURE_COUNT; i++)
		ctx->samples[i] = adc_read();
	/* filter biggest and smallest */
	av = samples_filter(ctx->samples, ADC_MEASURE_COUNT, ADC_MEASURE_DROP);

	result_tmp = av * ADC_CONVERS;
	/* Formula from the Pico  C/C++ SDK Manual */
	ctx->temp_internal = 27 - (result_tmp - 0.706) / 0.001721;
}

void chip_temperature_register(void)
{
	struct temperature_chip_t *ctx = NULL;

	if (!temperature_init(&ctx))
		return;

	ctx->mod.name = CHIP_T_MODULE;
	ctx->mod.run = temperature_measure;
	ctx->mod.commands.description = "Internal chip temperature";
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
