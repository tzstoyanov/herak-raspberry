// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_OTA_INTERNAL_H_
#define _LIB_SYS_OTA_INTERNAL_H_

#include "lwip/apps/tftp_client.h"
#include "mbedtls/sha256.h"

#define OTA_MODULE	"ota"
#define IS_DEBUG(C)	((C) && (C)->debug)

/* Must be multiple of 256 */
#define BUFF_SIZE		256

#define SHA_BUFF_STR	65

struct ota_context_t;

struct ota_update_t {
	bool in_progress;
	struct tftp_file_t file;
	mbedtls_sha256_context sha;
	char sha_verify[SHA_BUFF_STR];
	uint8_t buff[BUFF_SIZE];
	uint32_t buff_p;
	uint32_t flash_offset;
	uint64_t started;
	bool ready;
	uint64_t apply;
	int web_idx;
	struct ota_context_t *ota;
};

struct ota_context_t {
	sys_module_t mod;
	uint32_t debug;
	uint64_t debug_last_dump;
	struct ota_update_t update;
};

void ota_update_reset(struct ota_update_t *update);
int ota_update_validate(struct ota_update_t *update);
int ota_update_start(struct ota_update_t *update);
struct ota_update_t *ota_update_context_get(void);

#endif /* _LIB_SYS_OTA_INTERNAL_H_ */

