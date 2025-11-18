// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico_hal.h"
#include "lwip/dns.h"
#include "lwip/apps/tftp_client.h"

#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#define TFTP_CLIENT_MODULE		"tftp"
#define TFTP_URL	"tftp://"
#define MAX_CLIENT_JOBS		2

#define JOB_TIMEOUT_MSEC	60000 // 1 min of inactivity
#define IP_TIMEOUT_MS		10000 // 10sec

#define IS_DEBUG(C)	((C) && (C)->debug)

struct tftp_client_context_t;

struct tftp_client_job_t {
	uint64_t started;
	uint64_t last_activity;
	bool requested;
	bool get;
	const struct tftp_context *hooks;
	struct tftp_file_t *file;
	ip_addr_t peer_addr;
	ip_resolve_state_t ip_state;
	struct tftp_client_context_t *ctx;
	void *user_context;
};

struct tftp_client_context_t {
	sys_module_t mod;
	uint32_t debug;
	struct tftp_client_job_t jobs[MAX_CLIENT_JOBS];
};

static struct tftp_client_context_t *__tftp_client_context;

static struct tftp_client_context_t *tftp_client_context_get(void)
{
	return __tftp_client_context;
}

#define TIME_STR	64
static bool sys_tftp_client_log_status(void *context)
{
	struct tftp_client_context_t *ctx = (struct tftp_client_context_t *)context;
	char time_buff[TIME_STR];
	int i, cnt = 0;
	struct tm date;
	uint64_t now;

	for (i = 0; i < MAX_CLIENT_JOBS; i++) {
		if (ctx->jobs[i].started)
			cnt++;
	}
	hlog_info(TFTP_CLIENT_MODULE, "TFTP client, %d jobs in progress", cnt);
	if (!cnt)
		return true;
	now = time_ms_since_boot();
	for (i = 0; i < MAX_CLIENT_JOBS; i++) {
		if (!ctx->jobs[i].started)
			continue;
		time_msec2datetime(&date, now - ctx->jobs[i].started);
		hlog_info(TFTP_CLIENT_MODULE, "\tCopy %s %s %s:%d [%s]",
				  ctx->jobs[i].file->fname, ctx->jobs[i].get ? "from" : "to",
				  ctx->jobs[i].file->peer, ctx->jobs[i].file->port,
				  time_date2str(time_buff, TIME_STR, &date));
	}

	return true;
}

static void sys_tftp_client_debug_set(uint32_t lvl, void *context)
{
	struct tftp_client_context_t *ctx = (struct tftp_client_context_t *)context;

	ctx->debug = lvl;
}

#define TIMEOUT_STR	"Timeout processing the request"
static void sys_tftp_client_job_cancel(struct tftp_client_job_t *job)
{
	if (job->hooks)
		job->hooks->error(job->user_context, ERR_TIMEOUT, TIMEOUT_STR, strlen(TIMEOUT_STR));
	memset(job, 0, sizeof(struct tftp_client_job_t));
}

static void tftp_peer_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
	struct tftp_client_job_t *job = (struct tftp_client_job_t *)arg;

	UNUSED(hostname);
	memcpy(&(job->peer_addr), ipaddr, sizeof(ip_addr_t));
	job->ip_state = IP_RESOLVED;
}

static void sys_tftp_client_job_run(struct tftp_client_job_t *job)
{
	uint64_t now;
	err_t res;

	if (!job->started)
		return;

	now = time_ms_since_boot();

	if (job->ip_state == IP_NOT_RESOLEVED) {
		if (IS_DEBUG(job->ctx))
			hlog_info(TFTP_CLIENT_MODULE, "Resolving %s. ", job->file->peer);

		res = dns_gethostbyname(job->file->peer, &job->peer_addr, tftp_peer_found, job);
		switch (res) {
		case ERR_OK:
			job->ip_state = IP_RESOLVED;
			break;
		case ERR_INPROGRESS:
			job->ip_state = IP_RESOLVING;
			break;
		default:
			hlog_warning(TFTP_CLIENT_MODULE, "Error resolving %s: %d", job->file->peer, res);
			goto out_err;
		}
	}

	if (job->ip_state == IP_RESOLVING) {
		if ((now - job->started) > IP_TIMEOUT_MS) {
			hlog_warning(TFTP_CLIENT_MODULE, "Timeout resolving %s.", job->file->peer);
			goto out_err;
		}
		return;
	}

	if (job->ip_state == IP_RESOLVED && !job->requested) {
		LWIP_LOCK_START;
			if (job->get)
				res = tftp_get(job, &job->peer_addr, job->file->port, job->file->fname, TFTP_MODE_OCTET);
			else
				res = tftp_put(job, &job->peer_addr, job->file->port, job->file->fname, TFTP_MODE_OCTET);
		LWIP_LOCK_END;
		if (res != ERR_OK) {
			hlog_warning(TFTP_CLIENT_MODULE, "Failed to %s file %s %s server %s:%d: %d [%s].",
					  job->get ? "get" : "put", job->file->fname, job->get ? "from" : "to",
					  job->file->peer, job->file->port, res, lwip_strerr(res));
			goto out_err;
		}
		if (IS_DEBUG(job->ctx))
			hlog_info(TFTP_CLIENT_MODULE, "%s file %s %s server %s:%d.",
					  job->get ? "Geting" : "Puting", job->file->fname, job->get ? "from" : "to",
					  job->file->peer, job->file->port);

		job->requested = true;
		job->last_activity = now;

		return;
	}

	if ((now - job->last_activity) > JOB_TIMEOUT_MSEC) {
		if (IS_DEBUG(job->ctx))
			hlog_warning(TFTP_CLIENT_MODULE, "Timeout %s file %s %s server %s:%d.",
					     job->get ? "geting" : "puting", job->file->fname, job->get ? "from" : "to",
						 job->file->peer, job->file->port);
		goto out_err;
	}

	return;
out_err:
	sys_tftp_client_job_cancel(job);
}

static void sys_tftp_client_run(void *context)
{
	struct tftp_client_context_t *ctx = (struct tftp_client_context_t *)context;
	int i;

	for (i = 0; i < MAX_CLIENT_JOBS; i++) {
		if (!ctx->jobs[i].started)
			continue;
		sys_tftp_client_job_run(&ctx->jobs[i]);
	}
}

static void *tftp_client_open(const char *fname, const char *mode, u8_t is_write)
{
	struct tftp_client_context_t *ctx = tftp_client_context_get();
	int i;

	if (!ctx)
		return NULL;

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_CLIENT_MODULE, "Open %s. ", fname);

	for (i = 0; i < MAX_CLIENT_JOBS; i++) {
		if (!ctx->jobs[i].started)
			continue;
		if (ctx->jobs[i].hooks->open(fname, mode, is_write)) {
			ctx->jobs[i].last_activity = time_ms_since_boot();
			return &ctx->jobs[i];
		}
	}

	return NULL;
}

static void tftp_client_close(void *handle)
{
	struct tftp_client_job_t *job = (struct tftp_client_job_t *)handle;

	if (!job || !job->started)
		return;

	if (IS_DEBUG(job->ctx))
		hlog_info(TFTP_CLIENT_MODULE, "Close %s. ", job->file->fname);

	job->hooks->close(job->user_context);
	memset(job, 0, sizeof(struct tftp_client_job_t));
}

static int tftp_client_read(void *handle, void *buf, int bytes)
{
	struct tftp_client_job_t *job = (struct tftp_client_job_t *)handle;

	if (!job || !job->started)
		return -1;

	if (IS_DEBUG(job->ctx))
		hlog_info(TFTP_CLIENT_MODULE, "Read %s. %d bytes", job->file->fname, bytes);

	job->last_activity = time_ms_since_boot();
	return job->hooks->read(job->user_context, buf, bytes);
}

static int tftp_client_write(void *handle, struct pbuf *p)
{
	struct tftp_client_job_t *job = (struct tftp_client_job_t *)handle;

	if (!job || !job->started)
		return -1;

	if (IS_DEBUG(job->ctx))
		hlog_info(TFTP_CLIENT_MODULE, "Write %d bytes in %s", p->len, job->file->fname);

	job->last_activity = time_ms_since_boot();
	return job->hooks->write(job->user_context, p);
}

#define MAX_MSG	100
static void tftp_client_error(void *handle, int err, const char *msg, int size)
{
	struct tftp_client_job_t *job = (struct tftp_client_job_t *)handle;

	if (!job || !job->started)
		return;

	if (IS_DEBUG(job->ctx)) {
		char message[MAX_MSG] = {0};

		if (msg && size > 0)
			memcpy(message, msg, size < (MAX_MSG - 1) ? size : MAX_MSG - 1);

		hlog_warning(TFTP_CLIENT_MODULE, "Error processing job [%s]: %d [%s]",
				job->file->fname, err, msg);
	}

	job->hooks->error(job->user_context, err, msg, size);
	memset(job, 0, sizeof(struct tftp_client_job_t));
}

static int tftp_clinet_new_job(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context, bool get)
{
	struct tftp_client_context_t *ctx = tftp_client_context_get();
	int i;

	if (!ctx)
		return -1;

	if (IS_DEBUG(ctx))
		hlog_info(TFTP_CLIENT_MODULE, "New %s job for %s:%d/%s",
				  get ? "get" : "put", file->peer, file->port, file->fname);

	for (i = 0; i < MAX_CLIENT_JOBS; i++) {
		if (!ctx->jobs[i].started)
			break;
	}
	if (i >= MAX_CLIENT_JOBS)
		return -1;

	memset(&ctx->jobs[i], 0, sizeof(struct tftp_client_job_t));
	ctx->jobs[i].ctx = ctx;
	ctx->jobs[i].file = file;
	ctx->jobs[i].hooks = hooks;
	ctx->jobs[i].user_context = user_context;
	ctx->jobs[i].get = get;
	ctx->jobs[i].started = time_ms_since_boot();

	return 0;
}

static bool sys_tftp_client_init(struct tftp_client_context_t **ctx)
{
	err_t err;

	static const struct tftp_context tftp_hooks = {
		tftp_client_open,
		tftp_client_close,
		tftp_client_read,
		tftp_client_write,
		tftp_client_error
	};

	(*ctx) = (struct tftp_client_context_t *)calloc(1, sizeof(struct tftp_client_context_t));
	if (!(*ctx))
		return false;
	LWIP_LOCK_START;
		err = tftp_init_client(&tftp_hooks);
	LWIP_LOCK_END;
	if (err != ERR_OK)
		goto out_err;
	__tftp_client_context = *ctx;

	return true;
out_err:
	free(*ctx);
	(*ctx) = NULL;
	return false;
}

void sys_tftp_client_register(void)
{
	struct tftp_client_context_t *ctx = NULL;

	if (!sys_tftp_client_init(&ctx))
		return;

	ctx->mod.name = TFTP_CLIENT_MODULE;
	ctx->mod.run = sys_tftp_client_run;
	ctx->mod.log = sys_tftp_client_log_status;
	ctx->mod.debug = sys_tftp_client_debug_set;
	ctx->mod.commands.description = "TFTP Client";
	ctx->mod.context = ctx;
	sys_module_register(&ctx->mod);
}

/* APIs */

// tftp://<IP-ADDR>[:<PORT-NUM>]/<FILENAME>
// tftp://1.1.1.1/FILENAME
// tftp://zico.biz/FILENAME
// tftp://1.1.1.1:5050/FILENAME
// tftp://zico.biz:5010/FILENAME
int tftp_url_parse(char *url, struct tftp_file_t *file)
{
	char *addr = NULL;
	char *fname = NULL;
	char *port = NULL;

	if (!file || !url || strlen(url) <= strlen(TFTP_URL))
		return -1;
	if (strncmp(url, TFTP_URL, strlen(TFTP_URL)))
		return -1;

	addr = strtok_r(url + strlen(TFTP_URL), "/", &fname);
	if (!addr)
		return -1;
	addr = strtok_r(addr, ":", &port);

	free(file->fname);
	free(file->peer);
	memset(file, 0, sizeof(struct tftp_file_t));
	if (fname)
		file->fname = strdup(fname);
	file->peer = strdup(addr);
	if (!file->fname || !file->peer)
		goto out_err;
	if (port)
		file->port = (int)strtol(port, NULL, 0);
	else
		file->port = LWIP_IANA_PORT_TFTP;

	return 0;

out_err:
	free(file->fname);
	free(file->peer);
	return -1;
}

int tftp_file_get(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context)
{
	return tftp_clinet_new_job(hooks, file, user_context, true);
}

int tftp_file_put(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context)
{
	return tftp_clinet_new_job(hooks, file, user_context, false);
}


