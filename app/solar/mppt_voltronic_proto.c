// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "base64.h"
#include "params.h"
#include "solar.h"

#define COMMAND_MAX_LEN	24

#define END_OF_INPUT '\r'
#define IS_RESERVED_BYTE(_ch_) ( \
		((_ch_) == 0x28) || \
		((_ch_) == 0x0D) || \
		((_ch_) == 0x0A))

static const uint16_t crc_table[16] = {
	0x0000, 0x1021, 0x2042, 0x3063,
	0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B,
	0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};

struct mppt_command_t {
	char *cmd;
	char *desc;
};

/* Inquiry Commands */
static const struct mppt_command_t Qcommands[] = {
	{ "QPI",		"Device Protocol ID"},			// (PI<NN> <CRC><cr>
	{ "QID",		"The device serial number"},	// (9283210100631<CRC><cr>
	{ "QVFW",		"Main CPU Firmware version"},	// (VERFW:00041.17<CRC><cr>
	{ "QVFW2",		"Another CPU Firmware version"},// (NAK<CRC><cr>
	{ "QVFW3",		"Another CPU Firmware version"},// (VERFW:00002.61<CRC><cr>
	{ "VERFW",		"Bluetooth version inquiry"},	// (NAK<CRC><cr>
	{ "QPIRI",		"Device Rating Information"},	// (230.0 13.0 230.0 50.0 13.0 3000 3000 24.0 23.0 21.5 28.2 27.0 0 40 060 0 1 2 1 01 0 0 27.0 0 1<CRC><cr>
	{ "QFLAG",		"Device flag status"},			// (EaxyzDbjkuv<CRC><cr>
	{ "QPIGS",		"Device general status parameters"},	// (000.0 00.0 230.1 49.9 0046 0027 001 379 26.90 019 100 0012 02.2 211.6 00.00 00000 10010110 00 00 00472 110<CRC><cr>
	{ "QPIGS2",		"Device general status parameters (48V model)"}, // (BB.B CCC.C DDDDD <CRC><cr>
	{ "QMOD",		"Device Mode"},					// (B<CRC><cr>
	{ "QPIWS",		"Device Warning Status"},		// (00000100000000000000000000000000000<CRC><cr>
	{ "QDI",		"Default setting value information"},	// (230.0 50.0 0030 21.0 27.0 28.2 23.0 60 0 0 2 0 0 0 0 0 1 10100 1 0 27.0 0 1<CRC><cr>
	{ "QMCHGCR",	"Selectable value about max charging current" },			// ?
	{ "QMUCHGCR",	"Selectable value about max utility charging current" },	// ?
	{ "QBOOT",		"SP has bootstrap or not" },		// (NAH<CRC><cr>
	{ "QOPM",		"Output mode (For 4000/5000)" },	// (NAH<CRC><cr>
	{ "QPGSn",		"Parallel Information inquiry (For 4000/5000" }, // ?
	{ "QOPPT",		"Device output source priority time order" }, // (1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 0<CRC><cr>
	{ "QCHPT",		"Device charger source priority time order inquiry" }, // (2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 0 0 0<CRC><cr>
	{ "QT",			"Time inquiry" },	// (20231231090017<CRC><cr>
	{ "QBEQI",		"Battery equalization status parameters" },	// (0 060 030 060 030 29.20 000 120 0 0000CRC><cr>
	{ "QMN",		"Query model name" },	// (VMIII-3000<CRC><cr>
	{ "QGMN",		"Query general model name" },	// (037<CRC>
	{ "QET",		"Query total PV generated energy" },	// (00106000<CRC><cr>
	{ "QEY",	"Query PV generated energy of year" },	// (NNNNNNNN<CRC><cr>
	{ "QEM",	"Query PV generated energy of month" },	// (NNNNNNNN<CRC><cr>
	{ "QED", "Query PV generated energy of day" },	// (NNNNNNNN<CRC><cr>
	{ "QLT",		"Query total output load energy" },		// (NAK<CRC><cr>
	{ "QLY",	"Query output load energy of year" },	// (NNNNNNNN<CRC><cr>
	{ "QLM",	"Query output load energy of year" },	// (NNNNNNNN<CRC><cr>
	{ "QLD", "Query output load energy of day" },	// (NNNNNNNN<CRC><cr>
	{ "QLED",		"LED status parameters" }				// (NAK<cr>
};

/* Setting parameters Commands */
static const struct mppt_command_t Scommands[] = {
	{ "PE",		"<XXX>: setting some status enable" },
	{ "PD",		"<XXX> setting some status disable" },
	{ "PF",		"Setting control parameter to default value" },
	{ "F",		"<nn>: Setting device output rating frequency" },
	{ "POP",	"<NN>: Setting device output source priority" },
	{ "PBCV",	"<nn.n>: Set battery re-charge voltage" },
	{ "PBDV",	"<nn.n>: Set battery re-discharge voltage" },
	{ "PCP",	"<NN>: Setting device charger priority" },
	{ "PGR",	"<NN>: Setting device grid working range" },
	{ "PBT",	"<NN>: Setting battery type" },
	{ "PSDV",	"<nn.n>: Setting battery cut-off voltage (Battery under voltage)" },
	{ "PCVV",	"<nn.n>: Setting battery C.V. (constant voltage) charging voltage" },
	{ "PBFT",	"<nn.n>: Setting battery float charging voltage" },
	{ "PPVOKC",	"<n >: Setting PV OK condition" },
	{ "PSPB",	"<n >: Setting Solar power balance" },
	{ "MCHGC",	"<mnn>: Setting max charging current" },
	{ "MUCHGC",	"<mnn>: Setting utility max charging current" },
	{ "POPM",	"<mn >: Set output mode (For 4000/5000)" },
	{ "PPCP",	"<MNN>: Setting parallel device charger priority (For 4000/5000)" }
};

static uint16_t calculate_crc(const char *str_buffer, size_t len)
{
	const unsigned char *buffer = (const unsigned char *) str_buffer;
	unsigned char byte;
	uint16_t crc = 0;

	if (len < 1)
		return crc;

	do {
		byte = *buffer;
		crc = crc_table[(crc >> 12) ^ (byte >> 4)] ^ (crc << 4);
		crc = crc_table[(crc >> 12) ^ (byte & 0x0F)] ^ (crc << 4);
		buffer += sizeof(unsigned char);
	} while (--len);

	byte = crc;
	if (IS_RESERVED_BYTE(byte))
		crc += 1;
	byte = crc >> 8;
	if (IS_RESERVED_BYTE(byte))
		crc += 1 << 8;

	return crc;
}

int mppt_verify_reply(char *reply, int len)
{
	int rlen;
	uint16_t crc;
	int i;

	if (!reply || len < 4)
		return -1;

	if (reply[0] != '(')
		return -1;

	for (i = 0; i < len; i++)
		if (reply[i] == END_OF_INPUT)
			break;
	if (i >= len)
		return -1;

	rlen = i;
	if (rlen < 4)
		return -1;

	crc = calculate_crc(reply, rlen-2);
	if (reply[rlen-1] != (crc & 0xFF))
		return -1;

	if (reply[rlen-2] != ((crc >> 8) & 0xFF))
		return -1;

	reply[rlen-2] = 0;
	return strlen(reply);
}

bool mppt_check_qcommands(void)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);

	if (qcommads_count != MPPT_QMAX) {
		hlog_info("COLT", "Broken QComamnds array: %5 != %d", qcommads_count, MPPT_QMAX);
		return false;
	}
	return true;
}

char *mppt_get_qcommand(voltron_qcmd_t idx, int *len, char *append)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);
	static char cmd_buff[COMMAND_MAX_LEN];
	uint16_t crc;
	int cmd_len;

	if (idx > qcommads_count) {
		if (len)
			*len = 0;
		return NULL;
	}
	cmd_len = strlen(Qcommands[idx].cmd);
	if (append)
		cmd_len += strlen(append);
	if ((cmd_len + 3) >= COMMAND_MAX_LEN)
		return NULL;
	if (append)
		snprintf(cmd_buff, COMMAND_MAX_LEN, "%s%s", Qcommands[idx].cmd, append);
	else
		snprintf(cmd_buff, COMMAND_MAX_LEN, "%s", Qcommands[idx].cmd);
	crc = calculate_crc(cmd_buff, cmd_len);
	cmd_buff[cmd_len] = (crc >> 8) & 0xFF;
	cmd_buff[cmd_len+1] = crc & 0xFF;
	cmd_buff[cmd_len+2] = END_OF_INPUT;

	if (len)
		*len = cmd_len+3;
	return cmd_buff;
}

int mppt_get_qcommand_desc(voltron_qcmd_t idx, const char **cmd, const char **desc)
{
	static int qcommads_count = ARRAY_SIZE(Qcommands);

	if (idx > qcommads_count)
		return -1;

	if (cmd)
		*cmd = Qcommands[idx].cmd;
	if (desc)
		*desc = Qcommands[idx].desc;

	return 0;
}
