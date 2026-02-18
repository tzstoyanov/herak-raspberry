// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_SCRIPTS_API_H_
#define _LIB_SYS_SCRIPTS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

int script_exist(char *name, bool prefix_match);
int script_run(char *name, bool prefix_match);
int script_auto(char *name, bool prefix_match, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_SCRIPTS_API_H_ */

