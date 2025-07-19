// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"

#include "herak_sys.h"
#include "common_lib.h"
#include "common_internal.h"

struct sys_gpio_irq {
	gpio_irq_cb_t hook;
	void *context;
	uint32_t event_mask;
};

static struct {
	struct sys_gpio_irq *handlers[GPIO_PIN_MAX + 1];
} sys_irq_table;


int sys_add_irq_callback(int gpio_pin, gpio_irq_cb_t cb, uint32_t event_mask, void *user_context)
{
	if (gpio_pin < GPIO_PIN_MIN || gpio_pin > GPIO_PIN_MAX)
		return -1;

	if (sys_irq_table.handlers[gpio_pin])
		return -1;

	sys_irq_table.handlers[gpio_pin] = calloc(1, sizeof(struct sys_gpio_irq));
	if (!sys_irq_table.handlers[gpio_pin])
		return -1;

	sys_irq_table.handlers[gpio_pin]->hook = cb;
	sys_irq_table.handlers[gpio_pin]->context = user_context;
	sys_irq_table.handlers[gpio_pin]->event_mask = event_mask;

	return 0;
}

static void sys_gpio_irq_handle(uint gpio, uint32_t event_mask)
{
	if (gpio > GPIO_PIN_MAX || !sys_irq_table.handlers[gpio])
		return;
	if (!(sys_irq_table.handlers[gpio]->event_mask & event_mask))
		return;
	sys_irq_table.handlers[gpio]->hook(sys_irq_table.handlers[gpio]->context);
}

void sys_irq_init(void)
{
	int i;

	for (i = 0; i <= GPIO_PIN_MAX; i++) {
		if (!sys_irq_table.handlers[i] || !sys_irq_table.handlers[i]->hook)
			continue;
		gpio_set_irq_enabled_with_callback(i, sys_irq_table.handlers[i]->event_mask, true, sys_gpio_irq_handle);
	}
}
