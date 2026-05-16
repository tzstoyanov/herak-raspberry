// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _RSENSOR_H_
#define _RSENSOR_H_

int rsensor_get_index(char *name);
int rsensor_get_value(int index, float *val);

#endif /* _RSENSOR_H_ */

