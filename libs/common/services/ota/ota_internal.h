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

#define UPDATE_TIMEOUT_MS	300000 // 5 min

/* Must be multiple of 256 */
#define BUFF_SIZE		256
#define META_SIZE		512

#define TFTP_MODE	"octet"

#define SHA_BUFF_STR	65

#define OTA_MQTT_SENSORS		3
#define OTA_MQTT_DATA_LEN		512
#define OTA_MQTT_INTERVAL_MS	3600000 // MQTT publish on 1 hout

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
	uint64_t debug_last_dump;
	struct ota_context_t *ota;
};

struct ota_check_t {
	bool in_progress;
	struct tftp_file_t file;
	char buff[META_SIZE];
	uint32_t buff_p;
	uint64_t started;
	bool ready;
	bool apply;
	bool new_version;
	bool check_newer;
	bool check_ver;
	bool check_time;
	char *meta_file_name;
	char *param_cache;
	struct ota_context_t *ota;
};

struct ota_context_t {
	sys_module_t mod;
	uint32_t debug;
	struct ota_update_t update;
	struct ota_check_t check;
	uint64_t mqtt_last_send;
	mqtt_component_t mqtt_comp[OTA_MQTT_SENSORS];
	char mqtt_payload[OTA_MQTT_DATA_LEN + 1];
};

int ota_update_start(struct ota_update_t *update);
void ota_update_reset(struct ota_update_t *update);
void ota_update_run(struct ota_update_t *update);

int ota_check_start(struct ota_check_t *update);
void ota_check_reset(struct ota_check_t *update);
void ota_check_run(struct ota_check_t *check);
void ota_check_log(struct ota_check_t *check);
void ota_check_set_strategy(struct ota_check_t *check, bool newer, bool ver, bool time);
int ota_update_apply(struct ota_check_t *check);
int ota_update_get_new(struct ota_check_t *check,
					   char **name, char **ver, char **commit,
					   char **date, char **time, char **peer);

struct ota_update_t *ota_update_context_get(void);
struct ota_check_t *ota_check_context_get(void);

#endif /* _LIB_SYS_OTA_INTERNAL_H_ */

