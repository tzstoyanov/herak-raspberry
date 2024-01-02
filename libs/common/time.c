// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "hardware/rtc.h"
#include "lwip/inet.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/util/datetime.h"
#include "pico/mutex.h"

#include "base64.h"
#include "common_internal.h"
#include "params.h"

#define NTPLOG	"ntp"
#define CONNECT_TIMEOUT_MS 5000

struct {
	char *ntp_servers[SNTP_MAX_SERVERS];
	bool init;
	bool in_progress;
	absolute_time_t connect_time;
	absolute_time_t reolve_time;
	ip_addr_t server_addr;
	datetime_t datetime;
	bool time_synched;
	mutex_t lock;
}static ntp_context;

bool ntp_connected()
{
	return ntp_context.init;
}

static const char *mnames[] = {
	"Ukn", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void get_ntp_servers()
{
	char* rest;
	char *tok;
	int idx;

	idx = 0;
	rest = param_get(NTP_SERVERS);
	while ((tok = strtok_r(rest, ";", &rest)) && idx < SNTP_MAX_SERVERS)
		ntp_context.ntp_servers[idx++] = tok;
}

bool ntp_init()
{
	int i = 0;

	memset(&ntp_context, 0, sizeof(ntp_context));
	mutex_init(&ntp_context.lock);
	get_ntp_servers();
	while(i < SNTP_MAX_SERVERS && ntp_context.ntp_servers[i])
		i++;
	if (!i)
		return false;
	hlog_info(NTPLOG,"Got %d NTP servers", i, SNTP_MAX_SERVERS);
	rtc_init();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_servermode_dhcp(1);
	i = 0;
	while(ntp_context.ntp_servers[i] && i < SNTP_MAX_SERVERS) {
		sntp_setservername(i, ntp_context.ntp_servers[i]);
		hlog_info(NTPLOG,"  [%s]", ntp_context.ntp_servers[i]);
		i++;
	}
	return true;
}

void ntp_connect()
{
	static char buff[64];

	if (ntp_context.init) {
		mutex_enter_blocking(&ntp_context.lock);
			if (ntp_context.time_synched) {
				ntp_context.time_synched = false;
				datetime_to_str(buff, 64, &ntp_context.datetime);
				hlog_info(NTPLOG,"Time synched to [%s] UTC", buff);
				system_log_status();
			}
		mutex_exit(&ntp_context.lock);
		return;
	}
	if (!wifi_is_connected())
		return;
	LWIP_LOCK_START
		sntp_init();
	LWIP_LOCK_END
	ntp_context.init = true;
}

#ifdef __cplusplus
extern "C" {
#endif

#define UPTIME_STR_LEN	64
char *get_uptime()
{
	static char buf[UPTIME_STR_LEN];
	uint32_t msec = 0;
	uint32_t sec = 0;
	uint32_t min = 0;
	uint32_t hours = 0;
	uint32_t days = 0;
	uint32_t years = 0;

	msec = to_ms_since_boot(get_absolute_time());
	if (msec >= 1000) {
		sec = msec / 1000;
		msec = msec % 1000;
	}
	if (sec >= 60) {
		min = sec / 60;
		sec = sec % 60;
	}
	if (min >= 60) {
		hours = min / 60;
		min = min % 60;
	}
	if (hours >= 24) {
		days = hours / 24;
		hours = hours % 24;
	}
	if (days >= 365) {
		years = days / 365;
		days = days % 365;
	}

	if (years)
		snprintf(buf, UPTIME_STR_LEN, "%d years, %d days, %.2d:%.2d:%.2d.%.3d hours",
				years, days, hours, min, sec, msec);
	else if (days)
		snprintf(buf, UPTIME_STR_LEN, "%d days, %.2d:%.2d:%.2d.%.3d hours",
				days, hours, min, sec, msec);
	else if (hours)
		snprintf(buf, UPTIME_STR_LEN, "%.2d:%.2d:%.2d.%.3d hours",
				hours, min, sec, msec);
	else if (min)
		snprintf(buf, UPTIME_STR_LEN, "%.2d:%.2d.%.3d minutes",
				min, sec, msec);
	else if (sec)
		snprintf(buf, UPTIME_STR_LEN, "%.2d.%.3d sec",
				sec, msec);
	else snprintf(buf, UPTIME_STR_LEN, "%.3d msec", msec);

	return buf;
}

static int get_utc_eest_offset(datetime_t *dt)
{
	const int moffset[] = {0, 2, 2, 0, 3, 3, 3, 3, 3, 3, 0, 2, 2};
	const int dwoffset[] = {0, 6, 5, 4, 3, 2, 1};

	if (dt->month < 1 || dt->month > 12)
		return 0;
	if (moffset[dt->month])
		return moffset[dt->month];
	// Not the last week
	if (dt->day <= 24) {
		if (dt->month == 3)
			return 2;
		else
			return 3;
	}
	if (dt->dotw < 0 || dt->dotw > 6)
		return 0;
	// Not Sunday
	if (dt->dotw != 0) {
		if (dt->day + dwoffset[dt->dotw] > 31) {
			// The day is after the last Sunday
			if (dt->month == 3)
				return 3;
			else
				return 2;
		} else {
			// The day is before the last Sunday
			if (dt->month == 3)
				return 2;
			else
				return 3;
		}

	}
	// The last Sunday of the month
	if (dt->hour < 3) {
		if (dt->month == 3)
			return 2;
		else
			return 3;
	}

	return 0;
}

bool tz_datetime_get(datetime_t *date)
{
	const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int offset;
	bool ret;

	SYS_LOCK_START
		ret = rtc_get_datetime(date);
	SYS_LOCK_END
	if (!ret)
		return false;

	offset = get_utc_eest_offset(date);
	date->hour += offset;
	if (date->hour >= 24) {
		date->day++;
		date->hour -= 24;
		if (date->month > 0 && date->month <= 12 && date->day > mdays[date->month]) {
			date->month++;
			date->day = 1;
			if (date->month > 12)
				date->month = 1;
		}
	}

	return true;
}

char *get_current_time_str(char *buf, int buflen)
{
	datetime_t date;

	if (ntp_connected() && tz_datetime_get(&date)) {
		const char *month;

		if (date.month > 0 && date.month <= 12)
			month = mnames[date.month];
		else
			month = mnames[0];
		snprintf(buf, buflen, "%s %.2d %.2d.%.2d.%.2d",
					month, date.day, date.hour, date.min, date.sec);
	} else {
		snprintf(buf, buflen, "%s 0 %d", mnames[0], to_ms_since_boot(get_absolute_time()));
	}

	return buf;
}

void herak_set_system_time(uint32_t sec)
{
	time_t epoch = sec;
    struct tm time;

    gmtime_r(&epoch, &time);
    ntp_context.datetime.year = (int16_t) (1900 + time.tm_year);
    ntp_context.datetime.month = (int8_t) (time.tm_mon + 1);
    ntp_context.datetime.day = (int8_t) time.tm_mday;
    ntp_context.datetime.dotw = (int8_t) time.tm_wday;
    ntp_context.datetime.hour = (int8_t) time.tm_hour;
    ntp_context.datetime.min = (int8_t) time.tm_min;
    ntp_context.datetime.sec = (int8_t) time.tm_sec;

	/* Set time in UTC */
	SYS_LOCK_START
		rtc_set_datetime(&(ntp_context.datetime));
	SYS_LOCK_END

	mutex_enter_blocking(&ntp_context.lock);
		ntp_context.time_synched = true;
	mutex_exit(&ntp_context.lock);
}

#ifdef __cplusplus
}
#endif
