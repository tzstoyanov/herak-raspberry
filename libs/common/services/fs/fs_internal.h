// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_FS_INTERNAL_H_
#define _LIB_SYS_FS_INTERNAL_H_

#define FS_MODULE	"fs"

struct fs_context_t;

#define MAX_OPENED_FILES	10
#define IS_DEBUG(C)	((C) && (C)->debug)

struct fs_file_copy_t {
	struct tftp_file_t src;
	struct tftp_file_t dst;
	int local_fd;
	uint64_t started;
	int web_idx;
	struct fs_context_t *fs_ctx;
};
struct fs_context_t {
	sys_module_t mod;
	uint32_t debug;
	struct fs_file_copy_t copy_job;
	int open_fd[MAX_OPENED_FILES];
};

struct fs_context_t *fs_context_get(void);
void fs_cp_reset(struct fs_file_copy_t *copy);
const struct tftp_context *fs_tftp_hooks_get(void);

#endif /* _LIB_SYS_FS_INTERNAL_H_ */

