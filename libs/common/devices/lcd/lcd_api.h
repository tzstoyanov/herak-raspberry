// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_LCD_API_H_
#define _LIB_LCD_API_H_

int lcd_clear_cell(int cell);
int lcd_set_text(int cell, int row, int column, char *text);
int lcd_set_double(int cell, int row, int column, double num);
int lcd_set_int(int cell, int row, int column, int num);

#endif /* _LIB_LCD_API_H_*/

