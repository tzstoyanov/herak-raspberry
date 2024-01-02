// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>

#include "common_internal.h"
#include "pico/stdlib.h"
#include "hd44780/HD44780_LCD_PCF8574.hpp"

#include "base64.h"
#include "params.h"

#define WIFI_CHAR_INDEX	0
#define MQTT_CHAR_INDEX	1
#define MAX_STRING	14
#define MAX_CELLS	4
#define LCD_BLINK_INERVAL	2

#define WIFI_SYMBOL {0x04, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x11, 0x00}
#define MQTT_SYMBOL {0x00, 0x00, 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x1F}

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

struct {
	HD44780LCD *myLCD;
	bool wifiOn;
	bool mqttOn;
	lcd_cell cells[MAX_CELLS];
	bool refresh;
}static lcd_context;

bool get_lcd_config(int *address, int *clock, int *sda, int *scl)
{
	char *lcd_config = param_get(LCD_CONFIG);
	char *config_tokens[4];
	char *rest, *tok;
	bool ret = false;
	int i;

	if (strlen(lcd_config) < 1)
		goto out;
	rest = lcd_config;
	while (i < 4 && (tok = strtok_r(rest, ";", &rest)))
		config_tokens[i++] = tok;
	if (i != 4)
		goto out;
	(*address) = (int)strtol(config_tokens[0], NULL, 16);
	if ((*address) < 0 || (*address) >= 0xFFFF)
		goto out;

	(*clock) = (int)strtol(config_tokens[1], NULL, 10);
	if ((*clock) < 0 || (*clock) >= 0xFFFF)
		goto out;

	(*sda) = (int)strtol(config_tokens[2], NULL, 10);
	if ((*sda) < 0 || (*sda) >= 0xFFFF)
		goto out;

	(*scl) = (int)strtol(config_tokens[3], NULL, 10);
	if ((*scl) < 0 || (*scl) >= 0xFFFF)
		goto out;

	ret = true;

out:
	free(lcd_config);
	return ret;
}

bool lcd_init()
{
	uint8_t symWifi[8] = WIFI_SYMBOL;
	uint8_t symMqtt[8] = {0x00, 0x00, 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x1F};
	int address, clock, sda, scl;

	memset(&lcd_context, 0, sizeof(lcd_context));
	if (!get_lcd_config(&address, &clock, &sda, &scl))
		return false;

	lcd_context.myLCD = new HD44780LCD(address, i2c0, clock, sda, scl);
	if (!lcd_context.myLCD)
		return false;

	lcd_context.myLCD->PCF8574_LCDInit(lcd_context.myLCD->LCDCursorTypeOff, 2, 16);
	lcd_context.myLCD->PCF8574_LCDClearScreen();
	lcd_context.myLCD->PCF8574_LCDBackLightSet(true);
	lcd_context.myLCD->PCF8574_LCDCreateCustomChar(WIFI_CHAR_INDEX, symWifi);
	lcd_context.myLCD->PCF8574_LCDCreateCustomChar(MQTT_CHAR_INDEX, symMqtt);
	lcd_context.refresh = true;

	return true;
}

static lcd_cell *lcd_get_cell(int cell, int row, int column)
{
	HD44780LCD::LCDLineNumber_e lineNo;

	if (cell < 0 || cell >= MAX_CELLS || column < 1 || column > 15)
		return NULL;

	switch(row) {
	case 0:
		lineNo = lcd_context.myLCD->LCDLineNumberOne;
		break;
	case 1:
		lineNo = lcd_context.myLCD->LCDLineNumberTwo;
		break;
	default:
		return NULL;
	}

	if (lcd_context.cells[cell].column != column ||
		lcd_context.cells[cell].row != lineNo)
			lcd_context.refresh = true;

	lcd_context.cells[cell].column = column;
	lcd_context.cells[cell].row = lineNo;
	return &lcd_context.cells[cell];
}

int lcd_set_int(int idx, int row, int column, int num)
{
	lcd_cell *cell = lcd_get_cell(idx, row, column);

	if (!cell)
		return -1;
	if (cell->data.numInt != num || cell->dispaly != DISPLAY_INT)
		lcd_context.refresh = true;
	cell->data.numInt = num;
	cell->dispaly = DISPLAY_INT;

	return 0;
}

int lcd_set_double(int idx, int row, int column, double num)
{
	lcd_cell *cell = lcd_get_cell(idx, row, column);

	if (!cell)
		return -1;
	if (cell->data.numDouble != num || cell->dispaly != DISPLAY_DOUBLE)
		lcd_context.refresh = true;
	cell->data.numDouble = num;
	cell->dispaly = DISPLAY_DOUBLE;

	return 0;
}

int lcd_set_text(int idx, int row, int column, char *text)
{
	lcd_cell *cell = lcd_get_cell(idx, row, column);

	if (!cell)
		return -1;
	if (strncmp(cell->data.text, text, MAX_STRING) || cell->dispaly != DISPLAY_TEXT)
		lcd_context.refresh = true;
	strncpy(cell->data.text, text, MAX_STRING);
	cell->data.text[MAX_STRING] = 0;
	cell->dispaly = DISPLAY_TEXT;

	return 0;
}

int lcd_clear_cell(int idx)
{
	if (idx < 0 || idx >= MAX_CELLS)
		return -1;

	if (lcd_context.cells[idx].dispaly != DISPLAY_NONE)
		lcd_context.refresh = true;
	lcd_context.cells[idx].dispaly = DISPLAY_NONE;

	return 0;
}

static void lcd_print()
{
	int i;

	lcd_context.myLCD->PCF8574_LCDClearScreen();

	if (lcd_context.wifiOn) {
		lcd_context.myLCD->PCF8574_LCDGOTO(lcd_context.myLCD->LCDLineNumberOne, 0);
		lcd_context.myLCD->PCF8574_LCDPrintCustomChar(WIFI_CHAR_INDEX);
	}

	if (lcd_context.mqttOn) {
		lcd_context.myLCD->PCF8574_LCDGOTO(lcd_context.myLCD->LCDLineNumberTwo, 0);
		lcd_context.myLCD->PCF8574_LCDPrintCustomChar(MQTT_CHAR_INDEX);
	}

	for (i = 0; i < MAX_CELLS; i++) {
		if (lcd_context.cells[i].dispaly == DISPLAY_NONE)
			continue;
		lcd_context.myLCD->PCF8574_LCDGOTO(lcd_context.cells[i].row, lcd_context.cells[i].column);
		switch(lcd_context.cells[i].dispaly) {
		case DISPLAY_TEXT:
			lcd_context.myLCD->PCF8574_LCDSendString(lcd_context.cells[i].data.text);
			break;
		case DISPLAY_INT:
			lcd_context.myLCD->print(lcd_context.cells[i].data.numInt);
			break;
		case DISPLAY_DOUBLE:
			lcd_context.myLCD->print(lcd_context.cells[i].data.numDouble, 2);
			break;
		default:
			break;
		}
	}

	lcd_context.refresh = false;
}

void lcd_refresh()
{
	static int blinik_count = 0;

	if (wifi_is_connected()) {
		if (!lcd_context.wifiOn)
			lcd_context.refresh = true;
		lcd_context.wifiOn = true;
	} else {
		if (blinik_count % LCD_BLINK_INERVAL == 0) {
			lcd_context.wifiOn = !lcd_context.wifiOn;
			lcd_context.refresh = true;
		}

	}

	if (mqtt_is_connected()) {
		if (!lcd_context.mqttOn)
			lcd_context.refresh = true;
		lcd_context.mqttOn = true;
	} else {
		if (blinik_count % LCD_BLINK_INERVAL == 0) {
			lcd_context.mqttOn = !lcd_context.mqttOn;
			lcd_context.refresh = true;
		}
	}

	blinik_count++;

	if (lcd_context.refresh)
		lcd_print();
}
