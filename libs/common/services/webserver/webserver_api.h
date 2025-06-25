// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_WS_API_H_
#define _LIB_SYS_WS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int client_idx;
	bool keep_open;
	bool keep_silent;
	int hret;
} run_context_web_t;

#ifdef HAVE_SYS_WEBSERVER
#define	WEBCTX_SET_KEEP_OPEN(C, V)	\
	do { \
		if (((C)->type == CMD_CTX_WEB))\
			((run_context_web_t *)(C)->context)->keep_open = (V);\
	} while (0)
#define	WEBCTX_SET_KEEP_SILENT(C, V)	\
	do { \
		if (((C)->type == CMD_CTX_WEB))\
			((run_context_web_t *)(C)->context)->keep_silent = (V);\
	} while (0)

// C - cmd_run_context_t; S - log string
#define WEB_CLIENT_REPLY(C, S)\
	do {if ((C)->type == CMD_CTX_WEB) {\
		webserv_client_send_data(((run_context_web_t *)((C)->context))->client_idx, (S), strlen((S)));\
	}} while (0)
#else /* HAVE_SYS_WEBSERVER */
#define WEBCTX_SET_KEEP_OPEN(C, S)  { (void)(C); (void)(S); }
#define WEBCTX_SET_KEEP_SILENT(C, S)  { (void)(C); (void)(S); }
#define WEB_CLIENT_REPLY(C, S)  { (void)(C); (void)(S); }
#endif /* HAVE_SYS_WEBSERVER */

int webserv_client_send_data(int client_idx, char *data, int datalen);

int webserv_port(void);
int webserv_client_close(int client_idx);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_WS_API_H_ */

