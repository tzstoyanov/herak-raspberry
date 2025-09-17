// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _SYS_STATUS_API_H_
#define _SYS_STATUS_API_H_

typedef bool (*log_status_cb_t) (void *context);
int sys_state_callback_add(log_status_cb_t cb, void *user_context);
void sys_state_set_periodic_log_ms(int ms);

void sys_state_log_status(void);
bool sys_state_log_in_progress(void);

#endif /* _SYS_STATUS_API_H_ */
