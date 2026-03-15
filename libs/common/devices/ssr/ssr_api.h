// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _SSR_API_H_
#define _SSR_API_H_

int ssr_api_state_set(uint8_t id, bool state, uint32_t time, uint32_t delay);
int ssr_api_state_get(uint8_t id, bool *state, uint32_t *time_remain_ms, uint32_t *delay_remain_ms);

#endif /* _SSR_API_H_ */

