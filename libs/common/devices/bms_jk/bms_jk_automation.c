// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include <btstack_util.h>
#include "pico/platform/sections.h"

#include "bms_jk.h"

#define	WH_PAYLOAD_TEMPLATE "%s: %s"

#define BATT_STATE_COUNT_THR	3

/* Run automation scripts on state change */
static void jk_bt_auto_action(struct bt_auto_action_t *aauto)
{
	char notify_buff[WH_PAYLOAD_MAX_SIZE];

	/* Disable running scripts */
	if (aauto->state) {
		if (aauto->script_off_count && aauto->script_off_prefix)
			script_auto(aauto->script_off_prefix, true, false);
	} else {
		if (aauto->script_on_count && aauto->script_on_prefix)
			script_auto(aauto->script_on_prefix, true, false);
	}

	/* Run scripts for new state */
	if (aauto->state) {
		if (aauto->script_on_count && aauto->script_on_prefix) {
			script_auto(aauto->script_on_prefix, true, true);
			script_run(aauto->script_on_prefix, true);
			if (aauto->dev->ctx->wh_notify) {
				snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
						 DEV_NAME(aauto->dev), aauto->wh_on_message);
				webhook_send(notify_buff);
			}
			if (BMC_DEBUG(aauto->dev->ctx))
				hlog_info(BMS_JK_MODULE, "Run %d [%s] scripts",
						  aauto->script_on_count, aauto->script_on_prefix);
		}
	} else {
		if (aauto->script_off_count && aauto->script_off_prefix) {
			script_auto(aauto->script_off_prefix, true, true);
			script_run(aauto->script_off_prefix, true);
			if (aauto->dev->ctx->wh_notify) {
				snprintf(notify_buff, WH_PAYLOAD_MAX_SIZE, WH_PAYLOAD_TEMPLATE,
						 DEV_NAME(aauto->dev), aauto->wh_off_message);
				webhook_send(notify_buff);
			}

			if (BMC_DEBUG(aauto->dev->ctx))
				hlog_info(BMS_JK_MODULE, "Run %d [%s] scripts",
						  aauto->script_off_count, aauto->script_off_prefix);
		}
	}
	aauto->runs++;
	aauto->dev->mqtt.bms_data->force = true;
}

static void jk_bt_new_batt_state(struct jk_bms_dev_t *dev)
{
	int i;

	jk_bt_auto_action(&dev->auto_batt);
	for (i = 0; i < BMS_MAX_CELLS; i++) {
		if (!(1<<i & dev->cell_info.cells_enabled))
			break;
		dev->cell_info.cells_low_count[i] = 0;
		dev->cell_info.cells_high_count[i] = 0;
	}
}

static void jk_bt_check_cell_levels(struct jk_bms_dev_t *dev)
{
	int i;

	if (!dev->cell_info.valid)
		return;

	if (dev->auto_batt.state) {
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			if (!(1<<i & dev->cell_info.cells_enabled))
				break;
			if (dev->cell_info.cells_v[i] < dev->cell_v_low)
				dev->cell_info.cells_low_count[i]++;
			if (dev->cell_info.cells_low_count[i] >= BATT_STATE_COUNT_THR) {
				if (!dev->auto_batt.runs)
					hlog_info(BMS_JK_MODULE, "Battery %s is empty: cell %d is %3.2fV",
							DEV_NAME(dev), i, (float)(dev->cell_info.cells_v[i] * 0.001));
				dev->auto_batt.state = false;
				jk_bt_new_batt_state(dev);
				break;
			}
		}
	} else {
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			if (!(1<<i & dev->cell_info.cells_enabled))
				break;
			if (dev->cell_info.cells_v[i] >= dev->cell_v_high)
				dev->cell_info.cells_high_count[i]++;
		}
		for (i = 0; i < BMS_MAX_CELLS; i++) {
			if (!(1<<i & dev->cell_info.cells_enabled))
				break;
			if (dev->cell_info.cells_high_count[i] < BATT_STATE_COUNT_THR)
				break;
		}
		if (!(1<<i & dev->cell_info.cells_enabled)) {
			if (!dev->auto_batt.runs)
				hlog_info(BMS_JK_MODULE, "Battery %s is back to normal", DEV_NAME(dev));
			dev->auto_batt.state = true;
			jk_bt_new_batt_state(dev);
		}
	}
}

void jk_bt_automation_run(struct jk_bms_dev_t *dev)
{
	if (dev->auto_batt.enabled)
		jk_bt_check_cell_levels(dev);
}

static void jk_bt_auto_log(struct bt_auto_action_t *aauto)
{
	hlog_info(BMS_JK_MODULE, "\tAuto scripts: [%s*.run] %d , [%s*.run] %d",
			  aauto->script_on_prefix ? aauto->script_on_prefix : "NULL",
			  aauto->script_on_count,
			  aauto->script_off_prefix ? aauto->script_off_prefix : "NULL",
			  aauto->script_off_count);
}

void jk_bt_automation_log(struct jk_bms_dev_t *dev)
{
	if (dev->auto_batt.enabled) {
		hlog_info(BMS_JK_MODULE, "\tTrack battery state between %3.2fV and %3.2fV",
				  dev->cell_v_low * 0.001, dev->cell_v_high * 0.001);
		hlog_info(BMS_JK_MODULE, "\tBattery level is %s", dev->auto_batt.state ? "normal" : "low");
		jk_bt_auto_log(&dev->auto_batt);
	}
}

static void jk_bt_find_auto_scripts(struct bt_auto_action_t *aauto)
{
	if (aauto->script_on_count || aauto->script_off_count)
		return;

	if (aauto->script_on_prefix)
		free(aauto->script_on_prefix);
	aauto->script_on_prefix = NULL;
	sys_asprintf(&aauto->script_on_prefix, "%s_%s_", DEV_NAME(aauto->dev), aauto->script_on_name);
	if (aauto->script_on_prefix) {
		aauto->script_on_count = script_exist(aauto->script_on_prefix, true);
		if (BMC_DEBUG(aauto->dev->ctx))
			hlog_info(BMS_JK_MODULE, "Found %d scripts for %s with prefix [%s]",
					  aauto->script_on_count, aauto->script_on_name, aauto->script_on_prefix);
	}

	if (aauto->script_off_prefix)
		free(aauto->script_off_prefix);
	aauto->script_off_prefix = NULL;
	sys_asprintf(&aauto->script_off_prefix, "%s_%s_", DEV_NAME(aauto->dev), aauto->script_off_name);
	if (aauto->script_off_prefix) {
		aauto->script_off_count = script_exist(aauto->script_off_prefix, true);
		if (BMC_DEBUG(aauto->dev->ctx))
			hlog_info(BMS_JK_MODULE, "Found %d scripts for %s with prefix [%s]",
					  aauto->script_off_count, aauto->script_off_name, aauto->script_off_prefix);
	}
}

void jk_bt_automation_find_scripts(struct jk_bms_dev_t *dev)
{
	if (dev->auto_batt.enabled)
		jk_bt_find_auto_scripts(&dev->auto_batt);
}

void jk_bt_enable_battery_track(struct jk_bms_dev_t *dev, uint16_t cell_v_low, uint16_t cell_v_high)
{
	if (cell_v_low >= cell_v_high)
		return;
	dev->cell_v_low = cell_v_low;
	dev->cell_v_high = cell_v_high;
	dev->auto_batt.enabled = true;
	dev->auto_batt.script_on_name = "batt_normal";
	dev->auto_batt.script_off_name = "batt_low";
	dev->auto_batt.wh_on_message = "Normal battery";
	dev->auto_batt.wh_off_message = "Empty battery";
	dev->auto_batt.dev = dev;
}
