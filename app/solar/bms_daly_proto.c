// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <btstack_util.h>

#include "base64.h"
#include "params.h"
#include "solar.h"

#define FRAME_HEAD		0xa5
#define SELF_ADDRESS	0x80
/* 0x01	BMS Master */
/* 0x20	Bluetooth APP */
/* 0x40	GPRS / USB */
/* 0x80	Upper */
#define FRAME_DATA_LEN	8
#define COMMAND_LEN		13 /* head byte, address byte, data id byte, data length byte, 8 bytes data, crc byte */

typedef struct {
	int id;
	int cmd;
	char *desc;
} bms_command_t;

static const bms_command_t Qcommands[] = {
		{DALY_90, 0x90, "Query SOC of Total Voltage Current"},
		{DALY_91, 0x91, "Query Maximum Minimum Voltage of Monomer"},
		{DALY_92, 0x92, "Query Maximum minimum temperature of monomer"},
		{DALY_93, 0x93, "Query Charge/discharge, MOS status"},
		{DALY_94, 0x94, "Query Status Information 1"},
		{DALY_95, 0x95, "Query Cell voltage 1~48"},
		{DALY_96, 0x96, "Query Monomer temperature 1~16"},
		{DALY_97, 0x97, "Query Monomer equilibrium state"},
		{DALY_98, 0x98, "Query Battery failure status"},
		{DALY_50, 0x50, "Query Rated pack capacity and nominal cell voltage"},
		{DALY_51, 0x51, "Query Number of acquisition board, Cell counts and Temp Sensor counts"},
		{DALY_53, 0x53, "Query Battery operation mode / Production Date / Battery Type and Automatic sleep time"},
		{DALY_54, 0x54, "Query Firmware index number"},
		{DALY_57, 0x57, "Query Battery code"},
		{DALY_59, 0x59, "Query Level 1 and 2 alarm thresholds for high and low cell voltages"},
		{DALY_5A, 0x5A, "Query Level 1 and 2 alarm thresholds for high and low voltages for the pack as a whole"},
		{DALY_5B, 0x5B, "Query Level 1 and 2 alarm thresholds for charge and discharge current for the pack."},
		{DALY_5E, 0x5E, "Query Level 1 and 2 alarm thresholds for allowable difference in cell voltage and temperature sensor readings"},
		{DALY_5F, 0x5F, "Query Voltage thresholds that control balancing"},
		{DALY_60, 0x60, "Query Short-circuit shutdown threshold and the current sampling resolution"},
		{DALY_62, 0x62, "Query Software Version"},
		{DALY_63, 0x63, "Query Hardware Version"}
};

static const bms_command_t Scommands[] = {
		{DALY_S_10, 0x10, "Set the rated pack capacity and nominal cell voltage"},
		{DALY_S_11, 0x11, "Set the Number of acquisition board, Cell counts and Temp Sensor counts"},
		{DALY_S_13, 0x13, "Set Battery operation mode / Production Date / Battery Type and Automatic sleep time"},
		{DALY_S_14, 0x14, "Set the Firmware index number"},
		{DALY_S_17, 0x17, "Set the Battery code"},
		{DALY_S_19, 0x19, "Set the Level 1 and 2 alarm thresholds for high and low cell voltages"},
		{DALY_S_1A, 0x1A, "Set the Level 1 and 2 alarm thresholds for high and low voltages for the pack as a whole"},
		{DALY_S_1B, 0x1B, "Set the Level 1 and 2 alarm thresholds for charge and discharge current for the pack"},
		{DALY_S_1E, 0x1E, "Set the Level 1 and 2 alarm thresholds for allowable difference in cell voltage and temperature sensor readings"},
		{DALY_S_1F, 0x1F, "Set the voltage thresholds that control balancing"},
		{DALY_S_20, 0x20, "Set the short-circuit shutdown threshold and the current sampling resolution"}
};

char *bms_get_qcommand(daly_qcmd_t idx, int *len)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);
	static char cmd_buff[COMMAND_LEN];
	int i;

	for (i = 0; i < qcommads_count; i++) {
		if (Qcommands[i].id == idx)
			break;
	}
	if (i == qcommads_count)
		return NULL;

	memset(cmd_buff, 0, COMMAND_LEN);
	cmd_buff[0] = FRAME_HEAD;
	cmd_buff[1] = SELF_ADDRESS;
	cmd_buff[2] = Qcommands[i].cmd;
	cmd_buff[3] = FRAME_DATA_LEN;
	cmd_buff[12] = btstack_crc8_calc(cmd_buff, COMMAND_LEN);

	if (len)
		*len = COMMAND_LEN;

	return cmd_buff;
}

daly_qcmd_t bms_verify_response(char *buf, int len)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);
	uint8_t crc;
	int i;

	if (!buf)
		return DALY_MAX;
	if (len < COMMAND_LEN)
		return DALY_MAX;

	if (buf[0] != FRAME_HEAD)
		return DALY_MAX;
	if (buf[1] != 0x01) /* BMS Master address */
		return DALY_MAX;
	if (buf[3] != FRAME_DATA_LEN)
		return DALY_MAX;
	crc = buf[12];
	buf[12] = 0;
	if (crc != btstack_crc8_calc(buf, COMMAND_LEN))
		return DALY_MAX;
	for (i = 0; i < qcommads_count; i++) {
		if (Qcommands[i].cmd == buf[2])
			return Qcommands[i].id;
	}

	return DALY_MAX;
}

int bms_get_qcommand_desc(daly_qcmd_t idx, const char **cmd, const char **desc)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);
	static char cmd_id[5];
	int i;

	for (i = 0; i < qcommads_count; i++) {
		if (Qcommands[i].id == idx)
			break;
	}

	if (i == qcommads_count)
		return -1;

	if (desc)
		*desc = Qcommands[i].desc;
	if (cmd) {
		snprintf(cmd_id, 5, "0x%0.2X", Qcommands[i].cmd);
		*cmd = cmd_id;
	}

	return 0;
}
