// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include <pico_fota_bootloader/core.h>
#include "herak_sys.h"
#include "common_internal.h"
#include "ota_internal.h"

#define TFTP_MODE	"octet"

static void *ota_tftp_open(const char *fname, const char *mode, u8_t is_write)
{
	struct ota_update_t *update = ota_update_context_get();

	UNUSED(mode);

	if (!is_write || !update || !update->started)
		return NULL;

	hlog_info(OTA_MODULE, "Updating .... %s", fname);
	sys_job_state_set(OTA_JOB);
	pfb_mark_download_slot_as_invalid();
	pfb_initialize_download_slot();
	mbedtls_sha256_init(&update->sha);
	mbedtls_sha256_starts(&update->sha, 0);
	update->buff_p = 0;
	update->flash_offset = 0;
	memset(update->buff, 0, BUFF_SIZE);
	update->in_progress = true;

	return update;
}

static int ota_buff_commit(struct ota_update_t *update, int size)
{
	if (pfb_write_to_flash_aligned_256_bytes(update->buff, update->flash_offset, BUFF_SIZE))
		return -1;

	if (mbedtls_sha256_update(&update->sha, update->buff, size))
		return -1;

	update->flash_offset += size;
	update->buff_p = 0;
	memset(update->buff, 0, BUFF_SIZE);
	return 0;
}

static void ota_tftp_close(void *handle)
{
	struct ota_update_t *ctx = (struct ota_update_t *)handle;

	if (!ctx)
		return;
	if (!ctx->in_progress) {
		ota_update_reset(ctx);
		return;
	}

	if (ctx->buff_p)
		ota_buff_commit(ctx, ctx->buff_p);
	ctx->ready = true;
}

static int ota_tftp_write(void *handle, struct pbuf *p)
{
	struct ota_update_t *ctx = (struct ota_update_t *)handle;
	int bsize, csize, wp = 0;
	int wsize = p->len;

	if (!ctx)
		return -1;

	if (!ctx->in_progress)
		ota_tftp_open(ctx->file.fname, TFTP_MODE, true);
	if (!ctx->in_progress)
		return -1;

	do {
		bsize = BUFF_SIZE - ctx->buff_p;
		csize = ((wsize <= bsize) ? wsize : bsize);
		memcpy(ctx->buff + ctx->buff_p, p->payload + wp, csize);
		ctx->buff_p += csize;
		wsize -= csize;
		wp += csize;

		if (ctx->buff_p == BUFF_SIZE) {
			if (ota_buff_commit(ctx, ctx->buff_p)) {
				hlog_warning(OTA_MODULE, "Failed to save the image chunk");
				return -1;
			}
		}
	} while (wsize > 0);

	return 0;
}

static int ota_tftp_read(void *handle, void *buff, int bytes)
{
	UNUSED(buff);
	UNUSED(bytes);
	UNUSED(handle);

	hlog_warning(OTA_MODULE, "Read not supported");
	return -1;
}

static void ota_tftp_error(void *handle, int err, const char *msg, int size)
{
	struct ota_update_t *update = (struct ota_update_t *)handle;

	if (!update)
		return;

	hlog_warning(OTA_MODULE, "Failed to get new firmware: %d [%s]",
				 err, (size > 1 && msg) ? msg : "");
	ota_update_reset(update);
}

static const struct tftp_context ota_tftp = {
	ota_tftp_open,
	ota_tftp_close,
	ota_tftp_read,
	ota_tftp_write,
	ota_tftp_error
};

void ota_update_reset(struct ota_update_t *update)
{
	update->in_progress = false;
	update->started = 0;
	update->buff_p = 0;
	update->ready = false;
	free(update->file.fname);
	free(update->file.peer);
	memset(update->buff, 0, BUFF_SIZE);
	memset(update->sha_verify, 0, SHA_BUFF_STR);
	memset(&(update->sha), 0, sizeof(update->sha));
	update->flash_offset = 0;
	if (!update->apply) {
		pfb_mark_download_slot_as_invalid();
		pfb_initialize_download_slot();
		sys_job_state_clear(OTA_JOB);
	}
#ifdef HAVE_SYS_WEBSERVER
	if (update->web_idx >= 0)
		webserv_client_close(update->web_idx);
#endif
	update->web_idx = -1;
}

int ota_update_validate(struct ota_update_t *update)
{
	char sha_buff[SHA_BUFF_STR] = {0};
	uint8_t sha[32] = {0};
	int res, ret = -1;
	int i;

	mbedtls_sha256_finish(&update->sha, sha);
	for (i = 0; i < 32; i++)
		sprintf(sha_buff+(2*i), "%02x", sha[i]);

	hlog_info(OTA_MODULE, "Got %d bytes", update->flash_offset);
	hlog_info(OTA_MODULE, "File SHA: %s", sha_buff);

	if (update->sha_verify[0]) {
		if (strncmp(update->sha_verify, sha_buff, SHA_BUFF_STR)) {
			hlog_warning(OTA_MODULE, "Invalid image");
			goto out;
		}
	}
	res = pfb_firmware_sha256_check(update->flash_offset);
	if (res) {
		hlog_warning(OTA_MODULE, "Invalid image");
	} else {
		hlog_info(OTA_MODULE, "Valid image, going to boot it ... ");
		pfb_mark_download_slot_as_valid();
		update->apply = time_ms_since_boot();
		ret = 0;
	}

out:
	ota_update_reset(update);
	return ret;
}

int ota_update_start(struct ota_update_t *update)
{
	if (tftp_file_get(&ota_tftp, &(update->file), update))
		return -1;

	update->started = time_ms_since_boot();
	return 0;
}
