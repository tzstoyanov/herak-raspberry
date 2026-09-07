// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _DEV_UART_H_
#define _DEV_UART_H_

struct pio_prog;

struct dev_uart {
	int baud;
	struct pio_prog *pio_rx;
	struct pio_prog *pio_tx;
	bool debug;
};

int dev_uart_init(struct dev_uart *dev, int rx, int tx, int baud);
int dev_uart_start(struct dev_uart *dev);
int dev_uart_stop(struct dev_uart *dev);
int dev_uart_send(struct dev_uart *dev, uint8_t *buff, int size);
int dev_uart_read(struct dev_uart *dev, uint8_t *buff, int size);
int dev_uart_debug_set(struct dev_uart *dev, bool debug);

#endif /* _DEV_UART_H_ */
