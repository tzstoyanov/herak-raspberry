// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "pico/float.h"
#include "hardware/i2c.h"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define SHT20_MODULE		"sht20"
#define SHT20_SENORS_MAX	6
#define MQTT_DATA_LEN	128
#define MQTT_DELAY_MS	5000

#define I2C_TIMEOUT_US	1000
#define SHT20_ADDR	0x40
#define SHT20_CLOCK	50000
#define SHT20_DATA_SIZE	3
#define SHT20_CMD_RETRY	5

#define SHT0_READ_INTERVAL_MS		1000
#define SHT0_MEASURE_DELAY_MS		100
#define SHT0_POWER_UP_DELAY_MS		50
#define SHT0_POWER_DOWN_DELAY_MS	100
// 1 min delay in power down state if more than 20 connection errors are detected
#define CONN_ERR_THR			20
#define SHT0_ERROR_DELAY_MS		60000

					// no hold	// hold
#define SHT20_TEMP		0xF3	// 0xE3
#define SHT20_HUMID		0xF5	// 0xE5
#define SHT20_WRITE_USER_REG	0xE6
#define SHT20_READ_USER_REG	0xE7
#define SHT20_RESET		0xFE
#define SHT20_RESERVED_CFG_MASK	0x38

#define SHT20_CFG_RESOLUTION_12BITS     0x00
#define SHT20_CFG_RESOLUTION_11BITS     0x81
#define SHT20_CFG_RESOLUTION_10BITS     0x80
#define SHT20_CFG_RESOLUTION_8BITS		0x01
#define SHT20_CFG_DISABLE_ONCHIP_HEATER	0x00
#define SHT20_CFG_DISABLE_OTP_RELOAD    0x02

#define IS_DEBUG(C)	((C)->debug)

static const int16_t __in_flash() POLYNOMIAL = 0x131;

enum {
	SHT20_MQTT_TEMPERATURE = 0,
	SHT20_MQTT_HUMIDITY,
	SHT20_MQTT_VPD,
	SHT20_MQTT_DEW_POINT,
	SHT20_MQTT_MAX,
};

struct sht20_context_t;

struct sht20_sensor {
	i2c_inst_t *i2c;
	uint8_t sht20_addr;
	int sda_pin;
	int scl_pin;
	int power_pin;
	float temperature;
	float humidity;
	float vpd;	//  Vapor Pressure Deficit
	float dew_point;
	bool force;
	bool connected;
	bool hard_reset;
	uint64_t conn_err_count;
	uint64_t err_state;
	uint8_t config;
	uint8_t read_cmd;
	uint64_t read_requested;
	mqtt_component_t mqtt_comp[SHT20_MQTT_MAX];
	struct sht20_context_t *ctx;
	uint64_t ok_stat;
	uint64_t err_stat;
};

struct sht20_context_t {
	sys_module_t mod;
	uint8_t count;
	uint8_t idx;
	uint64_t last_read;
	struct sht20_sensor *sensors[SHT20_SENORS_MAX];
	uint32_t debug;
	uint64_t mqtt_last_send;
	char mqtt_payload[MQTT_DATA_LEN + 1];
};

static int sht20_sensor_write(struct sht20_sensor *sensor, uint8_t cmd)
{
	int i;

	for (i = 0; i < SHT20_CMD_RETRY; i++)
		if (sizeof(cmd) == i2c_write_timeout_us(sensor->i2c, sensor->sht20_addr, &cmd, sizeof(cmd), true, I2C_TIMEOUT_US))
			return 0;
	return -1;
}

static int sht20_sensor_read(struct sht20_sensor *sensor, uint8_t *cmd, uint8_t count)
{
	if (count == i2c_read_blocking(sensor->i2c, sensor->sht20_addr, cmd, count, false))
		return 0;

	return -1;
}

// https://github.com/u-fire/uFire_SHT20/blob/master/src/uFire_SHT20.cpp
// https://github.com/keep1234quiet/IOT_SHT20_STM32_ESP8266/blob/master/Devices/sht20.c
static void sht20_power_down(struct sht20_sensor *sensor)
{
	i2c_deinit(sensor->i2c);

	// Set pins to High-Impedance (Input) without pulling them low first
	gpio_set_function(sensor->sda_pin, GPIO_FUNC_NULL);
	gpio_set_function(sensor->scl_pin, GPIO_FUNC_NULL);
	gpio_set_dir(sensor->sda_pin, GPIO_IN);
	gpio_set_dir(sensor->scl_pin, GPIO_IN);
	gpio_disable_pulls(sensor->sda_pin);
	gpio_disable_pulls(sensor->scl_pin);

	if (sensor->power_pin >= GPIO_PIN_MIN)
		gpio_put(sensor->power_pin, 0);

	sleep_ms(SHT0_POWER_DOWN_DELAY_MS);
}

static void sht20_power_up(struct sht20_sensor *sensor)
{
	// Power on the sensor
	if (sensor->power_pin >= GPIO_PIN_MIN) {
		gpio_init(sensor->power_pin);
		gpio_set_dir(sensor->power_pin, GPIO_OUT);
		gpio_put(sensor->power_pin, 1);
	}
	sleep_ms(SHT0_POWER_UP_DELAY_MS);

	// BUS recovery: Force SCL high/low to clear any stuck bits
	gpio_init(sensor->sda_pin);
	gpio_init(sensor->scl_pin);
	gpio_set_dir(sensor->sda_pin, GPIO_IN);
	gpio_set_dir(sensor->scl_pin, GPIO_OUT);
	gpio_pull_up(sensor->sda_pin);
	gpio_pull_up(sensor->scl_pin);
	sleep_ms(5);
	for (int i = 0; i < 9; i++) {
		gpio_put(sensor->scl_pin, 1);
		sleep_us(5);
		gpio_put(sensor->scl_pin, 0);
		sleep_us(5);
	}
	gpio_put(sensor->scl_pin, 1);

	// Init the I2C hardware
	i2c_init(sensor->i2c, SHT20_CLOCK);
	gpio_set_function(sensor->sda_pin, GPIO_FUNC_I2C);
	gpio_set_function(sensor->scl_pin, GPIO_FUNC_I2C);
}

static int sht20_sensor_init(struct sht20_sensor *sensor)
{
	int ret = -1;

	sensor->connected = false;
	sht20_power_up(sensor);

	if (sht20_sensor_write(sensor, SHT20_READ_USER_REG))
		goto out;
	if (sht20_sensor_read(sensor, &sensor->config, sizeof(sensor->config)))
		goto out;
	if (sensor->config == 0xFF)
		goto out;
	sensor->config = ((sensor->config & SHT20_RESERVED_CFG_MASK) |
						SHT20_CFG_RESOLUTION_12BITS |
						SHT20_CFG_DISABLE_ONCHIP_HEATER |
						SHT20_CFG_DISABLE_OTP_RELOAD);
	if (sht20_sensor_write(sensor, SHT20_WRITE_USER_REG))
		goto out;
	if (sht20_sensor_write(sensor, sensor->config))
		goto out;

	sensor->connected = true;
	sensor->conn_err_count = 0;
	ret = 0;
out:
	if (!sensor->connected) {
		sensor->err_stat++;
		sensor->conn_err_count++;
		if (!sensor->err_state && !(sensor->conn_err_count % CONN_ERR_THR))
			sensor->err_state = time_ms_since_boot();
		if (IS_DEBUG(sensor->ctx))
			hlog_info(SHT20_MODULE, "Connection error on sensor %d: %d", sensor->sda_pin, sensor->conn_err_count);
	}
	return ret;
}

static void sht20_sensor_check_connected(struct sht20_sensor *sensor)
{
	sensor->connected = false;
	if (sht20_sensor_write(sensor, SHT20_READ_USER_REG))
		return;
	if (sht20_sensor_read(sensor, &sensor->config, sizeof(sensor->config)))
		return;
	if (sensor->config == 0xFF)
		return;
	sensor->connected = true;
}

static bool init_sht20_i2c_params(struct sht20_sensor *sensor)
{
	switch (sensor->sda_pin) {
	case 0:
	case 4:
	case 8:
	case 12:
	case 16:
	case 20:
		sensor->i2c = i2c0;
		sensor->scl_pin = sensor->sda_pin + 1;
		break;
	case 2:
	case 6:
	case 10:
	case 14:
	case 18:
	case 26:
		sensor->i2c = i2c1;
		sensor->scl_pin = sensor->sda_pin + 1;
		break;
	default:
		break;
	}
	sensor->sht20_addr = SHT20_ADDR;
	if (sensor->scl_pin == (sensor->sda_pin + 1) &&
		sensor->i2c != NULL)
		return true;
	return false;
}

static bool sht20_config_get(struct sht20_context_t **ctx)
{
	char *config = param_get(SHT20_SDA_PIN);
	char *power = param_get(SHT20_POWER_PIN);
	struct sht20_sensor sensor;
	char *rest, *tok;
	int p, i;

	(*ctx) = NULL;
	if (!config || strlen(config) < 1)
		goto out;

	(*ctx) = calloc(1, sizeof(struct sht20_context_t));
	if ((*ctx) == NULL)
		goto out;

	rest = config;
	while ((tok = strtok_r(rest, ";", &rest))) {
		memset(&sensor, 0, sizeof(sensor));
		sensor.sda_pin = (int)strtol(tok, NULL, 0);
		sensor.power_pin = -1;
		if (!init_sht20_i2c_params(&sensor))
			continue;
		(*ctx)->sensors[(*ctx)->count] = calloc(1, sizeof(struct sht20_sensor));
		if (!(*ctx)->sensors[(*ctx)->count])
			continue;
		sensor.ctx = *ctx;
		memcpy((*ctx)->sensors[(*ctx)->count], &sensor, sizeof(sensor));
		(*ctx)->count++;
		if ((*ctx)->count >= SHT20_SENORS_MAX)
			break;
	}

	i = 0;
	rest = power;
	while ((tok = strtok_r(rest, ";", &rest))) {
		if (i >= (*ctx)->count || !(*ctx)->sensors[i])
			break;
		p = (int)strtol(tok, NULL, 0);
		if (p >= GPIO_PIN_MIN && p <= GPIO_PIN_MAX)
			(*ctx)->sensors[i]->power_pin = p;
		i++;
	}

out:
	free(config);
	if ((*ctx) && (*ctx)->count < 1) {
		free(*ctx);
		(*ctx) = NULL;
	}

	return ((*ctx) ? (*ctx)->count > 0 : 0);
}

static bool sht20_log(void *context)
{
	struct sht20_context_t *ctx = (struct sht20_context_t *)context;
	uint32_t q;
	int i;

	if (!ctx)
		return true;
	hlog_info(SHT20_MODULE, "Reading %d sensors:", ctx->count);
	for (i = 0; i < ctx->count; i++) {
		q = ((ctx->sensors[i]->ok_stat * 100) / (ctx->sensors[i]->ok_stat + ctx->sensors[i]->err_stat));
		hlog_info(SHT20_MODULE, "\tid %d attached to %d,%d(%s %d%%), power pin (%d)",
				  i, ctx->sensors[i]->sda_pin, ctx->sensors[i]->scl_pin,
				  ctx->sensors[i]->connected ? "connected" : "not connected", q,
				  ctx->sensors[i]->power_pin);
		hlog_info(SHT20_MODULE, "\t\tTemperature %3.2f°C, Humidity %3.2f%%, VPD %3.2fkPa, Dew Point %3.2f%%",
				  ctx->sensors[i]->temperature, ctx->sensors[i]->humidity,
				  ctx->sensors[i]->vpd, ctx->sensors[i]->dew_point);
	}

	return true;
}

static void sht20_debug_set(uint32_t debug, void *context)
{
	struct sht20_context_t *ctx = (struct sht20_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

#define TIME_STR	64
#define ADD_MQTT_MSG(_S_) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
							count += snprintf(ctx->mqtt_payload + count, len - count, _S_); }
#define ADD_MQTT_MSG_VAR(_S_, ...) { if ((len - count) < 0) { printf("%s: Buffer full\n\r", __func__); return -1; } \
				     count += snprintf(ctx->mqtt_payload + count, len - count, _S_, __VA_ARGS__); }
static int sth20_mqtt_data_send(struct sht20_context_t *ctx, int idx)
{
	uint64_t now = time_ms_since_boot();
	static char time_buff[TIME_STR];
	int len = MQTT_DATA_LEN;
	mqtt_component_t *ms;
	int count = 0;
	int ret = -1;

	ms = &ctx->sensors[idx]->mqtt_comp[SHT20_MQTT_TEMPERATURE];
	ADD_MQTT_MSG("{");
		ADD_MQTT_MSG_VAR("\"time\": \"%s\"", get_current_time_str(time_buff, TIME_STR));
		ADD_MQTT_MSG_VAR(",\"temperature\": \"%3.2f\"", ctx->sensors[idx]->temperature);
		ADD_MQTT_MSG_VAR(",\"humidity\": \"%3.2f\"", ctx->sensors[idx]->humidity);
		ADD_MQTT_MSG_VAR(",\"vpd\": \"%3.2f\"", ctx->sensors[idx]->vpd);
		ADD_MQTT_MSG_VAR(",\"dew_point\": \"%3.2f\"", ctx->sensors[idx]->dew_point);
	ADD_MQTT_MSG("}")

	ctx->mqtt_payload[MQTT_DATA_LEN] = 0;
	ret = mqtt_msg_component_publish(ms, ctx->mqtt_payload);
	ctx->sensors[idx]->force = false;

	if (!ret)
		ctx->mqtt_last_send = now;

	return ret;
}

static void sht20_mqtt_send(struct sht20_context_t *ctx)
{
	uint64_t now = time_ms_since_boot();
	bool refresh;
	int i;

	if ((now - ctx->mqtt_last_send) >= MQTT_DELAY_MS)
		refresh = true;

	for (i = 0; i < ctx->count; i++)
		if (refresh || ctx->sensors[i]->force)
			ctx->sensors[i]->mqtt_comp[0].force = true;

	for (i = 0; i < ctx->count; i++) {
		if (ctx->sensors[i]->mqtt_comp[0].force == true) {
			sth20_mqtt_data_send(ctx, i);
			return;
		}
	}
}

static int sht20_sensor_request_data(struct sht20_sensor *sensor)
{
	int ret;

	ret = i2c_write_blocking(sensor->i2c,
							 sensor->sht20_addr,
							 &(sensor->read_cmd),
							 sizeof(sensor->read_cmd), false);
	if (ret != sizeof(sensor->read_cmd)) {
		sensor->err_stat++;
		if (IS_DEBUG(sensor->ctx))
			hlog_info(SHT20_MODULE, "Failed to request data from sensor %d", sensor->sda_pin);
		return -1;
	}

	sensor->read_requested = time_ms_since_boot();
	return 0;
}

static int sht20_check_crc(struct sht20_sensor *sensor, uint8_t *data, uint8_t count, uint8_t checksum)
{
	uint8_t crc = 0;
	int i, j;

	//calculates 8-Bit checksum with given polynomial
	for (i = 0; i < count; i++) {
		crc ^= (data[i]);
		for (j = 8; j > 0; --j) {
			if (crc & 0x80)
				crc = (crc << 1) ^ POLYNOMIAL;
			else
				crc = (crc << 1);
		}
	}
	if (crc != checksum) {
		if (IS_DEBUG(sensor->ctx))
			hlog_info(SHT20_MODULE, "CRC error on sensor %d: %d != %d", sensor->sda_pin, crc, checksum);
		return -1;
	}

	return 0;
}

static int sht20_sensor_get_data(struct sht20_sensor *sensor)
{
	uint8_t buff[SHT20_DATA_SIZE];
	float data, f1, f2;
	uint16_t raw;
	int ret = -1;

	ret = i2c_read_blocking(sensor->i2c, sensor->sht20_addr, buff, SHT20_DATA_SIZE, false);
	if (ret != SHT20_DATA_SIZE)
		goto out;

	if (IS_DEBUG(sensor->ctx))
		hlog_info(SHT20_MODULE, "Got raw data from sensor %d: [0x%2X 0x%2X 0x%2X]",
				  sensor->sda_pin, buff[0], buff[1], buff[2]);
	ret = sht20_check_crc(sensor, buff, 2, buff[2]);
	if (ret)
		goto out;

	raw = (buff[0] << 8) + buff[1];

	if (sensor->read_cmd == SHT20_TEMP) {
		data = raw * (175.72 / 65536.0)-46.85;
		if (sensor->temperature != data) {
			sensor->force = true;
			sensor->temperature = data;
		}
		sensor->read_cmd = SHT20_HUMID;
	} else {
		data = raw * (125.0 / 65536.0) - 6.0;
		if (sensor->humidity != data) {
			sensor->force = true;
			sensor->humidity = data;
		}
		sensor->read_cmd = SHT20_TEMP;
	}

	f1 = 0.6108 * exp(17.27 * sensor->temperature / (sensor->temperature + 237.3));
	f2 = sensor->humidity / 100 * f1;
	data = f1 - f2;
	if (sensor->vpd != data) {
		sensor->force = true;
		sensor->vpd = data;
	}

	f1 = -1.0 * sensor->temperature;
	f2 = 6.112 * exp(-1.0 * 17.67 * f1 / (243.5 - f1));
	f1 = sensor->humidity / 100.0 * f2;
	f2 = log(f1 / 6.112);
	data = -243.5 * f2 / (f2 - 17.67);
	if (sensor->dew_point != data) {
		sensor->force = true;
		sensor->dew_point = data;
	}

	if (IS_DEBUG(sensor->ctx))
		hlog_info(SHT20_MODULE, "   temperature %3.2f,  humidity %3.2f, vpd %3.2f, dew_point %3.2f",
				  sensor->temperature, sensor->humidity, sensor->vpd, sensor->dew_point);

	ret = 0;

out:
	sensor->read_requested = 0;
	if (ret)
		sensor->err_stat++;
	else
		sensor->ok_stat++;
	return ret;
}

static int sht20_sensor_data(struct sht20_sensor *sensor)
{
	uint64_t now = time_ms_since_boot();
	int ret = -1;

	if (sensor->err_state) {
		if ((now - sensor->err_state) > SHT0_ERROR_DELAY_MS)
			sensor->err_state = 0;
		return -1;
	}

	if (sensor->connected && sensor->read_requested) {
		if ((now - sensor->read_requested) < SHT0_MEASURE_DELAY_MS)
			return 0;
		sht20_sensor_get_data(sensor);
		sht20_power_down(sensor);
		return 1;
	}

	sht20_sensor_init(sensor);
	sht20_sensor_check_connected(sensor);
	if (sensor->connected)
		ret = sht20_sensor_request_data(sensor);

	if (ret)
		sht20_power_down(sensor);

	return ret;
}

static void sht20_run(void *context)
{
	struct sht20_context_t *ctx = (struct sht20_context_t *)context;
	uint64_t now = time_ms_since_boot();

	if (ctx->idx < ctx->count) {
		if (sht20_sensor_data(ctx->sensors[ctx->idx])) {
			ctx->last_read = now;
			ctx->idx++;
		}
		sht20_mqtt_send(ctx);
		return;
	}

	sht20_mqtt_send(ctx);
	if (((now - ctx->last_read) < SHT0_READ_INTERVAL_MS))
		return;
	ctx->idx = 0;
}

static void sht20_mqtt_components_add(struct sht20_context_t *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].module = SHT20_MODULE;
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].dev_class = "temperature";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].unit = "°C";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].value_template = "{{ value_json['temperature'] }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].name, "Temperature_%d", i);
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE]));

		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].module = SHT20_MODULE;
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].dev_class = "humidity";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].unit = "%";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].value_template = "{{ value_json['humidity'] }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].name, "Humidity_%d", i);
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY].state_topic = ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].state_topic;
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[SHT20_MQTT_HUMIDITY]));

		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].module = SHT20_MODULE;
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].dev_class = "pressure";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].unit = "kPa";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].value_template = "{{ value_json['vpd'] }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].name, "VPD_%d", i);
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD].state_topic = ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].state_topic;
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[SHT20_MQTT_VPD]));

		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].module = SHT20_MODULE;
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].platform = "sensor";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].dev_class = "temperature";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].unit = "°C";
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].value_template = "{{ value_json['dew_point'] }}";
		sys_asprintf(&ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].name, "DewPoint_%d", i);
		ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT].state_topic = ctx->sensors[i]->mqtt_comp[SHT20_MQTT_TEMPERATURE].state_topic;
		mqtt_msg_component_register(&(ctx->sensors[i]->mqtt_comp[SHT20_MQTT_DEW_POINT]));
	}
}

static bool sht20_init(struct sht20_context_t **ctx)
{
	int i;

	if (!sht20_config_get(ctx))
		return false;
	for (i = 0; i < (*ctx)->count; i++) {
		sht20_power_down((*ctx)->sensors[i]);
		(*ctx)->sensors[i]->read_cmd = SHT20_TEMP;
	}
	sht20_mqtt_components_add(*ctx);
	hlog_info(SHT20_MODULE, "Initialise successfully %d sensors", (*ctx)->count);
	for (i = 0; i < (*ctx)->count; i++)
		hlog_info(SHT20_MODULE, "\tSensor %d attached to sda %d; scl %d; power %d",
				   i, (*ctx)->sensors[i]->sda_pin, (*ctx)->sensors[i]->scl_pin,
				   (*ctx)->sensors[i]->power_pin);

	return true;
}

void sht20_register(void)
{
	struct sht20_context_t *ctx = NULL;

	if (!sht20_init(&ctx))
		return;

	ctx->mod.name = SHT20_MODULE;
	ctx->mod.run = sht20_run;
	ctx->mod.log = sht20_log;
	ctx->mod.debug = sht20_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}
