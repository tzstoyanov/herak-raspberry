// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "dev_uart.pio.h"
#include "dev_uart.h"
#include "common_internal.h"

#define UART_DEV_MODULE    "uart"

#define IS_DEBUG(D) ((D)->debug)

#define DMA_BUFFER_SIZE 32
struct pio_prog {
	int pin;
	int sm;
	PIO p;
	uint offset;
	const pio_program_t *program;
	pio_sm_config cfg;
	gpio_function_t pio_func;
	int clock_hz;
	enum gpio_dir direction;
	int debug;
};

static int pio_program_init(struct pio_prog *prog)
{
	pio_gpio_init(prog->p, prog->pin);
	gpio_set_dir(prog->pin, prog->direction);
	gpio_set_function(prog->pin, prog->pio_func);
	sm_config_set_set_pins(&prog->cfg, prog->pin, 1);
	if (prog->direction == GPIO_OUT) {
		pio_sm_set_pins_with_mask(prog->p, prog->sm, 1u << prog->pin, 1u << prog->pin);
		sm_config_set_out_pins(&prog->cfg, prog->pin, 1);
		pio_sm_set_consecutive_pindirs(prog->p, prog->sm, prog->pin, 1, true);
	} else {
		sm_config_set_in_pins(&prog->cfg, prog->pin);
		sm_config_set_in_shift(&prog->cfg, true, false, 32);
		gpio_pull_up(prog->pin);
	}
	sm_config_set_out_shift(&prog->cfg, true, false, 32);
	sm_config_set_clkdiv(&prog->cfg, (float)clock_get_hz(clk_sys) / prog->clock_hz);
	pio_sm_init(prog->p, prog->sm, prog->offset, &(prog->cfg));

	return 0;
}

static int pio_program_load(struct pio_prog *prog,
							const pio_program_t *program, pio_sm_config (*sm_config)(uint),
							int pin, enum gpio_dir direction, int clock)
{
	PIO pall[] = {pio0, pio1};
	int pall_count = ARRAY_SIZE(pall);
	int i;

	prog->sm = -1;
	prog->offset = -1;
	prog->program = program;
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
			prog->clock_hz = clock;
			prog->pin = pin;
			prog->direction = direction;
			prog->cfg = sm_config(prog->offset);
			return pio_program_init(prog);
		}
	}
	return -1;
}

static int pio_program_start(struct pio_prog *prog)
{
	pio_sm_init(prog->p, prog->sm, prog->offset, &(prog->cfg));
	pio_sm_set_enabled(prog->p, prog->sm, true);
	return 0;
}

static int pio_program_stop(struct pio_prog *prog)
{
	pio_sm_set_enabled(prog->p, prog->sm, false);
	return 0;
}

static int pio_program_read_buff(struct pio_prog *prog, uint8_t *buff, int size)
{
	uint32_t c;
	int i = 0;

	while (pio_sm_get_rx_fifo_level(prog->p, prog->sm) && size--) {
		c = pio_sm_get_blocking(prog->p, prog->sm);
		buff[i++] = (uint8_t)(c >> 24);
	}

	return i;
}

static int pio_program_send_buff(struct pio_prog *prog, uint8_t *buff, int size)
{
	int i;

	for (i = 0; i < size; i++)
		pio_sm_put_blocking(prog->p, prog->sm, (uint32_t)(buff[i]));

	return 0;
}

int dev_uart_init(struct dev_uart *dev, int rx, int tx, int baud)
{
	if (!dev || !GPIO_IS_VALID(rx) || !GPIO_IS_VALID(tx))
		return -1;
	memset(dev, 0, sizeof(struct dev_uart));
	dev->pio_rx = calloc(1, sizeof(struct pio_prog));
	if (!dev->pio_rx)
		goto out_err;
	dev->pio_tx = calloc(1, sizeof(struct pio_prog));

	dev->baud = baud;
	if (pio_program_load(dev->pio_rx, &uart_rx_program, uart_rx_program_get_default_config, rx, GPIO_IN, 8 * baud)) {
		hlog_warning(UART_DEV_MODULE, "PIO failed to load RX program.");
		goto out_err;
	}
	if (pio_program_load(dev->pio_tx, &uart_tx_program, uart_tx_program_get_default_config, tx, GPIO_OUT, 8 * baud)) {
		hlog_warning(UART_DEV_MODULE, "PIO failed to load TX program.");
		goto out_err;
	}

	return 0;

out_err:
	free(dev->pio_rx);
	free(dev->pio_tx);
	return -1;
}

int dev_uart_start(struct dev_uart *dev)
{
	int ret;

	if (!dev)
		return -1;
	ret = pio_program_start(dev->pio_rx);
	if (ret)
		return ret;
	ret = pio_program_start(dev->pio_tx);
	if (ret)
		pio_program_stop(dev->pio_rx);

	return ret;
}

int dev_uart_stop(struct dev_uart *dev)
{
	if (!dev)
		return -1;
	pio_program_stop(dev->pio_rx);
	pio_program_stop(dev->pio_tx);
	return 0;
}

int dev_uart_debug_set(struct dev_uart *dev, bool debug)
{
	if (!dev)
		return -1;
	dev->debug = debug;
	dev->pio_rx->debug = debug;
	dev->pio_tx->debug = debug;
	return 0;
}

int dev_uart_send(struct dev_uart *dev, uint8_t *buff, int size)
{
	return pio_program_send_buff(dev->pio_tx, buff, size);
}

int dev_uart_read(struct dev_uart *dev, uint8_t *buff, int size)
{
	return pio_program_read_buff(dev->pio_rx, buff, size);
}
