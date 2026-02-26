// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_WIFI_API_H_
#define _LIB_SYS_WIFI_API_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_OFF,
    WIFI_CONNECTED,
    WIFI_NOT_CONNECTED
} wifi_state_t;

wifi_state_t wifi_get_state(void);

#define WIFI_IS_CONNECTED   (wifi_get_state() == WIFI_CONNECTED)

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_WIFI_API_H_ */

