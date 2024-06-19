// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "lwip/inet.h"
#include "lwip/altcp.h"
#include "irrig.h"

#define SOILOG	"soil"

/* For each measurements, take 30 samples */
#define SOIL_MEASURE_COUNT 30
/* Filter out the 5 biggest and the 5 smallest */
#define SOIL_MEASURE_DROP	5

#define WH_DEFAULT_PORT	80
#define WH_HTTP_CMD		"POST"
#define WH_HTTP_TYPE	"application/json"
#define HTTP_OK	200
#define	WH_PAYLOAD_MAX_SIZE	64
#define	WH_PAYLOAD_TEMPLATE "{ \"sensor\": %d, \"status\": \"%s\", \"value\": %d }"
#define WH_SEND_DELAY_MS	5000


static struct {
	int gp_id;
	int adc_id;
} adc_mapping[] = {
		{26, 0},
		{27, 1},
		{28, 2},
};

struct soil_sensor_analog_t {
	int adc_id;
	uint32_t samples[SOIL_MEASURE_COUNT];
	uint32_t last_analog;
};

struct soil_sensor_t {
	int analog_pin;
	struct soil_sensor_analog_t *analog;
	int digital_pin;
	uint8_t last_digital;
	bool wh_send;
	uint32_t wh_last_send;
};

static struct {
	uint32_t send_time;
	int sensors_count;
	struct soil_sensor_t sensors[MAX_SOIL_SENSORS_COUNT];
	int wh_idx;
} soil_context;

static int wh_notify(int id, int trigger, int data)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE, id, trigger?"wet":"dry", data);
	return webhook_send(soil_context.wh_idx, notify_buff, strlen(notify_buff));
}

static void wh_notify_send(int id)
{
	uint32_t now;

	if (!soil_context.sensors[id].wh_send)
		return;

	now = to_ms_since_boot(get_absolute_time());
	if ((now - soil_context.sensors[id].wh_last_send) > WH_SEND_DELAY_MS) {
		if (!wh_notify(id, soil_context.sensors[id].last_digital,
					   soil_context.sensors[id].analog?soil_context.sensors[id].analog->last_analog:0)) {
			soil_context.sensors[id].wh_send = false;
		}
		soil_context.sensors[id].wh_last_send = now;
	}
}

static void measure_digital(int id)
{
	uint8_t digital;

	digital = gpio_get(soil_context.sensors[id].digital_pin);
	if (digital != soil_context.sensors[id].last_digital) {
		soil_context.sensors[id].last_digital = gpio_get(soil_context.sensors[id].digital_pin);
		soil_context.sensors[id].wh_send = true;
	}
}

static void measure_analog(int id)
{
	uint32_t av;
	int i;

	adc_select_input(soil_context.sensors[id].analog->adc_id);

	/* read the samples */
	for (i = 0; i < SOIL_MEASURE_COUNT; i++)
		soil_context.sensors[id].analog->samples[i] = adc_read();

	/* filter biggest and smallest */
	av = samples_filter(soil_context.sensors[id].analog->samples, SOIL_MEASURE_COUNT, SOIL_MEASURE_DROP);
	soil_context.sensors[id].analog->last_analog = av;
}

void soil_measure(void)
{
	int i;

	for (i = 0; i < soil_context.sensors_count; i++) {
		if (soil_context.sensors[i].analog)
			measure_analog(i);
		if (soil_context.sensors[i].digital_pin >= 0) {
			measure_digital(i);
			if (soil_context.wh_idx >= 0)
				wh_notify_send(i);
		}
		mqtt_data_soil(i,
					   soil_context.sensors[i].analog?soil_context.sensors[i].analog->last_analog:0,
					   soil_context.sensors[i].last_digital);
	}
}

static int soil_read_pin_cfg(char *config, bool digital)
{
	char *config_tokens[MAX_SOIL_SENSORS_COUNT];
	char *config_pins[2];
	char *rest, *tok;
	int count = 0;
	int idx, id;
	int i, j;

	if (!config)
		return 0;

	i = 0;
	rest = config;
	while (i < MAX_SOIL_SENSORS_COUNT && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i > 0)
		i--;
	while (i >= 0) {
		j = 0;
		rest = config_tokens[i];
		while (j < 2 && (tok = strtok_r(rest, ":", &rest)))
			config_pins[j++] = tok;
		i--;
		if (j != 2)
			continue;
		idx = (int)strtol(config_pins[0], NULL, 10);
		id = (int)strtol(config_pins[1], NULL, 10);
		if (idx < 0 || idx >= MAX_SOIL_SENSORS_COUNT)
			continue;
		if (id < 0 || id > 40)
			continue;
		count++;
		if (digital)
			soil_context.sensors[idx].digital_pin = id;
		else
			soil_context.sensors[idx].analog_pin = id;
	}

	return count;
}

static void wh_callback(int idx, int http_code, void *context)
{
	UNUSED(idx);
	UNUSED(context);
	switch (http_code) {
	case 0:
		hlog_info(SOILOG, "http timeout");
		break;
	case HTTP_OK:
		break;
	default:
		hlog_info(SOILOG, "http error [%d]", http_code);
		break;
	}
}

static bool notify_get_config(char **server, char **endpoint, int *port)
{
	char *port_str = NULL;
	char *srv = NULL;
	char *ep = NULL;
	int port_id = 0;

	srv = param_get(WEBHOOK_SERVER);
	if (!srv || strlen(srv) < 1)
		goto out_err;
	ep = param_get(WEBHOOK_ENDPINT);
	if (!ep || strlen(ep) < 1)
		goto out_err;

	port_str = param_get(WEBHOOK_PORT);
	if (port_str && strlen(port_str) > 1)
		port_id = atoi(port_str);
	if (!port_id)
		port_id = WH_DEFAULT_PORT;
	free(port_str);

	if (server)
		*server = srv;
	else
		free(srv);
	if (endpoint)
		*endpoint = ep;
	else
		free(ep);
	if (port)
		*port = port_id;

	return true;
out_err:
	free(srv);
	free(ep);
	return false;
}

int soil_init(void)
{
	char *digital = param_get(SOIL_D);
	char *analog = param_get(SOIL_A);
	char *wh_server, *wh_endpoint;
	bool has_analog = false;
	unsigned int cnt, i, j;
	int wh_port;

	memset(&soil_context, 0, sizeof(soil_context));
	if ((!digital || strlen(digital) < 1) && (!analog || strlen(analog) < 1))
		goto out_error;

	soil_context.wh_idx = -1;
	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		soil_context.sensors[i].analog_pin = -1;
		soil_context.sensors[i].digital_pin = -1;
	}

	cnt = soil_read_pin_cfg(digital, true);
	cnt += soil_read_pin_cfg(analog, false);
	if (cnt < 1)
		goto out_error;

	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		if (soil_context.sensors[i].digital_pin > 0)
			soil_context.sensors_count++;
		if (soil_context.sensors[i].analog_pin > 0) {
			for (j = 0; j < ARRAY_SIZE(adc_mapping); j++) {
				if (adc_mapping[j].gp_id == soil_context.sensors[i].analog_pin) {
					soil_context.sensors[i].analog = malloc(sizeof(struct soil_sensor_analog_t));
					if (!soil_context.sensors[i].analog)
						break;
					memset(soil_context.sensors[i].analog, 0, sizeof(struct soil_sensor_analog_t));
					has_analog = true;
					soil_context.sensors[i].analog->adc_id = adc_mapping[j].adc_id;
					if (soil_context.sensors[i].digital_pin < 0)
						soil_context.sensors_count++;
					break;
				}
			}
		}
	}

	if (soil_context.sensors_count < 1)
		goto out_error;

	free(digital);
	free(analog);

	if (notify_get_config(&wh_server, &wh_endpoint, &wh_port))
		soil_context.wh_idx = webhook_add(wh_server, wh_port, WH_HTTP_TYPE, wh_endpoint, WH_HTTP_CMD, true, wh_callback, NULL);

	if (has_analog) {
		adc_init();
	    adc_set_round_robin(0);
	}

	for (i = 0 ; i < MAX_SOIL_SENSORS_COUNT; i++) {
		if (soil_context.sensors[i].digital_pin > 0) {
			gpio_init(soil_context.sensors[i].digital_pin);
			gpio_set_dir(soil_context.sensors[i].digital_pin, GPIO_IN);
			gpio_put(soil_context.sensors[i].digital_pin, 0);
		}
		if (soil_context.sensors[i].analog_pin > 0)
			adc_gpio_init(soil_context.sensors[i].analog_pin);
	}

	hlog_info(SOILOG, "%d soil sensors initialized", soil_context.sensors_count);
	return soil_context.sensors_count;

out_error:
	free(digital);
	free(analog);
	hlog_info(SOILOG, "No valid configuration for soil sensors");
	return 0;
}

void soil_log(void)
{
	int s, i;

	for (i = 0; i < soil_context.sensors_count; i++) {
		if (soil_context.sensors[i].analog)
			s = soil_context.sensors[i].analog->last_analog;
		else
			s = -1;
		hlog_info(SOILOG, "Sensor %d: digital %d, analog %d",
				  i, soil_context.sensors[i].last_digital, s);
	}
}



