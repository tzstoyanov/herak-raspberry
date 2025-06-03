// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_WH_API_H_
#define _LIB_SYS_WH_API_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*webhook_reply_t) (int idx, int http_code, void *context);
int webhook_state(int idx, bool *connected, bool *sending);
int webhook_send(int idx, char *data, int datalen);
int webhook_add(char *addr, int port, char *content_type, char *endpoint, char *http_command,
				bool keep_open, webhook_reply_t user_cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_WH_API_H_ */

