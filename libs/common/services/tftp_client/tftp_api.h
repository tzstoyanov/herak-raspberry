// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_TFTP_API_H_
#define _LIB_SYS_TFTP_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/apps/tftp_client.h"

struct tftp_file_t {
	char *fname;
	char *peer;
	int port;
};
int tftp_url_parse(char *url, struct tftp_file_t *file);

int tftp_file_get(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context);
int tftp_file_put(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_TFTP_API_H_ */

