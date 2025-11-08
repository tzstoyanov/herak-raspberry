// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"
#include "stdatomic.h"

#include "pico/stdlib.h"
#include "pico/float.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define FLOW_YF_MODULE    "flow_yf"
#define MEASURE_TIME_MS 1000
#define YF_SENORS_MAX	6
#define MQTT_DATA_LEN   256
#define MQTT_DELAY_MS 5000
#define TIME_STR	64
#define IS_DEBUG(C)	((C)->debug)

// YF-DN32-T	G1 1/4"	->	3-120 L/min -> 1.8 pps per litre/minute of flow / 108 ppl
// YF-B6		G1"		->	2-50 L/min  -> 7.9 pps per litre/minute of flow / 476 ppl
// YF-B10		G3/4"	->	1-30 L/min  -> 6.6 pps per litre/minute of flow / 396 ppl
// YF-B1		G1/2"	->	1-25 L/min  -> 11  pps per litre/minute of flow / 660 ppl

enum {
	FLOW_YF_MQTT_FLOW = 0,
	FLOW_YF_MQTT_TOTAL_FLOW,
	FLOW_YF_MQTT_LAST_FLOW,
	FLOW_YF_MQTT_DURATION_FLOW,
	FLOW_YF_MQTT_TOTAL,
	FLOW_YF_MQTT_LAST_RESET,
	FLOW_YF_MQTT_MAX,
};

struct flow_yf_sensor {
	int pin;
	float pps;
	uint32_t pulse;
	float flow;
	bool force;
	uint64_t flow_start;
	uint64_t last_read;
	uint64_t duration_ms;
	time_t last_flow_date;
	uint64_t total_flow_ml;
	time_t last_reset_date;
	bool send_total;
	uint64_t total_ml;
	bool connected;
	mqtt_component_t mqtt_comp[FLOW_YF_MQTT_MAX];
};

struct flow_yf_context_t {
	sys_module_t mod;
	uint8_t count;
	struct flow_yf_sensor *sensors[YF_SENORS_MAX];
	uint32_t debug;
	uint64_t acc_msec;
	uint64_t acc_msec_last;
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static bool flow_yf_config_get(struct flow_yf_context_t **ctx)
{
	char *acc_sec = USER_PRAM_GET(FLOW_ACC_SEC);
	char *config = param_get(FLOW_YF);
	char *rest, *rest1, *tok, *ptok;
	float pps;
	int pin;

	(*ctx) = NULL;
	if (!config || strlen(config) < 1)
		goto out;

	(*ctx) = calloc(1, sizeof(struct flow_yf_context_t));
	if ((*ctx) == NULL)
		goto out;

	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		ptok = strtok_r(tok, ":", &rest1);
		if (!ptok || !rest1)
			continue;
		pin = (int)strtol(ptok, NULL, 0);
		if (pin < GPIO_PIN_MIN || pin > GPIO_PIN_MAX)
			continue;
		pps = strtof(rest1, NULL);
		if (pps <= 0)
			continue;
		(*ctx)->sensors[(*ctx)->count] = calloc(1, sizeof(struct flow_yf_sensor));
		if (!(*ctx)->sensors[(*ctx)->count])
			continue;
		(*ctx)->sensors[(*ctx)->count]->pin = pin;
		(*ctx)->sensors[(*ctx)->count]->pps = pps;
		(*ctx)->count++;
		if ((*ctx)->count >= YF_SENORS_MAX)
			break;
	}
	if (acc_sec && strlen(acc_sec) >= 1)
		(*ctx)->acc_msec = (int)strtol(acc_sec, NULL, 0) * 1000;

out:
	free(acc_sec);
	free(config);
	if ((*ctx) && (*ctx)->count < 1) {
		free(*ctx);
		(*ctx) = NULL;
	}

	return ((*ctx) ? (*ctx)->count > 0 : 0);
}

static bool flow_yf_log(void *context)
{
	struct flow_yf_context_t *ctx = (struct flow_yf_context_t *)context;
	static char time_buff[TIME_STR];
	struct tm dt = {0};
	int i;

	if (!ctx)
		return true;
	hlog_info(FLOW_YF_MODULE, "Reading %d sensors:", ctx->count);
	for (i = 0; i < ctx->count; i++) {
		hlog_info(FLOW_YF_MODULE, "\t %d: Current flow %3.2f L/min", i, ctx->sensors[i]->flow);

		if (ctx->sensors[i]->last_reset_date) {
			epoch2time(&ctx->sensors[i]->last_reset_date, &dt);
			time_to_str(time_buff, TIME_STR, &dt);
		} else {
			strncpy(time_buff, "N/A", TIME_STR);
		}
		hlog_info(FLOW_YF_MODULE, "\t    Total water %3.2f L since [%s]",
					(float)ctx->sensors[i]->total_ml / 1000.0, time_buff);

		if (ctx->sensors[i]->last_flow_date) {
			epoch2time(&ctx->sensors[i]->last_flow_date, &dt);
			time_to_str(time_buff, TIME_STR, &dt);
		} else {
			strncpy(time_buff, "N/A", TIME_STR);
		}
		hlog_info(FLOW_YF_MODULE, "\t    Last flow [%s]", time_buff);
		hlog_info(FLOW_YF_MODULE, "\t      Duration %lld min, Total %3.2f L",
				  ctx->sensors[i]->duration_ms / 60000, (float)ctx->sensors[i]->total_flow_ml / 1000.0);
	}
	if (ctx->acc_msec)
		hlog_info(FLOW_YF_MODULE, "Accumulating data on %lld seconds interval", ctx->acc_msec / 1000);

	return true;
}

static void flow_yf_debug_set(uint32_t debug, void *context)
{
	struct flow_yf_context_t *ctx = (struct flow_yf_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

static void flow_yf_reset(struct flow_yf_sensor *sensor)
{
	struct tm date;

	sensor->total_ml = 0;
	if (ntp_time_valid()) {
		if (tz_datetime_get(&date))
			time2epoch(&date, &sensor->last_reset_date);
	}
}

#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int flow_yf_mqtt_data_send(struct flow_yf_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	struct tm dt = {0};
	float tot = 0.0;
	int count = 0;
	int ret = -1;

	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"flow\": \"%3.2f\"", ctx->sensors[idx]->flow);
		if (ctx->sensors[idx]->send_total) {
			tot = (float)ctx->sensors[idx]->total_ml / 1000.0; // ml -> l
			flow_yf_reset(ctx->sensors[idx]);
			ctx->sensors[idx]->send_total = false;
		}
		ADD_MQTT_MSG_VAR(",\"total\": \"%3.2f\"", tot);
		if (ctx->sensors[idx]->last_reset_date) {
			epoch2time(&ctx->sensors[idx]->last_reset_date, &dt);
			time_to_str(time_buff, TIME_STR, &dt);
			ADD_MQTT_MSG_VAR(",\"last_reset\": \"%s\"", time_buff);
		} else {
			ADD_MQTT_MSG(",\"last_reset\":\"N/A\"");
		}
		ADD_MQTT_MSG_VAR(",\"total_flow\": \"%3.2f\"", (float)ctx->sensors[idx]->total_flow_ml / 1000.0); // ml -> l
		if (ctx->sensors[idx]->last_flow_date) {
			epoch2time(&ctx->sensors[idx]->last_flow_date, &dt);
			time_to_str(time_buff, TIME_STR, &dt);
			ADD_MQTT_MSG_VAR(",\"last_flow\": \"%s\"", time_buff);
		} else {
			ADD_MQTT_MSG(",\"last_flow\":\"N/A\"");
		}
		ADD_MQTT_MSG_VAR(",\"duration_flow\": \"%lld\"", ctx->sensors[idx]->duration_ms / 60000); // ms -> min
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(&ctx->sensors[idx]->mqtt_comp[FLOW_YF_MQTT_FLOW], ctx->mqtt_payload);
	ctx->sensors[idx]->force = false;

	if (!ret)
		ctx->mqtt_last_send = now;

	return ret;
}

static void flow_yf_mqtt_send(struct flow_yf_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	static int idx;
	int i;

	for (i = 0; i < ctx->count; i++)
		if (ctx->sensors[i]->force)
			ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].force = true;

	for (i = 0; i < ctx->count; i++) {
		if (ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].force == true) {
			flow_yf_mqtt_data_send(ctx, i);
			return;
		}
	}

	if ((now - ctx->mqtt_last_send) < MQTT_DELAY_MS)
		return;
	if (idx >= ctx->count)
		idx = 0;
	flow_yf_mqtt_data_send(ctx, idx++);
}

static void flow_yf_sensor_data(struct flow_yf_context_t *ctx, int idx)
{
	struct flow_yf_sensor *sensor = ctx->sensors[idx];
	uint64_t now = time_ms_since_boot();
	struct tm date;
	float interval;
	uint32_t data;

	if (now - sensor->last_read < MEASURE_TIME_MS)
		return;

	data = __atomic_exchange_n(&(sensor->pulse), 0, __ATOMIC_RELAXED);
	if (data) {
		if (!sensor->flow) {
			sensor->flow_start = now;
			sensor->total_flow_ml = 0;
			if (tz_datetime_get(&date))
				time2epoch(&date, &sensor->last_flow_date);
			if (IS_DEBUG(ctx))
				hlog_info(FLOW_YF_MODULE, "New flow detected on %d: %d", idx, data);
		}
		sensor->duration_ms = now - sensor->flow_start;
		// ((MEASURE_TIME_MS / (now - sensor->last_read)) * data) / sensor->pps;
		interval = ((float)MEASURE_TIME_MS / (float)(now - sensor->last_read));
		sensor->flow = (interval * data) / sensor->pps;
		sensor->total_flow_ml += ((sensor->flow * 1000) / 60);
		sensor->total_ml += ((sensor->flow * 1000) / 60);
		sensor->force = true;
		if (IS_DEBUG(ctx)) {
			hlog_info(FLOW_YF_MODULE, "%d: Measured %3.2f L/min: %d ticks for %3.2f sec",
					  idx, sensor->flow, data, interval);
			hlog_info(FLOW_YF_MODULE, "%d: Flow total %lld ml for %lld ms, total %lld ml",
					  idx, sensor->total_flow_ml, sensor->duration_ms, sensor->total_ml);
		}
	} else if (sensor->flow) {
		sensor->flow = 0;
		sensor->force = true;
		if (IS_DEBUG(ctx))
			hlog_info(FLOW_YF_MODULE, "Flow stoped on %d: %lld L for %d min",
					  idx, ctx->sensors[idx]->total_flow_ml / 1000,
					  ctx->sensors[idx]->duration_ms / 60000);
	}
	sensor->last_read = now;
}

static void flow_yf_run(void *context)
{
	struct flow_yf_context_t *ctx = (struct flow_yf_context_t *)context;
	uint64_t now = time_ms_since_boot();
	int i;

	for (i = 0; i < ctx->count; i++) {
		if (!ctx->sensors[i]->last_reset_date && ntp_time_valid())
			flow_yf_reset(ctx->sensors[i]);
		flow_yf_sensor_data(ctx, i);
	}

	if (ctx->acc_msec && ((now - ctx->acc_msec_last) >= ctx->acc_msec)) {
		ctx->acc_msec_last = now;
		for (i = 0; i < ctx->count; i++) {
			ctx->sensors[i]->force = true;
			ctx->sensors[i]->send_total = true;
		}
	}

	flow_yf_mqtt_send(ctx);

}

static void flow_yf_mqtt_components_add(struct flow_yf_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].dev_class = "volume_flow_rate";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].unit = "L/min";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].value_template = "{{ value_json.flow }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].name, "Flow_%d", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW]));

		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].dev_class = "volume_storage";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].unit = "L";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].value_template = "{{ value_json.total_flow }}";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].state_topic =
										ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].state_topic;
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW].name, "Flow_%d_total_flow", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL_FLOW]));

		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW].value_template = "{{ value_json.last_flow }}";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW].state_topic =
										ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].state_topic;
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW].name, "Flow_%d_last_flow", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_FLOW]));

		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].dev_class = "duration";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].unit = "min";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].value_template = "{{ value_json.duration_flow }}";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].state_topic =
										ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].state_topic;
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW].name, "Flow_%d_duration_flow", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_DURATION_FLOW]));

		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].dev_class = "volume_storage";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].unit = "L";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].value_template = "{{ value_json.total }}";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].state_topic =
										ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].state_topic;
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL].name, "Flow_%d_total", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_TOTAL]));

		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET].module = FLOW_YF_MODULE;
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET].value_template = "{{ value_json.last_reset }}";
		ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET].state_topic =
										ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_FLOW].state_topic;
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET].name, "Flow_%d_last_reset", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[FLOW_YF_MQTT_LAST_RESET]));
	}
}

static void flow_yf_irq(void *context)
{
	struct flow_yf_sensor *sensor = (struct flow_yf_sensor *)context;

	if (!sensor)
		return;
	__atomic_add_fetch(&(sensor->pulse), 1, __ATOMIC_RELAXED);
}

static bool flow_yf_init(struct flow_yf_context_t **ctx)
{
	int i, c = 0;

	if (!flow_yf_config_get(ctx))
		return false;

	for (i = 0; i < (*ctx)->count; i++) {
		(*ctx)->sensors[i]->send_total = ((*ctx)->acc_msec > 0) ? false : true;
		if (!sys_add_irq_callback((*ctx)->sensors[i]->pin, flow_yf_irq, GPIO_IRQ_EDGE_RISE, (*ctx)->sensors[i]))
			c++;
	}

	flow_yf_mqtt_components_add(*ctx);
	hlog_info(FLOW_YF_MODULE, "Initialise successfully %d / %d sensors", c, (*ctx)->count);
	for (i = 0; i < (*ctx)->count; i++)
		hlog_info(FLOW_YF_MODULE, "\tSensor %d attached to pin %d",
				   i, (*ctx)->sensors[i]->pin);

	return true;
}

static int cmd_flow_yf_reset(cmd_run_context_t *ctx, char *cmd, char *params, void *user_data)
{
	struct flow_yf_context_t *flow_yf_ctx = (struct flow_yf_context_t *)user_data;
	int i;

	UNUSED(ctx);
	UNUSED(cmd);

	if (params && params[0] == ':' && strlen(params) >= 2) {
		i = (int)strtol(params + 1, NULL, 0);
		if (i < 0 || i >= flow_yf_ctx->count)
			return -1;
		flow_yf_reset(flow_yf_ctx->sensors[i]);
	} else {
		for (i = 0; i < flow_yf_ctx->count; i++)
			flow_yf_reset(flow_yf_ctx->sensors[i]);
	}

	return 0;
}

static app_command_t flow_yf_requests[] = {
	{"reset", ":<id> - Optional, reset the accumulated statistics of the given sensor",
	  cmd_flow_yf_reset }
};

void flow_yf_register(void)
{
	struct flow_yf_context_t *ctx = NULL;

	if (!flow_yf_init(&ctx))
		return;

	ctx->mod.name = FLOW_YF_MODULE;
	ctx->mod.run = flow_yf_run;
	ctx->mod.log = flow_yf_log;
	ctx->mod.debug = flow_yf_debug_set;
	ctx->mod.commands.hooks = flow_yf_requests;
	ctx->mod.commands.count = ARRAY_SIZE(flow_yf_requests);
	ctx->mod.commands.description = "YF Flow control";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
