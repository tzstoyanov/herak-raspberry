// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include "lwip/inet.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "pico/time.h"
#include "pico/mutex.h"
#include "pico/aon_timer.h"

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

uint64_t time_ms_since_boot(void)
{
	return (to_us_since_boot(get_absolute_time()) / 1000u);
}

uint64_t time_msec2datetime(struct tm *date, uint64_t msec)
{
	uint32_t sec = 0, min = 0, hour = 0, day = 0, year = 0;

	memset(date, 0, sizeof(struct tm));
	if (msec >= 1000) {
		sec = msec / 1000;
		msec = msec % 1000;
	}
	if (sec >= 60) {
		min = sec / 60;
		sec = sec % 60;
	}
	if (min >= 60) {
		hour = min / 60;
		min = min % 60;
	}
	if (hour >= 24) {
		day = hour / 24;
		hour = hour % 24;
	}
	if (day >= 365) {
		year = day / 365;
		day = day % 365;
	}

	date->tm_sec = sec;
	date->tm_min = min;
	date->tm_hour = hour;
	date->tm_mday = day;
	date->tm_year = year - 1900;

	return msec;
}

char *time_date2str(char *buf, int str_len, struct tm *date)
{
	if (date->tm_year > 0)
		snprintf(buf, str_len, "%d years, %d days, %.2d:%.2d:%.2d hours",
				date->tm_year + 1900, date->tm_yday, date->tm_hour, date->tm_min, date->tm_sec);
	else if (date->tm_yday)
		snprintf(buf, str_len, "%d days, %.2d:%.2d:%.2d hours",
				date->tm_yday, date->tm_hour, date->tm_min, date->tm_sec);
	else if (date->tm_hour)
		snprintf(buf, str_len, "%.2d:%.2d:%.2d hours",
				date->tm_hour, date->tm_min, date->tm_sec);
	else if (date->tm_min)
		snprintf(buf, str_len, "%.2d:%.2d minutes",
				date->tm_min, date->tm_sec);
	else if (date->tm_sec)
		snprintf(buf, str_len, "%.2d sec",
				date->tm_sec);
	else
		snprintf(buf, str_len, "0");

	return buf;
}

#define UPTIME_STR_LEN	64
char *get_uptime(void)
{
	static char buf[UPTIME_STR_LEN];
	struct tm date;

	time_msec2datetime(&date, time_ms_since_boot());
	return time_date2str(buf, UPTIME_STR_LEN, &date);
}

static int get_utc_eest_offset(struct tm *dt)
{
	const int moffset[] = {2, 2, 0, 3, 3, 3, 3, 3, 3, 0, 2, 2};
	const int dwoffset[] = {0, 6, 5, 4, 3, 2, 1};

	if (dt->tm_mon < 0 || dt->tm_mon > 11)
		return 0;
	if (moffset[dt->tm_mon])
		return moffset[dt->tm_mon];
	// Not the last week
	if (dt->tm_mday <= 24) {
		if (dt->tm_mday == 3)
			return 2;
		else
			return 3;
	}
	if (dt->tm_wday < 0 || dt->tm_wday > 6)
		return 0;
	// Not Sunday
	if (dt->tm_wday != 0) {
		if (dt->tm_mday + dwoffset[dt->tm_wday] > 31) {
			// The day is after the last Sunday
			if (dt->tm_mon == 2)
				return 3;
			else
				return 2;
		} else {
			// The day is before the last Sunday
			if (dt->tm_mon == 2)
				return 2;
			else
				return 3;
		}

	}
	// The last Sunday of the month
	if (dt->tm_hour < 3) {
		if (dt->tm_mon == 2)
			return 2;
		else
			return 3;
	}

	return 0;
}

time_t time2epoch(struct tm *time, time_t *epoch)
{
	time_t e = mktime(time);

	if (epoch)
		(*epoch) = e;
	return e;
}

void epoch2time(time_t *epoch, struct tm *time)
{
	gmtime_r(epoch, time);
}

void time_to_str(char *buf, uint buf_size, const struct tm *t)
{
	strftime(buf, buf_size, "%a %e %b %H:%M:%S %Y", t);
}

bool tz_datetime_get(struct tm *date)
{
	const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int offset;
	bool ret;

	SYS_LOCK_START;
		ret = aon_timer_get_time_calendar(date);
	SYS_LOCK_END;
	if (!ret)
		return false;

	offset = get_utc_eest_offset(date);
	date->tm_hour += offset;
	if (date->tm_hour >= 24) {
		date->tm_mday++;
		date->tm_hour -= 24;
		if (date->tm_mon >= 0 && date->tm_mon <= 11 && date->tm_mday > mdays[date->tm_mon]) {
			date->tm_mon++;
			date->tm_mday = 1;
			if (date->tm_mon > 11)
				date->tm_mon = 0;
		}
	}

	return true;
}

char *get_current_time_str(char *buf, int buflen)
{
	struct tm date = {0};

	tz_datetime_get(&date);
	time_to_str(buf, buflen, &date);
	return buf;
}

char *get_current_time_log_str(char *buf, int buflen)
{
	struct tm date = {0};

	tz_datetime_get(&date);
    snprintf(buf, buflen, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			 date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
			 date.tm_hour, date.tm_min, date.tm_sec);
	return buf;
}
