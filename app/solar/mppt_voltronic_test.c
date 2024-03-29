// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#include <stdio.h>
#include "string.h"

#include "pico/stdlib.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/apps/mqtt.h"

#include "solar.h"

#define MPPT	"mppt-test"

//#define MPPT_DBUG
#ifdef MPPT_DBUG
#define DBG_LOG	hlog_info
#else
#define DBG_LOG	hlog_null
#endif

struct {
	int cmd;
	int rlen;
	char *reply;
} static test_data[] = {
		{MPPT_QID, 24,		(char[]){0x28, 0x39, 0x32, 0x38, 0x33, 0x32, 0x31, 0x30, 0x33, 0x31, 0x30, 0x30, 0x36, 0x33, 0x31, 0xE5, 0xE5, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, // 24
		{MPPT_QVFW, 24,		(char[]){0x28, 0x56, 0x45, 0x52, 0x46, 0x57, 0x3A, 0x30, 0x30, 0x30, 0x34, 0x31, 0x2E, 0x31, 0x37, 0xFC, 0xE8, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QPIRI, 104,	(char[]){0x28, 0x32, 0x33, 0x30, 0x2E, 0x30, 0x20, 0x31, 0x33, 0x2E, 0x30, 0x20, 0x32, 0x33, 0x30, 0x2E, 0x30, 0x20, 0x35, 0x30, 0x2E, 0x30, 0x20, 0x31,
									 0x33, 0x2E, 0x30, 0x20, 0x33, 0x30, 0x30, 0x30, 0x20, 0x33, 0x30, 0x30, 0x30, 0x20, 0x32, 0x34, 0x2E, 0x30, 0x20, 0x32, 0x33, 0x2E, 0x30, 0x20,
									 0x32, 0x31, 0x2E, 0x35, 0x20, 0x32, 0x38, 0x2E, 0x32, 0x20, 0x32, 0x37, 0x2E, 0x30, 0x20, 0x30, 0x20, 0x34, 0x30, 0x20, 0x30, 0x36, 0x30, 0x20,
									 0x30, 0x20, 0x31, 0x20, 0x32, 0x20, 0x31, 0x20, 0x30, 0x31, 0x20, 0x30, 0x20, 0x30, 0x20, 0x32, 0x37, 0x2E, 0x30, 0x20, 0x30, 0x20, 0x31, 0xF3,
									 0x17, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QFLAG, 16,	(char[]){0x28, 0x45, 0x61, 0x78, 0x79, 0x7A, 0x44, 0x62, 0x6A, 0x6B, 0x75, 0x76, 0x02, 0x7F, 0x0D, 0x00}},
		{MPPT_QPIGS, 112,	(char[]){0x28, 0x30, 0x30, 0x30, 0x2E, 0x30, 0x20, 0x30, 0x30, 0x2E, 0x30, 0x20, 0x32, 0x33, 0x30, 0x2E, 0x31, 0x20, 0x34, 0x39, 0x2E, 0x39, 0x20, 0x30,
									 0x30, 0x34, 0x36, 0x20, 0x30, 0x30, 0x32, 0x37, 0x20, 0x30, 0x30, 0x31, 0x20, 0x33, 0x37, 0x39, 0x20, 0x32, 0x36, 0x2E, 0x39, 0x30, 0x20, 0x30,
									 0x31, 0x39, 0x20, 0x31, 0x30, 0x30, 0x20, 0x30, 0x30, 0x31, 0x32, 0x20, 0x30, 0x32, 0x2E, 0x32, 0x20, 0x32, 0x31, 0x31, 0x2E, 0x36, 0x20, 0x30,
									 0x30, 0x2E, 0x30, 0x30, 0x20, 0x30, 0x30, 0x30, 0x30, 0x30, 0x20, 0x31, 0x30, 0x30, 0x31, 0x30, 0x31, 0x31, 0x30, 0x20, 0x30, 0x30, 0x20, 0x30,
									 0x30, 0x20, 0x30, 0x30, 0x34, 0x37, 0x32, 0x20, 0x31, 0x31, 0x30, 0x38, 0x53, 0x0D, 0x00, 0x00}},
		{MPPT_QMOD, 8,		(char[]){0x28, 0x42, 0xE7, 0xC9, 0x0D, 0x00, 0x00, 0x00}},
		{MPPT_QPIWS, 40,	(char[]){0x28, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
									 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xB2, 0xA7, 0x0D}},
		{MPPT_QDI, 80,		(char[]){0x28, 0x32, 0x33, 0x30, 0x2E, 0x30, 0x20, 0x35, 0x30, 0x2E, 0x30, 0x20, 0x30, 0x30, 0x33, 0x30, 0x20, 0x32, 0x31, 0x2E, 0x30, 0x20, 0x32, 0x37,
									 0x2E, 0x30, 0x20, 0x32, 0x38, 0x2E, 0x32, 0x20, 0x32, 0x33, 0x2E, 0x30, 0x20, 0x36, 0x30, 0x20, 0x30, 0x20, 0x30, 0x20, 0x32, 0x20, 0x30, 0x20,
									 0x30, 0x20, 0x30, 0x20, 0x30, 0x20, 0x30, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x30, 0x20, 0x31, 0x20, 0x30, 0x20, 0x32, 0x37, 0x2E, 0x30,
									 0x20, 0x30, 0x20, 0x31, 0x29, 0x46, 0x0D, 0x00}},
		{MPPT_QVFW3, 24,	(char[]){0x28, 0x56, 0x45, 0x52, 0x46, 0x57, 0x3A, 0x30, 0x30, 0x30, 0x30, 0x32, 0x2E, 0x36, 0x31, 0x17, 0x63, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_VERFW, 8,		(char[]){0x28, 0x4E, 0x41, 0x4B, 0x73, 0x73, 0x0D, 0x00}},
		{MPPT_QVFW2, 8,		(char[]){0x28, 0x4E, 0x41, 0x4B, 0x73, 0x73, 0x0D, 0x00}},
		{MPPT_QOPPT, 64,	(char[]){0x28, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31,
									 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31, 0x20, 0x31,
									 0x20, 0x31, 0x20, 0x30, 0x20, 0x30, 0x20, 0x30, 0xE1, 0x42, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QCHPT, 64,	(char[]){0x28, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32,
									 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32, 0x20, 0x32,
									 0x20, 0x32, 0x20, 0x30, 0x20, 0x30, 0x20, 0x30, 0x40, 0x7F, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QT, 24,		(char[]){0x28, 0x32, 0x30, 0x32, 0x33, 0x31, 0x32, 0x33, 0x31, 0x30, 0x39, 0x30, 0x30, 0x31, 0x37, 0x79, 0x6C, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QBEQI, 48,	(char[]){0x28, 0x30, 0x20, 0x30, 0x36, 0x30, 0x20, 0x30, 0x33, 0x30, 0x20, 0x30, 0x36, 0x30, 0x20, 0x30, 0x33, 0x30, 0x20, 0x32, 0x39, 0x2E, 0x32, 0x30,
									 0x20, 0x30, 0x30, 0x30, 0x20, 0x31, 0x32, 0x30, 0x20, 0x30, 0x20, 0x30, 0x30, 0x30, 0x30, 0x16, 0xA6, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QMN, 16,		(char[]){0x28, 0x56, 0x4D, 0x49, 0x49, 0x49, 0x2D, 0x33, 0x30, 0x30, 0x30, 0x8F, 0xBE, 0x0D, 0x00, 0x00}},
		{MPPT_QGMN, 8,		(char[]){0x28, 0x30, 0x33, 0x37, 0x61, 0x5A, 0x0D, 0x00}},
		{MPPT_QET, 16,		(char[]){0x28, 0x30, 0x30, 0x31, 0x30, 0x36, 0x30, 0x30, 0x30, 0x0B, 0x83, 0x0D, 0x00, 0x00, 0x00, 0x00}},
		{MPPT_QLT, 8,		(char[]){0x28, 0x4E, 0x41, 0x4B, 0x73, 0x73, 0x0D, 0x00}},
		{MPPT_QLED, 8,		(char[]){0x28, 0x4E, 0x41, 0x4B, 0x73, 0x73, 0x0D, 0x00}},
};

struct {
	uint16_t vid;
	uint16_t pid;
	int cud;
	usb_event_handler_t user_cb;
	void *user_context;
	bool mounted;
} static mppt_test_conext;

bool usb_init(void)
{
	memset(&mppt_test_conext, 0, sizeof(mppt_test_conext));
	mppt_test_conext.cud = -1;
	return true;
}

int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context)
{
	mppt_test_conext.vid = vid;
	mppt_test_conext.pid = pid;
	mppt_test_conext.user_cb = cb;
	mppt_test_conext.user_context = context;

	return 0;
}

int usb_send_to_device(int idx, char *buf, int len)
{
	const int test_size = ARRAY_SIZE(test_data);
	const char *cmd;
	int i;

	for (i = 0; i < test_size; i++) {
		if (mppt_get_qcommand_desc(test_data[i].cmd, &cmd, NULL))
			continue;
		if (len-3 == strlen(cmd) && !memcmp(buf, cmd, len-3))
			break;
	}

#ifdef MPPT_DBUG
	DBG_LOG(MPPT, "Got %s command:", i < test_size?"known":"unknown");
	dump_hex_data(MPPT, buf, len);
#endif

	mppt_test_conext.cud = i;
	return 0;
}

void usb_run(void)
{
	const int test_size = ARRAY_SIZE(test_data);
	int i;

	if (!mppt_test_conext.user_cb)
		return;

	if (!mppt_test_conext.mounted) {
		mppt_test_conext.user_cb(0, HID_MOUNT, NULL, 0, mppt_test_conext.user_context);
		mppt_test_conext.mounted = true;
		return;
	}
	if (mppt_test_conext.cud >= 0 && mppt_test_conext.cud < test_size) {
		for (i = 0; i < test_data[mppt_test_conext.cud].rlen; i += 8) {
			busy_wait_ms(100 * (rand() % 4));
			mppt_test_conext.user_cb(0, HID_REPORT, test_data[mppt_test_conext.cud].reply+i, 8, mppt_test_conext.user_context);
		}
		mppt_test_conext.cud = -1;
	}
}
