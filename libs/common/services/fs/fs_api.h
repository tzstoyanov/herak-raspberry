// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_FS_API_H_
#define _LIB_SYS_FS_API_H_

#ifdef HAVE_SYS_FS
#include "pico_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MAX_FILE_PATH		LFS_NAME_MAX

bool fs_is_mounted(void);
char *fs_get_err_msg(int err);
int fs_get_files_count(char *dir_path, char *ext);

int fs_open(char *path, enum lfs_open_flags flags);
void fs_close(int fd);
int fs_gets(int fd, char *buff, int buff_size);
int fs_read(int fd, char *buff, int buff_size);
int fs_write(int fd, char *buff, int buff_size);


#ifdef __cplusplus
}
#endif

#endif /* HAVE_SYS_FS */

#endif /* _LIB_SYS_FS_API_H_ */

