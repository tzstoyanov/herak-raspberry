// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_WS_API_H_
#define _LIB_SYS_WS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

// C - cmd_run_context_t; S - log string
#ifdef HAVE_SYS_WEBSERVER
#define WEB_CLIENT_REPLY(C, S)\
	do {if ((C)->type == CMD_CTX_WEB) {\
		webserv_client_send_data((C)->context.web.client_idx, (S), strlen((S)));\
	}} while (0)
#else /* HAVE_SYS_WEBSERVER */
#define WEB_CLIENT_REPLY(C, S)  { (void)(C); (void)(S); }
#endif /* HAVE_SYS_WEBSERVER */

enum http_response_id {
	HTTP_RESP_OK = 0,
	HTTP_RESP_BAD,
	HTTP_RESP_NOT_FOUND,
	HTTP_RESP_INTERNAL_ERROR,
	HTTP_RESP_TOO_MANY_ERROR,
	HTTP_RESP_MAX
};
typedef enum http_response_id (*webserv_request_cb_t) (run_context_web_t *wctx, char *cmd, char *url, void *context);
int webserv_client_send(int client_idx, char *data, int datalen, enum http_response_id rep);
int webserv_client_send_data(int client_idx, char *data, int datalen);

#define WEB_CMD_NR   "\r\n"
/* Web commands API */
int webserv_add_commands(char *url, app_command_t *commands, int commands_cont, char *description, void *user_data);

int webserv_port(void);
int webserv_client_close(int client_idx);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_WS_API_H_ */

