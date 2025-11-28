// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "herak_sys.h"
#include "common_internal.h"
#include "ota_internal.h"

enum ota_meta_ids {
	OTA_IMAGE_ID = 0,
	OTA_FILE_ID,
	OTA_SHA_ID,
	OTA_VER_ID,
	OTA_COMMIT_ID,
	OTA_BDATE_ID,
	OTA_BTIME_ID,
	OTA_DARCH_ID,
	OTA_MAX_ID
};

static struct {
	const int id;
	const char *current;
	const char *name;
	char *value;
} ota_update_mdata[OTA_MAX_ID] = {
	{ OTA_IMAGE_ID, IMAGE_NAME, "image:", NULL },
	{ OTA_FILE_ID, IMAGE_FILE, "file:", NULL },
	{ OTA_SHA_ID, NULL, "SHA:", NULL },
	{ OTA_VER_ID, PROJECT_VERSION, "version:", NULL },
	{ OTA_COMMIT_ID, GIT_COMMIT_HASH, "commit:", NULL },
	{ OTA_BDATE_ID, BUILD_DATE, "build date:", NULL },
	{ OTA_BTIME_ID, BUILD_TIME, "build time:", NULL },
	{ OTA_DARCH_ID, DEV_ARCH, "device arch:", NULL }
};

static void *ota_tftp_check_open(const char *fname, const char *mode, u8_t is_write)
{
	struct ota_check_t *check = ota_check_context_get();

	UNUSED(mode);

	if (!is_write || !check || !check->started || !fname)
		return NULL;
	if (strlen(fname) != strlen(check->file.fname))
		return NULL;
	if (strcmp(fname, check->file.fname))
		return NULL;

	check->buff_p = 0;
	memset(check->buff, 0, BUFF_SIZE);
	check->in_progress = true;

	return check;
}

static void ota_tftp_check_close(void *handle)
{
	struct ota_check_t *ctx = (struct ota_check_t *)handle;

	if (!ctx)
		return;
	if (!ctx->in_progress) {
		ota_check_reset(ctx);
		return;
	}
	if (IS_DEBUG(ctx->ota))
		hlog_info(OTA_MODULE, "Received image meta file");

	ctx->ready = true;
}

static int ota_tftp_check_write(void *handle, struct pbuf *p)
{
	struct ota_check_t *ctx = (struct ota_check_t *)handle;
	int bsize;

	if (!ctx)
		return -1;

	if (!ctx->in_progress)
		ota_tftp_check_open(ctx->file.fname, TFTP_MODE, true);
	if (!ctx->in_progress)
		return -1;

	bsize = (META_SIZE - 1) - ctx->buff_p;
	if (p->len >= bsize) {
		hlog_warning(OTA_MODULE, "Image meta file is larger than %d bytes, fail to get it", META_SIZE);
		return -1;
	}
	if (IS_DEBUG(ctx->ota))
		hlog_info(OTA_MODULE, "Got %d bytes meta file", p->len);

	memcpy(ctx->buff + ctx->buff_p, p->payload, p->len);
	ctx->buff_p += p->len;

	return 0;
}

static int ota_tftp_check_read(void *handle, void *buff, int bytes)
{
	UNUSED(buff);
	UNUSED(bytes);
	UNUSED(handle);

	hlog_warning(OTA_MODULE, "Read not supported");
	return -1;
}

static void ota_tftp_check_error(void *handle, int err, const char *msg, int size)
{
	struct ota_check_t *update = (struct ota_check_t *)handle;

	if (!update)
		return;

	hlog_warning(OTA_MODULE, "Failed to get new image meta file: %d [%s]",
				 err, (size > 1 && msg) ? msg : "");
	ota_check_reset(update);
}

static const struct tftp_context ota_tftp_check = {
	ota_tftp_check_open,
	ota_tftp_check_close,
	ota_tftp_check_read,
	ota_tftp_check_write,
	ota_tftp_check_error
};

static int ota_meta_parse(struct ota_check_t *check)
{
	int i, j;

	if (check->buff_p < 1)
		return -1;
printf("\n\r ota_meta_parse [%s]\n\r", check->buff);
	for (i = 0; i < OTA_MAX_ID; i++) {
		ota_update_mdata[i].value = strstr(check->buff, ota_update_mdata[i].name);
		if (!ota_update_mdata[i].value) {
			printf("\n\r 1: %d\n\r", i);
			return -1;
		}
		ota_update_mdata[i].value += strlen(ota_update_mdata[i].name);
		while (*(ota_update_mdata[i].value) == ' ' || *(ota_update_mdata[i].value) == '\t')
			ota_update_mdata[i].value++;
		if (*(ota_update_mdata[i].value) == '\0') {
			printf("\n\r 2: %d\n\r", i);
			return -1;
		}
	}
	for (i = 0; i < OTA_MAX_ID; i++) {
		j = 0;
		while (ota_update_mdata[i].value[j] != '\0' &&
			  ota_update_mdata[i].value[j] != '\n' &&
			  ota_update_mdata[i].value[j] != '\r')
			j++;
		ota_update_mdata[i].value[j] = '\0';
		if (IS_DEBUG(check->ota))
			hlog_info(OTA_MODULE, "Got metadata %s %s",
					   ota_update_mdata[i].name, ota_update_mdata[i].value);
	}
	return 0;
}

#define META_CHECK(I) \
	do {\
		if (!ota_update_mdata[(I)].current || !ota_update_mdata[(I)].value || \
			strlen(ota_update_mdata[(I)].current) != strlen(ota_update_mdata[(I)].value) ||\
			(strcmp(ota_update_mdata[(I)].current, ota_update_mdata[(I)].value))) {\
			hlog_warning(OTA_MODULE, "Invalid meta field %s [%s] != [%s]",\
						ota_update_mdata[(I)].name,\
						ota_update_mdata[(I)].current,\
						ota_update_mdata[(I)].value);\
			return -1;\
		} \
	} while (0)\

static int ota_meta_validate(struct ota_check_t *check)
{
	UNUSED(check);

	META_CHECK(OTA_DARCH_ID);
	META_CHECK(OTA_IMAGE_ID);
	return 0;
}

#define STR_BUFF_LEN	64
#define GET_DIGIT_VERIFY(V, D, MIN, MAX)\
	do {\
		tok = strtok_r(rest, (D), &rest); \
		if (!tok)\
			return -1;\
		(V) = strtol(tok, NULL, 10);\
		if ((V) < (MIN))\
			return -1;\
		if ((MAX) > (MIN) && (V) > (MAX))\
			return -1;\
	} while (0)

static int ota_str2time(char *date, char *time, time_t *t)
{
	char str[STR_BUFF_LEN + 1], *tok, *rest;
	struct tm res = {0};

	strncpy(str, date, STR_BUFF_LEN);
	rest = str;
	GET_DIGIT_VERIFY(res.tm_mday, ".", 1, 31);
	GET_DIGIT_VERIFY(res.tm_mon, ".", 1, 12);
	res.tm_mon--;
	GET_DIGIT_VERIFY(res.tm_year, ".", 1900, -1);
	res.tm_year -= 1900;

	strncpy(str, time, STR_BUFF_LEN);
	rest = str;
	GET_DIGIT_VERIFY(res.tm_hour, ":", 0, 23);
	GET_DIGIT_VERIFY(res.tm_min, ":", 0, 59);
	GET_DIGIT_VERIFY(res.tm_sec, ":", 0, 60);
	time2epoch(&res, t);
	return 0;
}

#define GET_DIGIT(V, D)	{\
		tok = strtok_r(rest, (D), &rest); \
		if (!tok)\
			return -1;\
		(V) = strtol(tok, NULL, 10);\
	}
static int ota_str2ver(char *version, int *ver_maj, int *ver_mid, int *ver_min)
{
	char str[STR_BUFF_LEN + 1], *tok, *rest;
	int vmin, vmid, vmaj;

	strncpy(str, version, STR_BUFF_LEN);
	rest = str;
	GET_DIGIT(vmaj, ".");
	GET_DIGIT(vmid, ".");
	GET_DIGIT(vmin, ".");

	*ver_maj = vmaj;
	*ver_mid = vmid;
	*ver_min = vmin;

	return 0;
}

static void ota_meta_check_update(struct ota_check_t *check)
{
	static time_t running;
	static int vmin_r, vmid_r, vmaj_r;
	int vmin_n = 0, vmid_n = 0, vmaj_n = 0;
	time_t new_time = 0;
	bool new = false;

	if (!running) {
		if (ota_str2ver(PROJECT_VERSION, &vmaj_r, &vmid_r, &vmin_r))
			return;
		if (ota_str2time(BUILD_DATE, BUILD_TIME, &running))
			return;
	}

	if (IS_DEBUG(check->ota)) {
		hlog_info(OTA_MODULE, "Check strategy: %s, %s, %s",
				  check->check_newer ? "latest" : "any",
				  check->check_ver ? "check version" : "does not check version",
				  check->check_time ? "check built time" : "does not check build time");
		hlog_info(OTA_MODULE, "Compare meta data:");
		hlog_info(OTA_MODULE, "\tVersion local [%s] <-> remote [%s]",
				  PROJECT_VERSION, ota_update_mdata[OTA_VER_ID].value);
		hlog_info(OTA_MODULE, "\tBuild time local [%s %s] <-> remote [%s %s]",
				  BUILD_DATE, BUILD_TIME,
				  ota_update_mdata[OTA_BDATE_ID].value,
				  ota_update_mdata[OTA_BTIME_ID].value);
	}

	if (ota_str2ver(ota_update_mdata[OTA_VER_ID].value, &vmaj_n, &vmid_n, &vmin_n))
		return;
	if (ota_str2time(ota_update_mdata[OTA_BDATE_ID].value,
					 ota_update_mdata[OTA_BTIME_ID].value, &new_time))
		return;

	if (check->check_ver) {
		if (!check->check_newer) {
			if (vmaj_r != vmaj_n || vmid_r != vmid_n || vmin_r != vmin_n)
				new = true;
		} else {
			if (vmaj_r < vmaj_n)
				new = true;
			else if (vmaj_r == vmaj_n && vmid_r < vmid_n)
				new = true;
			else if (vmaj_r == vmaj_n && vmid_r == vmid_n && vmin_r < vmin_n)
				new = true;
		}
	}

	if (check->check_time) {
		if (!check->check_newer) {
			if (running != new_time)
				new = true;
		} else {
			if (running < new_time)
				new = true;
		}
	}

	if (check->new_version != new)
		check->ota->mqtt_comp[0].force = true;

	check->new_version = new;
}

void ota_check_reset(struct ota_check_t *check)
{
	int i;

	check->started = 0;
	check->in_progress = false;
	check->apply = false;
	check->ready = false;
	check->new_version = false;
	check->ota->mqtt_comp[0].force = true;

	for (i = 0; i < OTA_MAX_ID; i++)
		ota_update_mdata[i].value = NULL;
}

int ota_check_start(struct ota_check_t *check)
{
	if (tftp_file_get(&ota_tftp_check, &(check->file), check))
		return -1;

	if (IS_DEBUG(check->ota))
		hlog_info(OTA_MODULE, "Starting update check: %s from %s",
				  check->file.fname, check->file.peer);
	check->started = time_ms_since_boot();
	return 0;
}

void ota_check_log(struct ota_check_t *check)
{
	int i;

	hlog_info(OTA_MODULE, "Auto update strategy: %s, %s, %s",
			  check->check_newer ? "latest" : "any",
			  check->check_ver ? "check version" : "does not check version",
			  check->check_time ? "check built time" : "does not check build time");

	if (!check->ready || !check->new_version) {
		hlog_info(OTA_MODULE, "No new version available");
		return;
	}

	hlog_info(OTA_MODULE, "New version detected on %s:", check->file.peer);
	for (i = 0; i < OTA_MAX_ID; i++) {
		if (!ota_update_mdata[i].value)
			continue;
		hlog_info(OTA_MODULE, "\t%s %s", ota_update_mdata[i].name, ota_update_mdata[i].value);
	}
}

void ota_check_run(struct ota_check_t *check)
{
	uint64_t now;

	if (check->ready) {
		if (ota_meta_parse(check)) {
			hlog_warning(OTA_MODULE, "Invalid image meta file");
			goto out_reset;
		}
		if (ota_meta_validate(check)) {
			hlog_warning(OTA_MODULE, "Failed to validate image meta file");
			goto out_reset;
		}
		ota_meta_check_update(check);
		ota_check_log(check);
		check->in_progress = false;
		check->started = false;
		return;
	}

	now = time_ms_since_boot();
	if ((now - check->started) < UPDATE_TIMEOUT_MS)
		return;

	hlog_warning(OTA_MODULE, "Timeout reading file %s from server %s:%d.",
			     check->file.fname, check->file.peer, check->file.port);

out_reset:
	ota_check_reset(check);
}

void ota_check_set_strategy(struct ota_check_t *check, bool newer, bool ver, bool time)
{
	check->check_newer = newer;
	check->check_ver = ver;
	check->check_time = time;
	if (IS_DEBUG(check->ota))
		hlog_info(OTA_MODULE, "Set auto update strategy: %s, %s, %s",
				  newer ? "latest" : "any",
				  ver ? "check version" : "does not check version",
				  time ? "check built time" : "does not check build time");
}

int ota_update_apply(struct ota_check_t *check)
{
	char *path = NULL;
	int i;

	check->ota->update.file.peer = strdup(check->file.peer);
	if (!check->ota->update.file.peer)
		goto out_err;
	check->ota->update.file.port = check->file.port;
	if (strlen(ota_update_mdata[OTA_SHA_ID].value) == (SHA_BUFF_STR - 1)) {
		memcpy(check->ota->update.sha_verify, ota_update_mdata[OTA_SHA_ID].value, (SHA_BUFF_STR - 1));
		check->ota->update.sha_verify[SHA_BUFF_STR - 1] = 0;
	}
	i = strlen(check->file.fname);
	do {
		if (check->file.fname[i] == '/')
			break;
	} while (i--);
	if (i > 0) {
		path = strdup(check->file.fname);
		if (!path)
			goto out_err;
		path[i] = 0;
	}
	if (path) {
		sys_asprintf(&check->ota->update.file.fname, "%s/%s",
					 path, ota_update_mdata[OTA_FILE_ID].value);
		free(path);
	} else {
		check->ota->update.file.fname = strdup(ota_update_mdata[OTA_FILE_ID].value);
	}

	if (!check->ota->update.file.fname)
		goto out_err;

	if (ota_update_start(&check->ota->update))
		goto out_err;

	return 0;

out_err:
	if (IS_DEBUG(check->ota))
		hlog_warning(OTA_MODULE, "Cannot apply auto update %s from %s",
					 ota_update_mdata[OTA_FILE_ID].value, check->file.peer);
	free(check->ota->update.file.peer);
	check->ota->update.file.peer = NULL;
	free(check->ota->update.file.fname);
	check->ota->update.file.fname = NULL;

	return -1;
}

int ota_update_get_new(struct ota_check_t *check,
					   char **name, char **ver, char **commit,
					   char **date, char **time, char **peer)
{
	if (!check->ready || !check->new_version)
		return -1;

	if (name)
		*name = ota_update_mdata[OTA_IMAGE_ID].value;
	if (ver)
		*ver = ota_update_mdata[OTA_VER_ID].value;
	if (commit)
		*commit = ota_update_mdata[OTA_COMMIT_ID].value;
	if (date)
		*date = ota_update_mdata[OTA_BDATE_ID].value;
	if (time)
		*time = ota_update_mdata[OTA_BTIME_ID].value;
	if (peer)
		*peer = check->file.peer;

	return 0;
}
