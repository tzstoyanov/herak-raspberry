// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _LIB_SYS_LOG_API_H_
#define _LIB_SYS_LOG_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Log */
void hlog_any(int severity, const char *topic, const char *fmt, ...);
enum {
	HLOG_EMERG	= 0,
	HLOG_ALERT,
	HLOG_CRIT,
	HLOG_ERR,
	HLOG_WARN,
	HLOG_NOTICE,
	HLOG_INFO,
	HLOG_DEBUG
};
bool hlog_remoute(void);
#define hlog_info(topic, args...) hlog_any(HLOG_INFO, topic, args)
#define hlog_warning(topic, args...) hlog_any(HLOG_WARN, topic, args)
#define hlog_err(topic, args...) hlog_any(HLOG_ERR, topic, args)
#define hlog_dbg(topic, args...) hlog_any(HLOG_DEBUG, topic, args)
#define hlog_null(topic, args...)

void hlog_init(int level);
void hlog_connect(void);
void hlog_reconnect(void);
void hlog_web_enable(bool set);
void log_level_set(uint32_t level);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_SYS_LOG_API_H_ */

