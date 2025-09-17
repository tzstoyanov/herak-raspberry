// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _SYS_STATUS_API_H_
#define _SYS_STATUS_API_H_

typedef bool (*log_status_cb_t) (void *context);
int add_status_callback(log_status_cb_t cb, void *user_context);
void system_set_periodic_log_ms(int ms);

void system_log_status(void);
bool system_log_in_progress(void);

#endif /* _SYS_STATUS_API_H_ */
