// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_SYSCOMMANDS_API_H_
#define _LIB_SYS_SYSCOMMANDS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

int syscmd_log_send(char *logbuff);
void debug_log_forward(int client_idx);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_SYSCOMMANDS_API_H_ */

