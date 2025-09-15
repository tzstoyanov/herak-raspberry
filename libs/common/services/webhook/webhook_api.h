// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_WH_API_H_
#define _LIB_SYS_WH_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#define	WH_PAYLOAD_MAX_SIZE	384

bool webhook_connected(void);
int webhook_send(char *message);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_WH_API_H_ */

