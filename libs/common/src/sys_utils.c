// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

#include "pico/cyw43_arch.h"
#include "herak_sys.h"
#include "common_lib.h"
#include "common_internal.h"
#include "pico/stdlib.h"
#include "base64.h"

extern char __StackLimit, __bss_end__;

int sys_asprintf(char **strp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	int size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (size < 0)
		return -1;

	*strp = calloc(1, size + 1);
	if (!*strp)
		return -1;

	va_start(args, fmt);
	vsnprintf(*strp, size + 1, fmt, args);
	va_end(args);
	return size;
}
#define PRINT_BUF_LEN	32
static void dump_raw_data(char *topic, char *format, const uint8_t *data, int len)
{
	char print_buff[PRINT_BUF_LEN], buf[4];
	int i = 0, j = 0;

	print_buff[0] = 0;
	while (i < len) {
		snprintf(buf, 4, format, data[i++]);
		if ((j + strlen(buf)) >= PRINT_BUF_LEN) {
			j = 0;
			hlog_info(topic, "\t %s", print_buff);
			print_buff[0] = 0;
		}
		strcat(print_buff, buf);
		j += strlen(buf);
	}
	if (j)
		hlog_info(topic, "\t %s", print_buff);
}

void dump_hex_data(char *topic, const uint8_t *data, int len)
{
	dump_raw_data(topic, "%0.2X ", data, len);
}

void dump_char_data(char *topic, const uint8_t *data, int len)
{
	dump_raw_data(topic, "%c", data, len);
}

uint8_t sys_value_to_percent(uint32_t range_min, uint32_t range_max, uint32_t val)
{
	if (val <= range_min)
		return 0;
	if (val >= range_max)
		return 100;

	return	(100 * (val - range_min)) / (range_max - range_min);
}

uint32_t samples_filter(uint32_t *samples, int total_count, int filter_count)
{
	uint32_t all;
	uint16_t sw;
	int i, j;

	/* bubble sort */
	for (i = 0 ; i < total_count - 1; i++) {
		for (j = 0 ; j < total_count - i - 1; j++) {
			if (samples[j] > samples[j+1]) {
				sw  = samples[j];
				samples[j] = samples[j+1];
				samples[j+1] = sw;
			}
		}
	}
	/* filter biggest and smallest */
	all = 0;
	for (i = filter_count ; i < total_count - filter_count; i++)
		all += samples[i];
	all /= total_count-(2*filter_count);

	return all;
}

uint32_t get_total_heap(void)
{
	static uint32_t mem_total;

	if (!mem_total)
		mem_total = &__StackLimit  - &__bss_end__;

	return mem_total;
}

uint32_t get_free_heap(void)
{
	struct mallinfo m = mallinfo();

//	return m.fordblks;
	return get_total_heap() - m.uordblks;

}

#define SYS_LOG	"system"
void log_sys_health(void)
{
	int errs = 0;

#if MEM_STATS
	if (lwip_stats.mem.err) {
		errs++;
		hlog_err(SYS_LOG, "Error %s: %d / %d available",
				 lwip_stats.mem.name, lwip_stats.mem.err, lwip_stats.mem.avail);
	}
	for (int i = 0; i < MEMP_MAX; i++) {
		if (lwip_stats.memp[i]->err) {
			hlog_err(SYS_LOG, "Error MEM %s: %d / %d available",
					 lwip_stats.memp[i]->name, lwip_stats.memp[i]->err, lwip_stats.memp[i]->avail);
		}
	}
#endif

#if SYS_STATS
	if (lwip_stats.sys.mbox.err) {
		errs++;
		hlog_err(SYS_LOG, "Error SYS MBOX: %d / %d available",
				 lwip_stats.sys.mbox.err, lwip_stats.sys.mbox.max);
	}
	if (lwip_stats.sys.mutex.err) {
		errs++;
		hlog_err(SYS_LOG, "Error SYS Mutex: %d / %d available",
				 lwip_stats.sys.mutex.err, lwip_stats.sys.mutex.err);
	}
	if (lwip_stats.sys.sem.err) {
		errs++;
		hlog_err(SYS_LOG, "Error SYS Sem: %d / %d available",
				 lwip_stats.sys.sem.err, lwip_stats.sys.sem.max);
	}
#endif

#if TCP_STATS
	if (lwip_stats.tcp.err) {
		errs++;
		hlog_err(SYS_LOG, "Error TCP: %d", lwip_stats.tcp.err);
	}
#endif

#if UDP_STATS
	if (lwip_stats.udp.err) {
		errs++;
		hlog_err(SYS_LOG, "Error UDP: %d", lwip_stats.udp.err);
	}
#endif

#if ICMP_STATS
	if (lwip_stats.icmp.err) {
		errs++;
		hlog_err(SYS_LOG, "Error ICMP: %d", lwip_stats.icmp.err);
	}
#endif

#if IP_STATS
	if (lwip_stats.ip.err) {
		errs++;
		hlog_err(SYS_LOG, "Error IP: %d", lwip_stats.ip.err);
	}
#endif

#if IPFRAG_STATS
	if (lwip_stats.ip_frag.err) {
		errs++;
		hlog_err(SYS_LOG, "Error IPfrag: %d", lwip_stats.ip_frag.err);
	}
#endif

#if ETHARP_STATS
	if (lwip_stats.etharp.err) {
		errs++;
		hlog_err(SYS_LOG, "Error EthArp: %d", lwip_stats.etharp.err);
	}
#endif

#if LINK_STATS
	if (lwip_stats.link.err) {
		errs++;
		hlog_err(SYS_LOG, "Error Link: %d", lwip_stats.link.err);
	}
#endif

	if (!errs)
		hlog_info(SYS_LOG, "System is healthy, no errors detected");

}

char *sys_user_param_get(char *name, const char *def, int def_len)
{
	char *val = NULL;

#ifdef HAVE_SYS_CFG_STORE
	val = cfgs_param_get(name);
#endif /* HAVE_SYS_CFG_STORE */

	if (val)
		return val;
	if (def_len < 1)
		return NULL;

	return base64_decode(def, def_len);
}

