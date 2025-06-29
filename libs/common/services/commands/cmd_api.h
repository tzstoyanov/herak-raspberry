// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_COMMANDS_API_H_
#define _LIB_SYS_COMMANDS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

enum run_type_t {
	CMD_CTX_WEB,
	CMD_CTX_MQTT,
	CMD_CTX_SCRIPT,
};

typedef struct {
	enum run_type_t	type;
	void            *context;
} cmd_run_context_t;

typedef int (*app_command_cb_t) (cmd_run_context_t *ctx, char *cmd, char *params, void *user_data);
typedef struct {
	char *command;
	char *help;
	app_command_cb_t cb;
} app_command_t;

int cmd_handler_add(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data);
int cmd_exec(cmd_run_context_t *cmd_ctx, char *cmd_str);
void cmd_module_help(char *module);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_COMMANDS_API_H_ */

