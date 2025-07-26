// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "math.h"

#include "common_internal.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "base64.h"
#include "params.h"

#define TEMP_MODULE	"temperature"
#define READ_INTERVAL_MS	500

#define ADC_REF_VOLT	3.3f
#define ADC_MAX			(1 << 12)
#define ADC_INTERNAL_TEMP	4
#define ADC_CONVERS(V)		(((float)(V))*(ADC_REF_VOLT / ((float)ADC_MAX)))

#define T_KELVIN		273.15f
#define T_KELVIN_25		(T_KELVIN + 25.0f)
/* For each measurements, take 50 samples */
#define ADC_MEASURE_COUNT	50
/* Filter out the 5 biggest and the 5 smallest */
#define ADC_MEASURE_DROP	5

#define MAX_SENSORS		5
#define MQTT_DATA_LEN   128
#define MQTT_DELAY_MS	5000

#define NTC_PULLUP_RES		5000.0f

#define IS_DEBUG(C)	((C)->debug)

static struct {
	int gp_id;
	int adc_id;
} adc_mapping[] = {
		{26, 0},
		{27, 1},
		{28, 2},
		{29, 3},
		{-1, 4},	// Input 4 is the onboard temperature sensor.
};

enum {
	TEMPERATURE_TYPE_INTERNAL,
	TEMPERATURE_TYPE_NTC,
};

struct temperature_t;
typedef float (*temperature_calc_cb_t) (struct temperature_t *temp, float v);

struct temperature_ntc_t {
	float nominal;		// NTC resistance @ 25*C
	float coefficient;	// NTC Beta coefficient
};

struct temperature_t {
	uint32_t	samples[ADC_MEASURE_COUNT];
	float		min;
	float		max;
	float		temperature;
	uint64_t	last_read;
	uint		count;
	uint		adc_id;
	int			type;
	void		*params;
	temperature_calc_cb_t calc;
	mqtt_component_t mqtt_comp;
};

struct temperature_context_t {
	sys_module_t mod;
	uint32_t debug;
	int idx;
	int count;
	struct temperature_t *sensors[MAX_SENSORS];
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static struct temperature_context_t *__temperature_ctx;

static struct temperature_context_t *temperature_context_get(void)
{
	return __temperature_ctx;
}

static const char *temperature_type_str(int type)
{
	switch (type) {
	case TEMPERATURE_TYPE_INTERNAL:
		return "chip";
	case TEMPERATURE_TYPE_NTC:
		return "ntc";
	default:
		break;
	}
	return "uknown";
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int temperature_mqtt_data_send(struct temperature_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	mqtt_component_t *ms;
	int count = 0;
	int ret = -1;

	ms = &ctx->sensors[idx]->mqtt_comp;
	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"%s\": \"%3.2f\"",
						 ctx->sensors[idx]->mqtt_comp.name,
						 ctx->sensors[idx]->temperature);
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ms, ctx->mqtt_payload);

	if (!ret)
		ctx->mqtt_last_send = now;

	return ret;
}

static int temperature_add_sensor(struct temperature_context_t *ctx, int gpio_pin,
								  int type, float min, float max,
								  temperature_calc_cb_t calc, void *params)
{
	uint i;

	if (ctx->count >= MAX_SENSORS)
		return -1;

	for (i = 0; i < ARRAY_SIZE(adc_mapping); i++) {
		if (adc_mapping[i].gp_id == gpio_pin)
			break;
	}

	if (i >= ARRAY_SIZE(adc_mapping))
		return -1;

	ctx->sensors[ctx->count] = calloc(1, sizeof(struct temperature_t));
	if (!ctx->sensors[ctx->count])
		return -1;

	ctx->sensors[ctx->count]->type = type;
	ctx->sensors[ctx->count]->adc_id = adc_mapping[i].adc_id;
	ctx->sensors[ctx->count]->min = min;
	ctx->sensors[ctx->count]->max = max;
	ctx->sensors[ctx->count]->calc = calc;
	ctx->sensors[ctx->count]->params = params;

	ctx->sensors[ctx->count]->mqtt_comp.module = TEMP_MODULE;
	ctx->sensors[ctx->count]->mqtt_comp.platform = "sensor";
	ctx->sensors[ctx->count]->mqtt_comp.dev_class = "temperature";
	ctx->sensors[ctx->count]->mqtt_comp.unit = "°C";
	sys_asprintf(&ctx->sensors[ctx->count]->mqtt_comp.name, "temperature_%s_%d",
				 temperature_type_str(type), ctx->count);
	sys_asprintf(&ctx->sensors[ctx->count]->mqtt_comp.value_template, "{{ value_json.%s }}",
				 ctx->sensors[ctx->count]->mqtt_comp.name);
	if (ctx->count > 1)
		ctx->sensors[ctx->count]->mqtt_comp.state_topic = ctx->sensors[0]->mqtt_comp.state_topic;
	mqtt_msg_component_register(&(ctx->sensors[ctx->count]->mqtt_comp));

	ctx->count++;

	return ctx->count - 1;
}

float temperature_calc_internal(struct temperature_t *sensor, float v)
{
	UNUSED(sensor);

	/* Formula from the Pico  C/C++ SDK Manual */
	return (27 - (v - 0.706) / 0.001721);
}

float temperature_calc_ntc(struct temperature_t *sensor, float v)
{
	struct temperature_ntc_t *ntc = (struct temperature_ntc_t *)sensor->params;
	float res;

	// Calculate Resistance
	res = NTC_PULLUP_RES * (ADC_REF_VOLT / v - 1);
	res = (1 / ((1 / T_KELVIN_25) + (-1 / ntc->coefficient) * logf(res / ntc->nominal)));
	return (res - T_KELVIN);
}

// <gpio>:<nominal>:<const>;
static void temperature_init_ntc(struct temperature_context_t *ctx)
{
	char *ntc_cfg = param_get(TEMPERATURE_NTC);
	struct temperature_ntc_t *params;
	char *rest1, *tok1;
	char *rest, *tok;
	char *tok_map[3];
	int pin, i;

	if ((!ntc_cfg || strlen(ntc_cfg) < 1))
		return;

	rest = ntc_cfg;
	while ((tok = strtok_r(rest, ";", &rest))) {
		i = 0;
		rest1 = tok;
		while (i < 3 && (tok1 = strtok_r(rest1, ":", &rest1)))
			tok_map[i++] = tok;
		if (i < 3)
			continue;
		pin = (int)strtol(tok_map[0], NULL, 10);
		if (pin < 0 || pin >= GPIO_PIN_MAX)
			continue;
		params = calloc(1, sizeof(struct temperature_ntc_t));
		if (!params)
			continue;
		params->nominal = (int)strtol(tok_map[1], NULL, 0);
		params->coefficient = (int)strtol(tok_map[2], NULL, 0);
		temperature_add_sensor(ctx, pin, TEMPERATURE_TYPE_NTC,
							   -30.0, 60.0, temperature_calc_ntc, params);
		adc_gpio_init(pin);
	}
}

static bool temperature_init(struct temperature_context_t **ctx)
{
	(*ctx) = (struct temperature_context_t *)calloc(1, sizeof(struct temperature_context_t));
	if (!(*ctx))
		return false;

	if (temperature_add_sensor((*ctx), -1, TEMPERATURE_TYPE_INTERNAL,
							   -30.0, 60.0, temperature_calc_internal, NULL) != 0)
		goto out_err;
	adc_init();
	adc_irq_set_enabled(false);
	adc_run(false);
	adc_fifo_drain();
	adc_set_temp_sensor_enabled(true);

	temperature_init_ntc(*ctx);

	__temperature_ctx = (*ctx);

	return true;
out_err:
	free(*ctx);
	*ctx = NULL;
	return false;
}

float temperature_internal_get(void)
{
	struct temperature_context_t *ctx = temperature_context_get();

	if (!ctx)
		return 0;
	return ctx->sensors[0]->temperature;
}

static void temperature_measure(struct temperature_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	struct temperature_t *sensor;
	uint32_t av;
	float vadc;
	float temp;
	int i;

	if (ctx->idx >= ctx->count)
		ctx->idx = 0;
	sensor = ctx->sensors[ctx->idx];
	if (now - sensor->last_read < READ_INTERVAL_MS)
		goto out;
	adc_select_input(sensor->adc_id);
	if (adc_get_selected_input() != sensor->adc_id)
		goto out;
	adc_read();
	sleep_us(100);
	for (i = 0; i < ADC_MEASURE_COUNT; i++) {
		sensor->samples[i] = adc_read();
		sleep_us(20);
	}

	av = samples_filter(sensor->samples, ADC_MEASURE_COUNT, ADC_MEASURE_DROP);
	vadc = ADC_CONVERS(av);
	temp = sensor->calc(sensor, vadc);
	if (temp < sensor->min || temp > sensor->max)
		return;
	if (sensor->temperature != temp) {
		sensor->mqtt_comp.force = true;
		sensor->temperature = temp;
	}

	sensor->last_read = now;
	if (IS_DEBUG(ctx))
		hlog_info(TEMP_MODULE, "Measured [%s]: %3.2f*C / %3.2fV",
				  temperature_type_str(sensor->type), sensor->temperature, vadc);
out:
	ctx->idx++;
}

static void temperature_mqtt_send(struct temperature_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	static int idx;
	int i;

	for (i = 0; i < ctx->count; i++) {
		if (ctx->sensors[i]->mqtt_comp.force == true) {
			temperature_mqtt_data_send(ctx, i);
			return;
		}
	}

	if ((now - ctx->mqtt_last_send) < MQTT_DELAY_MS)
		return;
	if (idx >= ctx->count)
		idx = 0;
	temperature_mqtt_data_send(ctx, idx++);
}

static void temperature_run(void *context)
{
	struct temperature_context_t *ctx = (struct temperature_context_t *)context;

	temperature_measure(ctx);
	temperature_mqtt_send(ctx);
}

static void temperature_debug_set(uint32_t debug, void *context)
{
	struct temperature_context_t *ctx = (struct temperature_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

static bool temperature_log(void *context)
{
	struct temperature_context_t *ctx = (struct temperature_context_t *)context;
	int i;

	hlog_info(TEMP_MODULE, "Sensors:");
	for (i = 0; i < ctx->count; i++) {
		if (!ctx->sensors[i])
			continue;
		hlog_info(TEMP_MODULE, "\t[%s]: %3.2f",
				  temperature_type_str(ctx->sensors[i]->type), ctx->sensors[i]->temperature);
	}

	return true;
}

void temperature_register(void)
{
	struct temperature_context_t *ctx = NULL;

	if (!temperature_init(&ctx))
		return;

	ctx->mod.name = TEMP_MODULE;
	ctx->mod.run = temperature_run;
	ctx->mod.log = temperature_log;
	ctx->mod.debug = temperature_debug_set;
	ctx->mod.context = ctx;

	sys_module_register(&ctx->mod);
}
