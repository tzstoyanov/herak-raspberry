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

#include "herak_sys.h"
#include "base64.h"
#include "common_internal.h"
#include "params.h"

static const char * const __in_flash() mnames[] = {
	"Ukn", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

uint64_t time_ms_since_boot(void)
{
	return (to_us_since_boot(get_absolute_time()) / 1000u);
}

uint64_t time_msec2datetime(datetime_t *date, uint64_t msec)
{
	uint32_t sec = 0, min = 0, hour = 0, day = 0, year = 0;

	memset(date, 0, sizeof(datetime_t));
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

	date->sec = sec;
	date->min = min;
	date->hour = hour;
	date->day = day;
	date->year = year;

	return msec;
}

char *time_date2str(char *buf, int str_len, datetime_t *date)
{
	if (date->year)
		snprintf(buf, str_len, "%d years, %d days, %.2d:%.2d:%.2d hours",
				date->year, date->day, date->hour, date->min, date->sec);
	else if (date->day)
		snprintf(buf, str_len, "%d days, %.2d:%.2d:%.2d hours",
				date->day, date->hour, date->min, date->sec);
	else if (date->hour)
		snprintf(buf, str_len, "%.2d:%.2d:%.2d hours",
				date->hour, date->min, date->sec);
	else if (date->min)
		snprintf(buf, str_len, "%.2d:%.2d minutes",
				date->min, date->sec);
	else if (date->sec)
		snprintf(buf, str_len, "%.2d sec",
				date->sec);
	else
		snprintf(buf, str_len, "0");

	return buf;
}

#define UPTIME_STR_LEN	64
char *get_uptime(void)
{
	static char buf[UPTIME_STR_LEN];
	datetime_t date;

	time_msec2datetime(&date, time_ms_since_boot());
	return time_date2str(buf, UPTIME_STR_LEN, &date);
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

	SYS_LOCK_START;
		ret = rtc_get_datetime(date);
	SYS_LOCK_END;
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
#ifdef HAVE_SYS_NTP
	datetime_t date;

	if (ntp_connected() && tz_datetime_get(&date)) {
		const char *month;

		if (date.month > 0 && date.month <= 12)
			month = mnames[date.month];
		else
			month = mnames[0];
		snprintf(buf, buflen, "%.2d %s %d %.2d:%.2d:%.2d",
				 date.day, month, date.year, date.hour, date.min, date.sec);
	} else {
		snprintf(buf, buflen, "%s 0 %lld", mnames[0], time_ms_since_boot());
	}
#else /* !HAVE_SYS_NTP */
	snprintf(buf, buflen, "%s 0 %lld", mnames[0], time_ms_since_boot());
#endif /* HAVE_SYS_NTP */

	return buf;
}
