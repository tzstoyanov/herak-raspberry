// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "opentherm_dev.pio.h"
#include "opentherm.h"

#define OT_TIMEOUT_MS   200
#define MAX_RETRIES     2
#define MIN_INTERVAL_MS	150
#define DEAD_INTERVAL_MS  60000  // 1 min
#define MAX_SEARCH_HZ	1000000 // 1MHz

#define IS_PIO_LOG(C) ((C) && LOG_PIO_DEBUG)

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
static int opentherm_frame_decode(opentherm_pio_t *pio, uint32_t frame, opentherm_msg_t *msg)
{
	uint32_t parity = __builtin_popcount(frame);

	if (parity & 1) {
		if (IS_PIO_LOG(pio->log_mask))
			hlog_warning(OTHM_MODULE, "> Frame [0x%X] decode  error.\n", frame);
		return -1;
	}
	msg->msg_type = (frame >> 28) & 0x07;
	msg->id = (frame >> 16) & 0xFF;
	msg->value = frame & 0xFFFF;
	return 0;
}

static int opentherm_exchange_frame(opentherm_pio_t *pio, uint64_t out, uint32_t *in)
{
	uint64_t tstart;

	// Setup PIO
	pio_sm_init(pio->pio_tx.p, pio->pio_tx.sm, pio->pio_tx.offset, &pio->pio_tx.cfg);
	pio_sm_init(pio->pio_rx.p, pio->pio_rx.sm, pio->pio_rx.offset, &pio->pio_rx.cfg);
	pio_sm_drain_tx_fifo(pio->pio_tx.p, pio->pio_tx.sm);
	while (pio_sm_get_rx_fifo_level(pio->pio_tx.p, pio->pio_tx.sm) > 0)
		pio_sm_get(pio->pio_tx.p, pio->pio_tx.sm);
	while (pio_sm_get_rx_fifo_level(pio->pio_rx.p, pio->pio_rx.sm) > 0)
		pio_sm_get(pio->pio_rx.p, pio->pio_rx.sm);

	// Send data using transmitter PIO
	pio_sm_put_blocking(pio->pio_tx.p, pio->pio_tx.sm, (uint32_t)(out >> 32));
	pio_sm_put_blocking(pio->pio_tx.p, pio->pio_tx.sm, (uint32_t)(out));
	pio_sm_set_enabled(pio->pio_tx.p, pio->pio_tx.sm, true);

	// Wait for TX to finish
	tstart = time_ms_since_boot();
	while (pio_sm_get_tx_fifo_level(pio->pio_tx.p, pio->pio_tx.sm) > 0) {
		tight_loop_contents();
		if (time_ms_since_boot() - tstart > OT_TIMEOUT_MS) {
			// Time Out sending the request
			pio_sm_set_enabled(pio->pio_tx.p, pio->pio_tx.sm, false);
			return -1;
		}
	}

	// Wait for response
	pio_sm_set_enabled(pio->pio_rx.p, pio->pio_rx.sm, true);
	tstart = time_ms_since_boot();
	while (pio_sm_get_rx_fifo_level(pio->pio_rx.p, pio->pio_rx.sm) < 3 &&
		(time_ms_since_boot() - tstart) < OT_TIMEOUT_MS) {
		sleep_ms(1);
	}

	pio_sm_set_enabled(pio->pio_tx.p, pio->pio_tx.sm, false);
	pio_sm_set_enabled(pio->pio_rx.p, pio->pio_rx.sm, false);


	// Check timeout
	if (pio_sm_get_rx_fifo_level(pio->pio_rx.p, pio->pio_rx.sm) < 3)
		return -2;
	// Read response
	in[0] = pio_sm_get(pio->pio_rx.p, pio->pio_rx.sm);
	in[1] = pio_sm_get(pio->pio_rx.p, pio->pio_rx.sm);
	in[2] = pio_sm_get(pio->pio_rx.p, pio->pio_rx.sm);

	return 0;
}

#define END_BIT 0x80000000
// OpenTherm exchange function
static int opentherm_exchange_run(opentherm_pio_t *pio, opentherm_msg_t *request, opentherm_msg_t *reply)
{
	uint32_t f = opentherm_frame_encode(request->msg_type, request->id, request->value);
	uint64_t m = manchester_encode(f, true);
	uint32_t in[3];
	int ret;

	ret = opentherm_exchange_frame(pio, m, in);
	if (ret) {
		if (IS_PIO_LOG(pio->log_mask))
			hlog_warning(OTHM_MODULE, "> PIO %s frame timeout.\n", ret == -1 ? "send":"receive");
		return -1;
	}

	// Decode response
	if (in[2] != END_BIT) {
		if (IS_PIO_LOG(pio->log_mask))
			hlog_warning(OTHM_MODULE, "> PIO no valid EndBit received: 0x%X.\n", in[2]);
		return -1;
	}

	m = ((uint64_t)in[0] << 32) | in[1];
	f = 0;
	if (manchester_decode(m, false, &f)) {
		if (IS_PIO_LOG(pio->log_mask))
			hlog_warning(OTHM_MODULE, "> PIO no valid frame received: manchester decode failed.");
		return -1;
	}

	return opentherm_frame_decode(pio, f, reply);
}

int opentherm_dev_pio_exchange(opentherm_pio_t *pio, opentherm_msg_t *request, opentherm_msg_t *reply)
{
	int retry = MAX_RETRIES;

	if (!pio->attached)
		return -1;

	do {
		sleep_ms(MIN_INTERVAL_MS);
		wd_update();
		if (!opentherm_exchange_run(pio, request, reply)) {
			pio->last_valid = time_ms_since_boot();
			return 0;
		}
		retry--;
	} while (retry > 0);

	if ((time_ms_since_boot() - pio->last_valid) > DEAD_INTERVAL_MS) {
		if (IS_PIO_LOG(pio->log_mask))
			hlog_warning(OTHM_MODULE, "PIO connection lost.");
		pio->attached = false;
	}

	return -1;
}

int opentherm_dev_pio_find(opentherm_pio_t *pio)
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

	if (IS_PIO_LOG(pio->log_mask))
		hlog_info(OTHM_MODULE, "Looking for devices ... ");
	// Find frequency
	do {
		sm_config_set_clkdiv(&pio->pio_rx.cfg, (float)clock_get_hz(clk_sys) / hz);
		ret = opentherm_exchange_frame(pio, m, in);
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
		if (IS_PIO_LOG(pio->log_mask))
			hlog_info(OTHM_MODULE, "No devices found");
		sm_config_set_clkdiv(&pio->pio_rx.cfg, (float)clock_get_hz(clk_sys) / pio->rx_hz);
		return -1;
	}

	// Find min frequency
	do {
		min += 10;
		sm_config_set_clkdiv(&pio->pio_rx.cfg, (float)clock_get_hz(clk_sys) / (hz - min));
		ret = opentherm_exchange_frame(pio, m, in);
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
		sm_config_set_clkdiv(&pio->pio_rx.cfg, (float)clock_get_hz(clk_sys) / (hz + max));
		ret = opentherm_exchange_frame(pio, m, in);
		if (in[2] != END_BIT || ret)
			break;
		if (manchester_decode(((uint64_t)in[0] << 32) | in[1], false, &f))
			break;
		sleep_ms(MIN_INTERVAL_MS);
		wd_update();
	} while (1);
	max -= 10;

	hz += ((max-min)/2);
	sm_config_set_clkdiv(&pio->pio_rx.cfg, (float)clock_get_hz(clk_sys) / hz);
	hlog_info(OTHM_MODULE, "Device attached at %dhz", hz);
	pio->rx_hz = hz;
	pio->attached = true;
	pio->conn_count++;
	pio->last_valid = time_ms_since_boot();
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

// Load and configure opentherm tx and rx program
int opentherm_dev_pio_init(opentherm_pio_t *ctx)
{
	ctx->rx_hz = 96550;
	ctx->pio_rx.program = &opentherm_rx_program;
	ctx->pio_tx.program = &opentherm_tx_program;
	if (load_pio_program(&ctx->pio_rx)) {
		hlog_warning(OTHM_MODULE, "PIO failed to load RX program.");
		return -1;
	}
	if (load_pio_program(&ctx->pio_tx)) {
		hlog_warning(OTHM_MODULE, "PIO failed to load TX program.");
		return -1;
	}
	gpio_set_dir(ctx->pio_rx.pin, GPIO_IN);

	ctx->pio_tx.cfg = opentherm_tx_program_get_default_config(ctx->pio_tx.offset);
	gpio_init(ctx->pio_tx.pin);
	gpio_set_dir(ctx->pio_tx.pin, GPIO_OUT);
	gpio_set_function(ctx->pio_tx.pin, ctx->pio_tx.pio_func);
	sm_config_set_set_pins(&ctx->pio_tx.cfg, ctx->pio_tx.pin, 1);
	sm_config_set_out_pins(&ctx->pio_tx.cfg, ctx->pio_tx.pin, 1);
	sm_config_set_out_shift(&ctx->pio_tx.cfg, false, true, 32);
	sm_config_set_in_shift(&ctx->pio_tx.cfg, false, true, 32);
	sm_config_set_clkdiv(&ctx->pio_tx.cfg, (float)clock_get_hz(clk_sys) / 4000); // 4kHz clock, 250us
	pio_sm_init(ctx->pio_tx.p, ctx->pio_tx.sm, ctx->pio_tx.offset, &ctx->pio_tx.cfg);
	pio_sm_set_pins(ctx->pio_tx.p, ctx->pio_tx.sm, 1);
	pio_sm_set_consecutive_pindirs(ctx->pio_tx.p, ctx->pio_tx.sm, ctx->pio_tx.pin, 1, true);

	ctx->pio_rx.cfg = opentherm_rx_program_get_default_config(ctx->pio_rx.offset);
	gpio_init(ctx->pio_rx.pin);
	gpio_set_dir(ctx->pio_rx.pin, GPIO_IN);
	gpio_set_function(ctx->pio_rx.pin, ctx->pio_rx.pio_func);
	sm_config_set_set_pins(&ctx->pio_rx.cfg, ctx->pio_rx.pin, 1);
	sm_config_set_in_pins(&ctx->pio_rx.cfg, ctx->pio_rx.pin);
	sm_config_set_in_shift(&ctx->pio_rx.cfg, false, true, 32);
	sm_config_set_clkdiv(&ctx->pio_rx.cfg, (float)clock_get_hz(clk_sys) / ctx->rx_hz);
	pio_sm_init(ctx->pio_rx.p, ctx->pio_rx.sm, ctx->pio_rx.offset, &ctx->pio_rx.cfg);
	pio_sm_set_pins(ctx->pio_rx.p, ctx->pio_rx.sm, 0);
	pio_sm_set_consecutive_pindirs(ctx->pio_rx.p, ctx->pio_rx.sm, ctx->pio_rx.pin, 1, false);

	return 0;
}

int opentherm_dev_pio_attached(opentherm_pio_t *pio)
{
	return pio->attached;
}

void opentherm_dev_pio_log(opentherm_pio_t *pio)
{
	if (!pio->attached)
		hlog_info(OTHM_MODULE, "No OpenTherm device attached, connection count %d.",
				  pio->conn_count);
	else
		hlog_info(OTHM_MODULE, "OpenTherm device attached at %dhz, connection count %d.",
				  pio->rx_hz, pio->conn_count);
}
