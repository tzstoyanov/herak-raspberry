// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "HD44780_LCD_PCF8574.hpp"

#include "herak_sys.h"
#include "common_internal.h"

#include "base64.h"
#include "params.h"

#define WIFI_CHAR_INDEX	0
#define MQTT_CHAR_INDEX	1
#define MAX_STRING	14
#define MAX_CELLS	4
#define LCD_BLINK_INERVAL	2
#define LCD_I2C_CLOCK_KHZ 100

const static uint8_t __in_flash() __symWifi[8] = {0x04, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x11, 0x00};
const static uint8_t __in_flash() __symMQTT[8] = {0x00, 0x00, 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x1F};

#define CONST_STR(x) const_cast < char * > (x)

#define LCD_MODULE	"LCD"

enum {
	DISPLAY_NONE = 0,
	DISPLAY_TEXT,
	DISPLAY_INT,
	DISPLAY_DOUBLE
};

typedef struct {
	HD44780LCD::LCDLineNumber_e row;
	int column;
	int dispaly;
	union {
		int numInt;
		double numDouble;
		char text[MAX_STRING+1];
	}data;
} lcd_cell;

struct lcd_context_t {
	sys_module_t mod;
	HD44780LCD *myLCD;
	bool wifiOn;
	bool mqttOn;
	lcd_cell cells[MAX_CELLS];
	bool refresh;
	uint32_t debug;
};

static struct lcd_context_t *lcd_ctx;

static bool init_lcd_i2c_params(int sda,  i2c_inst_t **i2c)
{
	(*i2c) = NULL;
	switch (sda) {
	case 0:
	case 4:
	case 8:
	case 12:
	case 16:
	case 20:
		(*i2c)= i2c0;
		break;
	case 2:
	case 6:
	case 10:
	case 14:
	case 18:
	case 26:
		(*i2c) = i2c1;
		break;
	default:
		break;
	}
	if ((*i2c) != NULL)
		return true;
	return false;
}

static bool get_lcd_config(lcd_context_t **ctx)
{
	char *lcd_config = param_get(LCD_CONFIG);
	int address, clock, sda;
	char *config_tokens[4];
	i2c_inst_t *i2c_inst;
	char *rest, *tok;
	bool ret = false;
	int i = 0;

	(*ctx) = NULL;
	if (!lcd_config || strlen(lcd_config) < 1)
		goto out;
	rest = lcd_config;
	while (i < 4 && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i != 4)
		goto out;
	address = (int)strtol(config_tokens[0], NULL, 16);
	if (address < 0 || address >= 0xFFFF)
		goto out;

	clock = (int)strtol(config_tokens[1], NULL, 10);
	if (clock < 0 || clock >= 0xFFFF)
		goto out;

	sda = (int)strtol(config_tokens[2], NULL, 10);
	if (sda < 0 || sda >= 0xFFFF)
		goto out;

	if (!init_lcd_i2c_params(sda, &i2c_inst))
		goto out;

	(*ctx) = (lcd_context_t *)calloc(1, sizeof(lcd_context_t));
	if (!(*ctx))
		goto out;
	(*ctx)->myLCD = new HD44780LCD(address, i2c_inst, LCD_I2C_CLOCK_KHZ, sda, sda + 1);
	if (!(*ctx)->myLCD)
		goto out;
	ret = true;

out:
	free(lcd_config);
	if (!ret && (*ctx)) {
		free(*ctx);
		(*ctx) = NULL;
	}
	return ret;
}
 
static bool lcd_init(lcd_context_t **ctx)
{
	if (!get_lcd_config(ctx))
		return false;

	(*ctx)->myLCD->PCF8574_LCDInit((*ctx)->myLCD->LCDCursorTypeOff, 2, 16);
	(*ctx)->myLCD->PCF8574_LCDClearScreen();
	(*ctx)->myLCD->PCF8574_LCDBackLightSet(true);
	(*ctx)->myLCD->PCF8574_LCDCreateCustomChar(WIFI_CHAR_INDEX, (uint8_t *)__symWifi);
	(*ctx)->myLCD->PCF8574_LCDCreateCustomChar(MQTT_CHAR_INDEX, (uint8_t *)__symMQTT);
	(*ctx)->refresh = true;
	lcd_ctx = *ctx;

	return true;
}

static lcd_cell *lcd_get_cell(int cell, int row, int column)
{
	HD44780LCD::LCDLineNumber_e lineNo;

	if (!lcd_ctx || cell < 0 || cell >= MAX_CELLS || column < 1 || column > 15)
		return NULL;

	switch(row) {
	case 0:
		lineNo = lcd_ctx->myLCD->LCDLineNumberOne;
		break;
	case 1:
		lineNo = lcd_ctx->myLCD->LCDLineNumberTwo;
		break;
	default:
		return NULL;
	}

	if (lcd_ctx->cells[cell].column != column ||
		lcd_ctx->cells[cell].row != lineNo)
			lcd_ctx->refresh = true;

	lcd_ctx->cells[cell].column = column;
	lcd_ctx->cells[cell].row = lineNo;
	return &lcd_ctx->cells[cell];
}

static void lcd_print(struct lcd_context_t *ctx)
{
	int i;

	ctx->myLCD->PCF8574_LCDClearScreen();

	if (ctx->wifiOn) {
		ctx->myLCD->PCF8574_LCDGOTO(ctx->myLCD->LCDLineNumberOne, 0);
		ctx->myLCD->PCF8574_LCDPrintCustomChar(WIFI_CHAR_INDEX);
	}

	if (ctx->mqttOn) {
		ctx->myLCD->PCF8574_LCDGOTO(ctx->myLCD->LCDLineNumberTwo, 0);
		ctx->myLCD->PCF8574_LCDPrintCustomChar(MQTT_CHAR_INDEX);
	}

	for (i = 0; i < MAX_CELLS; i++) {
		if (ctx->cells[i].dispaly == DISPLAY_NONE)
			continue;
		ctx->myLCD->PCF8574_LCDGOTO(ctx->cells[i].row, ctx->cells[i].column);
		switch(ctx->cells[i].dispaly) {
		case DISPLAY_TEXT:
			ctx->myLCD->PCF8574_LCDSendString(ctx->cells[i].data.text);
			break;
		case DISPLAY_INT:
			ctx->myLCD->print(ctx->cells[i].data.numInt);
			break;
		case DISPLAY_DOUBLE:
			ctx->myLCD->print(ctx->cells[i].data.numDouble, 2);
			break;
		default:
			break;
		}
	}

	ctx->refresh = false;
}

static void lcd_refresh(void *context)
{
	struct lcd_context_t *ctx = (struct lcd_context_t *)context;
	static int blinik_count = 0;

	if (WIFI_IS_CONNECTED) {
		if (!ctx->wifiOn)
			ctx->refresh = true;
		ctx->wifiOn = true;
	} else {
		if (blinik_count % LCD_BLINK_INERVAL == 0) {
			ctx->wifiOn = !ctx->wifiOn;
			ctx->refresh = true;
		}

	}

#ifdef HAVE_SYS_MQTT
	if (mqtt_is_connected()) {
		if (!ctx->mqttOn)
			ctx->refresh = true;
		ctx->mqttOn = true;
	} else {
		if (blinik_count % LCD_BLINK_INERVAL == 0) {
			ctx->mqttOn = !ctx->mqttOn;
			ctx->refresh = true;
		}
	}
#endif  /* HAVE_SYS_MQTT */

	blinik_count++;

	if (ctx->refresh)
		lcd_print(ctx);
}

static bool lcd_log(void *context)
{
	struct lcd_context_t *ctx = (struct lcd_context_t *)context;

	UNUSED(ctx);

	hlog_info(LCD_MODULE, "LCD attached");

	return true;
}

static void lcd_debug_set(uint32_t debug, void *context)
{
	struct lcd_context_t *ctx = (struct lcd_context_t *)context;

	if (ctx)
		ctx->debug = debug;
}

#ifdef __cplusplus
extern "C" {
#endif

int lcd_set_int(int cell, int row, int column, int num)
{
	lcd_cell *cellp = lcd_get_cell(cell, row, column);

	if (!cellp)
		return -1;
	if (cellp->data.numInt != num || cellp->dispaly != DISPLAY_INT)
		lcd_ctx->refresh = true;
	cellp->data.numInt = num;
	cellp->dispaly = DISPLAY_INT;

	return 0;
}

int lcd_set_double(int cell, int row, int column, double num)
{
	lcd_cell *cellp = lcd_get_cell(cell, row, column);

	if (!cellp)
		return -1;
	if (cellp->data.numDouble != num || cellp->dispaly != DISPLAY_DOUBLE)
		lcd_ctx->refresh = true;
	cellp->data.numDouble = num;
	cellp->dispaly = DISPLAY_DOUBLE;

	return 0;
}

int lcd_set_text(int cell, int row, int column, char *text)
{
	lcd_cell *cellp = lcd_get_cell(cell, row, column);

	if (!cellp)
		return -1;
	if (strncmp(cellp->data.text, text, MAX_STRING) || cellp->dispaly != DISPLAY_TEXT)
		lcd_ctx->refresh = true;
	strncpy(cellp->data.text, text, MAX_STRING);
	cellp->data.text[MAX_STRING] = 0;
	cellp->dispaly = DISPLAY_TEXT;

	return 0;
}

int lcd_clear_cell(int cell)
{
	if (!lcd_ctx || cell < 0 || cell >= MAX_CELLS)
		return -1;

	if (lcd_ctx->cells[cell].dispaly != DISPLAY_NONE)
		lcd_ctx->refresh = true;
	lcd_ctx->cells[cell].dispaly = DISPLAY_NONE;

	return 0;
}


void lcd_register(void)
{
	struct lcd_context_t *ctx = NULL;

	if (!lcd_init(&ctx))
		return;

	ctx->mod.name = CONST_STR(LCD_MODULE);
	ctx->mod.run = lcd_refresh;
	ctx->mod.log = lcd_log;
	ctx->mod.debug = lcd_debug_set;
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

#ifdef __cplusplus
}
#endif
