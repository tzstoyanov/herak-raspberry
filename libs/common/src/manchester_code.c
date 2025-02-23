// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"

// Manchester encodes a 32 bit frame into a 64 bit integer.
uint64_t manchester_encode(uint32_t frame, bool invert)
{
		uint64_t mframe = 0;
		uint32_t mask = 0x80000000;
		uint8_t one = invert ? 1 : 2;
		uint8_t zero = invert ? 2 : 1;

	while (mask) {
		if (frame & mask) {
			mframe <<= 2;
			mframe |= one;
		} else {
			mframe <<= 2;
			mframe |= zero;
		}
		mask >>= 1;
	}

	return mframe;
}

// Manchester decodes a 64 bit integer into a 32 bit frame.
int manchester_decode(uint64_t mframe, bool invert, uint32_t *value)
{
	uint32_t frame = 0;
	uint64_t mask = 0x8000000000000000ULL;
	uint64_t mask2 = 0x4000000000000000ULL;
	uint8_t one = invert ? 0 : 1;
	uint8_t zero = invert ? 1 : 0;

	for (int i = 0; i < 32; i++) {
		if (mframe & mask) {
			if (mframe & mask2)
				return -1;
			frame <<= 1;
			frame |= one;
		} else {
			if (!(mframe & mask2))
				return -1;
			frame <<= 1;
			frame |= zero;
		}
		mask >>= 2;
		mask2 >>= 2;
	}
	if (value)
		*value = frame;
	return 0;
}
