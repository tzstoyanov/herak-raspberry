// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "opentherm.pio.h"
#include "boiler.h"

#define OT_TIMEOUT_MS   200
#define MAX_RETRIES     2
#define MIN_INTERVAL_MS	150
#define DEAD_INTERVAL_MS  60000  // 1 min
#define MAX_SEARCH_HZ	1000000 // 1MHz

#define IS_PIO_LOG (boiler_dbg_check(LOG_PIO_DEBUG))

typedef struct {
	int pin;
	int sm;
	PIO p;
	uint offset;
	const pio_program_t *program;
	pio_sm_config cfg;
	gpio_function_t pio_func;
} pio_prog_t;

static struct {
	int rx_hz;
	bool attached;
	int conn_count;
	uint64_t last_valid;
	pio_prog_t pio_rx;
	pio_prog_t pio_tx;
} mpio_context;

// Encodes opentherm info into a 32 bit network ordered frame
static uint32_t opentherm_frame_encode(uint8_t msg_type, uint8_t data_id, uint16_t data_value)
{
	uint32_t frame = 0;

	frame |= (msg_type & 0x07) << 28;
	frame |= (data_id & 0xFF) << 16;
	frame |= (data_value & 0xFFFF);
	// Parity bit
	if (__builtin_popcount(frame) & 1)
		frame |= 0x80000000;

	return frame;
}

// Decodes a 32 bit frame into opentherm info.
static int opentherm_frame_decode(uint32_t frame, opentherm_msg_t *msg)
{
	uint32_t parity = __builtin_popcount(frame);

	if (parity & 1) {
		if (IS_PIO_LOG)
			hlog_warning(OTHLOG, "> Frame [0x%X] decode  error.\n", frame);
		return -1;
	}
	msg->msg_type = (frame >> 28) & 0x07;
	msg->id = (frame >> 16) & 0xFF;
	msg->value = frame & 0xFFFF;
	return 0;
}

static int opentherm_exchange_frame(uint64_t out, uint32_t *in)
{
	uint64_t tstart;

	// Setup PIO
	pio_sm_init(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, mpio_context.pio_tx.offset, &mpio_context.pio_tx.cfg);
	pio_sm_init(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, mpio_context.pio_rx.offset, &mpio_context.pio_rx.cfg);
	pio_sm_drain_tx_fifo(mpio_context.pio_tx.p, mpio_context.pio_tx.sm);
	while (pio_sm_get_rx_fifo_level(mpio_context.pio_tx.p, mpio_context.pio_tx.sm) > 0)
		pio_sm_get(mpio_context.pio_tx.p, mpio_context.pio_tx.sm);
	while (pio_sm_get_rx_fifo_level(mpio_context.pio_rx.p, mpio_context.pio_rx.sm) > 0)
		pio_sm_get(mpio_context.pio_rx.p, mpio_context.pio_rx.sm);

	// Send data using transmitter PIO
	pio_sm_put_blocking(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, (uint32_t)(out >> 32));
	pio_sm_put_blocking(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, (uint32_t)(out));
	pio_sm_set_enabled(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, true);

	// Wait for TX to finish
	tstart = time_ms_since_boot();
	while (pio_sm_get_tx_fifo_level(mpio_context.pio_tx.p, mpio_context.pio_tx.sm) > 0) {
		tight_loop_contents();
		if (time_ms_since_boot() - tstart > OT_TIMEOUT_MS) {
			// Time Out sending the request
			pio_sm_set_enabled(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, false);
			return -1;
		}
	}

	// Wait for response
	pio_sm_set_enabled(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, true);
	tstart = time_ms_since_boot();
	while (pio_sm_get_rx_fifo_level(mpio_context.pio_rx.p, mpio_context.pio_rx.sm) < 3 &&
		(time_ms_since_boot() - tstart) < OT_TIMEOUT_MS) {
		sleep_ms(1);
	}

	pio_sm_set_enabled(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, false);
	pio_sm_set_enabled(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, false);


	// Check timeout
	if (pio_sm_get_rx_fifo_level(mpio_context.pio_rx.p, mpio_context.pio_rx.sm) < 3)
		return -2;
	// Read response
	in[0] = pio_sm_get(mpio_context.pio_rx.p, mpio_context.pio_rx.sm);
	in[1] = pio_sm_get(mpio_context.pio_rx.p, mpio_context.pio_rx.sm);
	in[2] = pio_sm_get(mpio_context.pio_rx.p, mpio_context.pio_rx.sm);

	return 0;
}

#define END_BIT 0x80000000
// OpenTherm exchange function
static int opentherm_exchange_run(opentherm_msg_t *request, opentherm_msg_t *reply)
{
	uint32_t f = opentherm_frame_encode(request->msg_type, request->id, request->value);
	uint64_t m = manchester_encode(f, true);
	uint32_t in[3];
	int ret;

	ret = opentherm_exchange_frame(m, in);
	if (ret) {
		if (IS_PIO_LOG)
			hlog_warning(OTHLOG, "> PIO %s frame timeout.\n", ret == -1 ? "send":"receive");
		return -1;
	}

	// Decode response
	if (in[2] != END_BIT) {
		if (IS_PIO_LOG)
			hlog_warning(OTHLOG, "> PIO no valid EndBit received: 0x%X.\n", in[2]);
		return -1;
	}

	m = ((uint64_t)in[0] << 32) | in[1];
	f = 0;
	if (manchester_decode(m, false, &f)) {
		if (IS_PIO_LOG)
			hlog_warning(OTHLOG, "> PIO no valid frame received: manchester decode failed.");
		return -1;
	}

	return opentherm_frame_decode(f, reply);
}

int opentherm_pio_exchange(opentherm_msg_t *request, opentherm_msg_t *reply)
{
	int retry = MAX_RETRIES;

	if (!mpio_context.attached)
		return -1;

	do {
		sleep_ms(MIN_INTERVAL_MS);
		wd_update();
		if (!opentherm_exchange_run(request, reply)) {
			mpio_context.last_valid = time_ms_since_boot();
			return 0;
		}
		retry--;
	} while (retry > 0);

	if ((time_ms_since_boot() - mpio_context.last_valid) > DEAD_INTERVAL_MS) {
		if (IS_PIO_LOG)
			hlog_warning(OTHLOG, "PIO connection lost.");
		mpio_context.attached = false;
	}

	return -1;
}

int opentherm_pio_find(void)
{
	uint32_t f = opentherm_frame_encode(MSG_TYPE_READ_DATA, DATA_ID_STATUS, 0);
	uint64_t m = manchester_encode(f, true);
	int min = 0, max = 0;
	bool found = false;
	int step = 10000;
	uint32_t in[3];
	bool up = true;
	uint32_t hz = 1;
	int ret;

	if (IS_PIO_LOG)
		hlog_info(OTHLOG, "Looking for devices ... ");
	// Find frequency
	do {
		sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / hz);
		ret = opentherm_exchange_frame(m, in);
		if (!in[2] || ret) {
			if (!up) {
				up = true;
				step /= 10;
			}
			hz += step;
		} else if (in[2] != END_BIT) {
			if (up) {
				up = false;
				step /= 10;
			}
			hz -= step;
		} else if (manchester_decode(((uint64_t)in[0] << 32) | in[1], false, &f)) {
			if (up) {
				up = false;
				step /= 10;
			}
			hz -= step;
		} else {
			found = true;
			break;
		}
		if (!ret)
			sleep_ms(MIN_INTERVAL_MS);
		wd_update();
	} while (step && hz > 0 && hz < MAX_SEARCH_HZ);

	if (!found) {
		if (IS_PIO_LOG)
			hlog_info(OTHLOG, "No devices found");
		sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / mpio_context.rx_hz);
		return -1;
	}

	// Find min frequency
	do {
		min += 10;
		sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / (hz - min));
		ret = opentherm_exchange_frame(m, in);
		if (in[2] != END_BIT || ret)
			break;
		if (manchester_decode(((uint64_t)in[0] << 32) | in[1], false, &f))
			break;
		sleep_ms(MIN_INTERVAL_MS);
		wd_update();
	} while (1);
	min -= 10;

	// Find max frequency
	do {
		max += 10;
		sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / (hz + max));
		ret = opentherm_exchange_frame(m, in);
		if (in[2] != END_BIT || ret)
			break;
		if (manchester_decode(((uint64_t)in[0] << 32) | in[1], false, &f))
			break;
		sleep_ms(MIN_INTERVAL_MS);
		wd_update();
	} while (1);
	max -= 10;

	hz += ((max-min)/2);
	sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / hz);
	hlog_info(OTHLOG, "Device attached at %dhz", hz);
	mpio_context.rx_hz = hz;
	mpio_context.attached = true;
	mpio_context.conn_count++;
	mpio_context.last_valid = time_ms_since_boot();
	return 0;
}

static int load_pio_program(pio_prog_t *prog)
{
	PIO pall[] = {pio0, pio1};
	int pall_count = ARRAY_SIZE(pall);
	int i;

	prog->sm = -1;
	prog->offset = -1;
	for (i = 0; i < pall_count; i++) {
		if (pio_can_add_program(pall[i], prog->program)) {
			prog->offset = pio_add_program(pall[i], prog->program);
			prog->sm = pio_claim_unused_sm(pall[i], false);
			if (prog->sm < 0) {
				pio_remove_program(pall[i], prog->program, prog->offset);
				continue;
			}
		prog->p = pall[i];
		if (prog->p == pio0)
			prog->pio_func = GPIO_FUNC_PIO0;
		else
			prog->pio_func = GPIO_FUNC_PIO1;
		return 0;
		}
	}
	return -1;
}

static int opentherm_config_get(void)
{
	char *str = NULL;
	char *rest, *tok;
	int ret = -1;

	str = param_get(OPENTHERM_PINS);
	if (!str || strlen(str) < 1)
		goto out;
	rest = str;
	tok = strtok_r(rest, ";", &rest);
	mpio_context.pio_rx.pin = (int)strtol(tok, NULL, 10);
	if (mpio_context.pio_rx.pin < 0 || mpio_context.pio_rx.pin > 40)
		goto out;
	mpio_context.pio_tx.pin = (int)strtol(rest, NULL, 10);
	if (mpio_context.pio_tx.pin < 0 || mpio_context.pio_tx.pin > 40)
		goto out;
	ret = 0;

out:
	free(str);
	return ret;
}

// Load and configure opentherm tx and rx program
int opentherm_pio_init(opentherm_context_t *boiler)
{
	UNUSED(boiler);

	if (opentherm_config_get()) {
		hlog_warning(OTHLOG, "PIO no valid config.");
		return -1;
	}

	mpio_context.rx_hz = 96550;
	mpio_context.pio_rx.program = &opentherm_rx_program;
	mpio_context.pio_tx.program = &opentherm_tx_program;
	if (load_pio_program(&mpio_context.pio_rx)) {
		hlog_warning(OTHLOG, "PIO failed to load RX program.");
		return -1;
	}
	if (load_pio_program(&mpio_context.pio_tx)) {
		hlog_warning(OTHLOG, "PIO failed to load TX program.");
		return -1;
	}
	gpio_set_dir(mpio_context.pio_rx.pin, GPIO_IN);

	mpio_context.pio_tx.cfg = opentherm_tx_program_get_default_config(mpio_context.pio_tx.offset);
	gpio_init(mpio_context.pio_tx.pin);
	gpio_set_dir(mpio_context.pio_tx.pin, GPIO_OUT);
	gpio_set_function(mpio_context.pio_tx.pin, mpio_context.pio_tx.pio_func);
	sm_config_set_set_pins(&mpio_context.pio_tx.cfg, mpio_context.pio_tx.pin, 1);
	sm_config_set_out_pins(&mpio_context.pio_tx.cfg, mpio_context.pio_tx.pin, 1);
	sm_config_set_out_shift(&mpio_context.pio_tx.cfg, false, true, 32);
	sm_config_set_in_shift(&mpio_context.pio_tx.cfg, false, true, 32);
	sm_config_set_clkdiv(&mpio_context.pio_tx.cfg, (float)clock_get_hz(clk_sys) / 4000); // 4kHz clock, 250us
	pio_sm_init(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, mpio_context.pio_tx.offset, &mpio_context.pio_tx.cfg);
	pio_sm_set_pins(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, 1);
	pio_sm_set_consecutive_pindirs(mpio_context.pio_tx.p, mpio_context.pio_tx.sm, mpio_context.pio_tx.pin, 1, true);

	mpio_context.pio_rx.cfg = opentherm_rx_program_get_default_config(mpio_context.pio_rx.offset);
	gpio_init(mpio_context.pio_rx.pin);
	gpio_set_dir(mpio_context.pio_rx.pin, GPIO_IN);
	gpio_set_function(mpio_context.pio_rx.pin, mpio_context.pio_rx.pio_func);
	sm_config_set_set_pins(&mpio_context.pio_rx.cfg, mpio_context.pio_rx.pin, 1);
	sm_config_set_in_pins(&mpio_context.pio_rx.cfg, mpio_context.pio_rx.pin);
	sm_config_set_in_shift(&mpio_context.pio_rx.cfg, false, true, 32);
	sm_config_set_clkdiv(&mpio_context.pio_rx.cfg, (float)clock_get_hz(clk_sys) / mpio_context.rx_hz);
	pio_sm_init(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, mpio_context.pio_rx.offset, &mpio_context.pio_rx.cfg);
	pio_sm_set_pins(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, 0);
	pio_sm_set_consecutive_pindirs(mpio_context.pio_rx.p, mpio_context.pio_rx.sm, mpio_context.pio_rx.pin, 1, false);

	return 0;
}

int opentherm_pio_attached(void)
{
	return mpio_context.attached;
}

void opentherm_pio_log(opentherm_context_t *boiler)
{
	UNUSED(boiler);

	if (!mpio_context.attached)
		hlog_info(OTHLOG, "No OpenTherm device attached, connection count %d.",
						  mpio_context.conn_count);
	else
		hlog_info(OTHLOG, "OpenTherm device attached at %dhz, connection count %d.",
						  mpio_context.rx_hz, mpio_context.conn_count);
}
