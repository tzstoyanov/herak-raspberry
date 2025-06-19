// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_FS_API_H_
#define _LIB_SYS_FS_API_H_

#ifdef __cplusplus
extern "C" {
#endif

bool fs_is_mounted(void);
char *fs_get_err_msg(int err);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_FS_API_H_ */

