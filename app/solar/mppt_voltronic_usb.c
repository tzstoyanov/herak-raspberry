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

#define MPPT	"mppt"

//#define MPPT_DBUG
//#define MPPT_TEST_CMD

#pragma GCC diagnostic ignored "-Wstringop-truncation"

#ifdef MPPT_DBUG
#define DBG_LOG	hlog_info
#else
#define DBG_LOG	hlog_null
#endif

#define SENT_WAIT_MS		20000 /* Wait up to 20s for reply */
#define SENT_MIN_TIME_MS	5000 /* Send command each 5s */
#define CMD_BUF_SIZE		128
#define CMD_END_CHAR		'\r'
#define USB_DISCOVERY_MS	30000 /* If not connected, reset the USB bus on every 30sec */

struct voltron_qdi_data_t {
	float	ac_output_v;
	float	ac_output_hz;
	int	max_ac_charge_a;
	float	bat_under_v;
	float	charge_float_v;
	float	charge_bulk_v;
	float	bat_def_recharge_v;
	int		max_charge_a;
	int		ac_input_range_b;
	int		out_src_prio_b;
	int		charge_src_prio_b;
	int		bat_type_b;
	int		buzzer_b;
	int		power_save_b;
	int		overload_restart_b;
	int		overtemperature_restart_b;
	int		lcd_backlight_b;
	int		alarm_src_interupt_b;
	int		fault_code_b;
	int		lcd_timeout_b;
	int		pv_ok_parallel_b;
	int		pv_power_balance_b;
	int		overload_bypass_b;
	int		output_mode;
	float	bat_redischarge_v;
};

struct voltron_qpiri_data_t {
	float	grid_v;
	float	grid_a;
	float	ac_out_v;
	float	ac_out_hz;
	float	ac_out_a;
	int		ac_out_va;
	int		ac_out_w;
	float	bat_v;
	float	bat_recharge_v;
	float	bat_under_v;
	float	bat_bulk_v;
	float	bat_float_v;
	int		bat_type_b;
	int		ac_charging_a;
	int		charging_a;
	int		in_voltage_b;
	int		out_src_prio;
	int		charge_src_prio;
	int		parallel_num;
	int		mach_type;
	int		topo;
	int		out_mode;
	float	bat_redischarge_v;
	int		pv_ok_parallel_b;
	int		pv_power_balance_b;
};

struct voltron_qpigs_data_t {
	float	grid_v;
	float	grid_hz;
	float	ac_out_v;
	float	ac_out_hz;
	int		ac_out_va;
	int		ac_out_w;
	int		out_load_p;
	int		bus_v;
	float	bat_v;
	int		bat_charge_a;
	int		bat_capacity_p;
	int		sink_temp;
	float	pv_in_bat_a;
	float	pv_in_v;
	float	bat_scc_v;
	int		bat_discharge_a;
	int	stat_mask;
};

struct voltron_qpiws_data_t {
	bool	inverter_fault;
	bool	bus_over;
	bool	bus_under;
	bool	bus_soft_fail;
	bool	line_fail;
	bool	OPVShort;
	bool	inverter_v_low;
	bool	inverter_v_high;
	bool	over_temperature;
	bool	fan_locked;
	bool	battery_v_high;
	bool	battery_low;
	bool	battery_under_shutdown;
	bool	Overload;
	bool	eeprom_fault;
	bool	inverter_over_current;
	bool	inverter_soft_fail;
	bool	self_test_fail;
	bool	OPDC_v_over;
	bool	bat_open;
	bool	current_sensor_fail;
	bool	battery_short;
	bool	power_limit;
	bool	PV_v_high;
	bool	MPPT_overload_fault;
	bool	MPPT_overload_warning;
	bool	battery_low_to_charge;
};

struct voltron_qflags_data_t {
	uint8_t	buzzer:1;
	uint8_t	overload_bypass:1;
	uint8_t	power_saving:1;
	uint8_t	lcd_timeout:1;
	uint8_t	overload_restart:1;
	uint8_t	overtemp_restart:1;
	uint8_t	backlight:1;
	uint8_t	primary_source_interrupt_alarm:1;
	uint8_t	fault_code_record:1;
};

struct voltron_data_t {
	char	serial_number[MPPT_PARAM_FIXED_SIZE];	// QID
	char	firmware_vesion[MPPT_PARAM_FIXED_SIZE];	// QVFW
	char	firmware_vesion3[MPPT_PARAM_FIXED_SIZE];	// QVFW3
	char	model_name[MPPT_PARAM_FIXED_SIZE];		// QMN
	char	gen_model_name[MPPT_PARAM_FIXED_SIZE];	// QGMN
	char	mode;								// QMOD
	uint32_t pv_total_wh;						// QET
	datetime_t date;							// QT
	struct voltron_qflags_data_t	status_flags;	// QFLAG
	struct voltron_qpiws_data_t		warnings;		// QPIWS
	struct voltron_qdi_data_t		qdi_data;		// QDI
	struct voltron_qpiri_data_t		qpiri_data;		// QPIRI
	struct voltron_qpigs_data_t		qpigs_data;		// QPIGS
};

static struct {
	int vid; /* Vendor ID */
	int pid; /* Product ID */
	int usb_idx;
	bool usb_connected;
	bool send_in_progress;
	bool timeout_state;
	uint32_t timeout_count;
	uint64_t cmd_send_time;
	uint64_t usb_reset_time;
	voltron_qcmd_t cmd_idx;
	int cmd_count;
	char cmd_buff[CMD_BUF_SIZE];
	int cmd_buf_len;
	struct voltron_data_t vdata;
} mppt_context;

static bool get_mppt_config(void)
{
	bool ret = false;
	char *usb_id = NULL;
	char *rest;
	char *tok;
	int  id;

	if (MPPT_VOLTRON_USB_len < 1)
		return false;

	usb_id = param_get(MPPT_VOLTRON_USB);
	if (!usb_id || strlen(usb_id) < 1)
		goto out;
	rest = usb_id;
	tok = strtok_r(rest, ":", &rest);
	if (!tok)
		goto out;
	id = (int)strtol(tok, NULL, 16);
	if (id < 0 || id > 0xFFFF)
		goto out;
	mppt_context.vid = id;
	id = (int)strtol(rest, NULL, 16);
	if (id < 0 || id > 0xFFFF)
		goto out;
	mppt_context.pid = id;
	ret = true;
out:
	free(usb_id);
	return ret;
}

// (9283210100631
int qid_cmd_process(void)
{
	strncpy(mppt_context.vdata.serial_number, mppt_context.cmd_buff + 1, MPPT_PARAM_FIXED_SIZE - 1);
	DBG_LOG(MPPT, "QID reply: [%s]", mppt_context.vdata.serial_number);
	return 0;
}

// (VERFW:00041.17
int qvfw_cmd_process(void)
{
	char *ver = strchr(mppt_context.cmd_buff, ':');

	if (ver) {
		strncpy(mppt_context.vdata.firmware_vesion, ver + 1, MPPT_PARAM_FIXED_SIZE - 1);
		DBG_LOG(MPPT, "QVFW reply: [%s]", mppt_context.vdata.firmware_vesion);
		return 0;
	}

	DBG_LOG(MPPT, "QVFW broken reply: [%s]", mppt_context.cmd_buff);

	return -1;
}

// (EaxyzDbjkuv
int qflag_cmd_process(void)
{
	char *e = strchr(mppt_context.cmd_buff, 'E');

	DBG_LOG(MPPT, "QFLAG reply: [%s]", mppt_context.cmd_buff);

	memset(&(mppt_context.vdata.status_flags), 0, sizeof(mppt_context.vdata.status_flags));
	if (e) {
		while (*e != '\0' && *e != 'D' && *e != '\r') {
			switch (*e) {
			case 'A':
			case 'a':
					mppt_context.vdata.status_flags.buzzer = 1;
				break;
			case 'B':
			case 'b':
				mppt_context.vdata.status_flags.overload_bypass = 1;
				break;
			case 'J':
			case 'j':
				mppt_context.vdata.status_flags.power_saving = 1;
				break;
			case 'K':
			case 'k':
				mppt_context.vdata.status_flags.lcd_timeout = 1;
				break;
			case 'U':
			case 'u':
				mppt_context.vdata.status_flags.overload_restart = 1;
				break;
			case 'V':
			case 'v':
				mppt_context.vdata.status_flags.overtemp_restart = 1;
				break;
			case 'X':
			case 'x':
				mppt_context.vdata.status_flags.backlight = 1;
				break;
			case 'Y':
			case 'y':
				mppt_context.vdata.status_flags.primary_source_interrupt_alarm = 1;
				break;
			case 'Z':
			case 'z':
				mppt_context.vdata.status_flags.fault_code_record = 1;
				break;
			}
			e++;
		}
	}

	return 0;
}

// (230.0 50.0 0030 21.0 27.0 28.2 23.0 60 0 0 2 0 0 0 0 0 1 1 1 0 1 0 27.0 0 1
// (BBB.B CC.C 00DD EE.E FF.F GG.G HH.H II J K L M N O P Q R S T U V W YY.Y X Z
//   %f    %f   %d   %f   %f   %f   %f   %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d  %f %d %d
int qdi_cmd_process(void)
{
	struct voltron_qdi_data_t q, z;
	int ret;

	memset(&z, 0, sizeof(struct voltron_qdi_data_t));
	ret = sscanf(mppt_context.cmd_buff+1, "%f %f %d %f %f %f %f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %f %d %d",
			&q.ac_output_v, &q.ac_output_hz, &q.max_ac_charge_a, &q.bat_under_v, &q.charge_float_v,
			&q.charge_bulk_v, &q.bat_def_recharge_v, &q.max_charge_a, &q.ac_input_range_b, &q.out_src_prio_b,
			&q.charge_src_prio_b, &q.bat_type_b, &q.buzzer_b, &q.power_save_b, &q.overload_restart_b,
			&q.overtemperature_restart_b, &q.lcd_backlight_b, &q.alarm_src_interupt_b, &q.fault_code_b,
			&q.overload_bypass_b, &q.lcd_timeout_b, &q.output_mode, &q.bat_redischarge_v, &q.pv_ok_parallel_b,
			&q.pv_power_balance_b);

	if (ret == 25 && memcmp(&q, &z, sizeof(struct voltron_qdi_data_t))) {
		memcpy(&(mppt_context.vdata.qdi_data), &q, sizeof(struct voltron_qdi_data_t));
		DBG_LOG(MPPT, "QDI reply: [%s]: ", mppt_context.cmd_buff);
		DBG_LOG(MPPT, "   %3.2f %3.2f %d %3.2f %3.2f %3.2f %3.2f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %3.2f %d %d",
				q.ac_output_v, q.ac_output_hz, q.max_ac_charge_a, q.bat_under_v, q.charge_float_v,
				q.charge_bulk_v, q.bat_def_recharge_v, q.max_charge_a, q.ac_input_range_b, q.out_src_prio_b,
				q.charge_src_prio_b, q.bat_type_b, q.buzzer_b, q.power_save_b, q.overload_restart_b,
				q.overtemperature_restart_b, q.lcd_backlight_b, q.alarm_src_interupt_b, q.fault_code_b,
				q.overload_bypass_b, q.lcd_timeout_b, q.output_mode, q.bat_redischarge_v, q.pv_ok_parallel_b,
				q.pv_power_balance_b);
		return 0;
	}

	DBG_LOG(MPPT, "QDI broken reply: [%s]", mppt_context.cmd_buff);

	return -1;
}

// (230.0 13.0 230.0 50.0 13.0 3000 3000 24.0 23.0 21.5 28.2 27.0 0 40 060 0 1 2 1 01 0 0 27.0 0 1
// (BBB.B CC.C DDD.D EE.E FF.F HHHH IIII JJ.J KK.K JJ.J KK.K LL.L O PP QQ0 O P Q R SS T U VV.V W X
//   %f    %f    %f   %f   %f    %d  %d   %f   %f   %f   %f   %f %d %d  %d %d %d %d %d %d %d %d %f %d %d
int qpiri_cmd_process(void)
{
	struct voltron_qpiri_data_t q, z;
	int ret;

	memset(&z, 0, sizeof(struct voltron_qpiri_data_t));
	ret = sscanf(mppt_context.cmd_buff+1, "%f %f %f %f %f %d %d %f %f %f %f %f %d %d %d %d %d %d %d %d %d %d %f %d %d",
			&q.grid_v, &q.grid_a, &q.ac_out_v, &q.ac_out_hz, &q.ac_out_a, &q.ac_out_va, &q.ac_out_w, &q.bat_v,
			&q.bat_recharge_v, &q.bat_under_v, &q.bat_bulk_v, &q.bat_float_v, &q.bat_type_b, &q.ac_charging_a,
			&q.charging_a, &q.in_voltage_b, &q.out_src_prio, &q.charge_src_prio, &q.parallel_num, &q.mach_type,
			&q.topo, &q.out_mode, &q.bat_redischarge_v, &q.pv_ok_parallel_b, &q.pv_power_balance_b);

	if (ret == 25 && memcmp(&q, &z, sizeof(struct voltron_qpiri_data_t))) {
		memcpy(&(mppt_context.vdata.qpiri_data), &q, sizeof(struct voltron_qpiri_data_t));
		DBG_LOG(MPPT, "QPIRI reply: [%s]: ", mppt_context.cmd_buff);
		DBG_LOG(MPPT, "  %3.2f %3.2f %3.2f %3.2f %3.2f %d %d %3.2f %3.2f %3.2f %3.2f %3.2f %d %d %d %d %d %d %d %d %d %d %3.2f %d %d",
				q.grid_v, q.grid_a, q.ac_out_v, q.ac_out_hz, q.ac_out_a, q.ac_out_va, q.ac_out_w, q.bat_v,
				q.bat_recharge_v, q.bat_under_v, q.bat_bulk_v, q.bat_float_v, q.bat_type_b, q.ac_charging_a,
				q.charging_a, q.in_voltage_b, q.out_src_prio, q.charge_src_prio, q.parallel_num, q.mach_type,
				q.topo, q.out_mode, q.bat_redischarge_v, q.pv_ok_parallel_b, q.pv_power_balance_b);
		return 0;
	}

	DBG_LOG(MPPT, "QPIRI broken reply: [%s]: ", mppt_context.cmd_buff);

	return -1;

}

// (000.0 00.0 230.1 49.9 0046 0027 001 379 26.90 019 100 0012 02.2 211.6 00.00 00000 10010110 00 00 00472 110
// (BBB.B CC.C DDD.D EE.E FFFF GGGG HHH III JJ.JJ KKK OOO TTTT EEEE UUU.U WW.WW PPPPP 76543210
//	 %f    %f    %f    %f  %d    %d  %d  %d   %f   %d  %d   %d  %f     %f   %f    %d     %d
int qpigs_cmd_process(void)
{
	struct voltron_qpigs_data_t q, z;
	int ret;

	memset(&z, 0, sizeof(struct voltron_qpigs_data_t));
	ret = sscanf(mppt_context.cmd_buff+1, "%f %f %f %f %d %d %d %d %f %d %d %d %f %f %f %d %d",
			&q.grid_v, &q.grid_hz, &q.ac_out_v, &q.ac_out_hz, &q.ac_out_va, &q.ac_out_w, &q.out_load_p, &q.bus_v,
			&q.bat_v, &q.bat_charge_a, &q.bat_capacity_p, &q.sink_temp, &q.pv_in_bat_a, &q.pv_in_v, &q.bat_scc_v,
			&q.bat_discharge_a, &q.stat_mask);

	if (ret == 17 && memcmp(&q, &z, sizeof(struct voltron_qpigs_data_t))) {
		memcpy(&(mppt_context.vdata.qpigs_data), &q, sizeof(struct voltron_qpigs_data_t));
		DBG_LOG(MPPT, "QPIGS reply: [%s]: ", mppt_context.cmd_buff);
		DBG_LOG(MPPT, "  %3.2f %3.2f %3.2f %3.2f %d %d %d %d %3.2f %d %d %d %3.2f %3.2f %3.2f %d %d",
				q.grid_v, q.grid_hz, q.ac_out_v, q.ac_out_hz, q.ac_out_va, q.ac_out_w, q.out_load_p, q.bus_v,
				q.bat_v, q.bat_charge_a, q.bat_capacity_p, q.sink_temp, q.pv_in_bat_a, q.pv_in_v, q.bat_scc_v,
				q.bat_discharge_a, q.stat_mask);
		return 0;
	}

	DBG_LOG(MPPT, "QPIGS broken reply: [%s]: ", mppt_context.cmd_buff);

	return -1;
}

// (B
int qmod_cmd_process(void)
{
	mppt_context.vdata.mode = *(mppt_context.cmd_buff+1);
	DBG_LOG(MPPT, "QMOD reply: [%s]: ", mppt_context.cmd_buff);
	return 0;
}

// (00106000
int qet_cmd_process(void)
{
	int ret;
	int t;

	ret = sscanf(mppt_context.cmd_buff+1, "%d", &t);
	if (ret == 1) {
		mppt_context.vdata.pv_total_wh = t;
		DBG_LOG(MPPT, "QET reply: [%d]", mppt_context.vdata.pv_total_wh);
		return 0;
	}

	DBG_LOG(MPPT, "QET broken reply: [%s]: ", mppt_context.cmd_buff);
	return -1;
}

// (VMIII-3000
int qmn_cmd_process(void)
{
	strncpy(mppt_context.vdata.model_name, mppt_context.cmd_buff+1, MPPT_PARAM_FIXED_SIZE);
	DBG_LOG(MPPT, "QMN reply: [%s]", mppt_context.vdata.model_name);
	return 0;
}

// (037
int qgmn_cmd_process(void)
{
	strncpy(mppt_context.vdata.gen_model_name, mppt_context.cmd_buff+1, MPPT_PARAM_FIXED_SIZE);
	DBG_LOG(MPPT, "QGMN reply: [%s]", mppt_context.vdata.gen_model_name);
	return 0;
}

// (20231231090017
int qt_cmd_process(void)
{
	datetime_t date;
	char buf[5];
	int d;

	memset(&mppt_context.vdata.date, 0, sizeof(mppt_context.vdata.date));

	memcpy(buf, mppt_context.cmd_buff+1, 4);
	buf[4] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.year = d;

	memcpy(buf, mppt_context.cmd_buff+5, 2);
	buf[2] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.month = d;

	memcpy(buf, mppt_context.cmd_buff+7, 2);
	buf[2] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.day = d;

	memcpy(buf, mppt_context.cmd_buff+9, 2);
	buf[2] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.hour = d;

	memcpy(buf, mppt_context.cmd_buff+11, 2);
	buf[2] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.min = d;

	memcpy(buf, mppt_context.cmd_buff+13, 2);
	buf[2] = 0;
	if (sscanf(buf, "%d", &d) != 1)
		goto broken;
	date.sec = d;

	memcpy(&mppt_context.vdata.date, &date, sizeof(date));

	DBG_LOG(MPPT, "QT reply: [%d.%d.%d %d:%d:%d]",
			mppt_context.vdata.date.day, mppt_context.vdata.date.month,
			mppt_context.vdata.date.year, mppt_context.vdata.date.hour,
			mppt_context.vdata.date.min, mppt_context.vdata.date.sec);

	return 0;

broken:
	DBG_LOG(MPPT, "QT broken reply: [%s]: ", mppt_context.cmd_buff);
	return -1;
}

// (VERFW:00002.61
int qvfw3_cmd_process(void)
{
	char *ver = strchr(mppt_context.cmd_buff, ':');

	if (ver) {
		strncpy(mppt_context.vdata.firmware_vesion3, ver + 1, MPPT_PARAM_FIXED_SIZE - 1);
		DBG_LOG(MPPT, "QVFW3 reply: [%s]", mppt_context.vdata.firmware_vesion3);
		return 0;
	}

	DBG_LOG(MPPT, "QVFW3 broken reply: [%s]", mppt_context.cmd_buff);
	return -1;
}

struct qpiws_offset_t {
	int bit;
	bool *var;
};

// (00000100000000000000000000000000000
int qpiws_cmd_process(void)
{
	struct qpiws_offset_t warn[] = {
			{1, &mppt_context.vdata.warnings.inverter_fault},			// Inverter fault
			{2, &mppt_context.vdata.warnings.bus_over},					// Bus Over
			{3, &mppt_context.vdata.warnings.bus_under},				// Bus Under
			{4, &mppt_context.vdata.warnings.bus_soft_fail},			// Bus Soft Fail
			{5, &mppt_context.vdata.warnings.line_fail},				// LINE_FAIL
			{6, &mppt_context.vdata.warnings.OPVShort},					// OPVShort
			{7, &mppt_context.vdata.warnings.inverter_v_low},			// Inverter voltage too low
			{8, &mppt_context.vdata.warnings.inverter_v_high},			// Inverter voltage too high
			{9, &mppt_context.vdata.warnings.over_temperature},			// Over temperature
			{10, &mppt_context.vdata.warnings.fan_locked},				// Fan locked
			{11, &mppt_context.vdata.warnings.battery_v_high},			// Battery voltage high
			{12, &mppt_context.vdata.warnings.battery_low},				// Battery low alarm
			{14, &mppt_context.vdata.warnings.battery_under_shutdown},	// Battery under shutdown
			{16, &mppt_context.vdata.warnings.Overload},				// Over load
			{17, &mppt_context.vdata.warnings.eeprom_fault},			// Eeprom fault
			{18, &mppt_context.vdata.warnings.inverter_over_current},	// Inverter Over Current
			{19, &mppt_context.vdata.warnings.inverter_soft_fail},		// Inverter Soft Fail
			{20, &mppt_context.vdata.warnings.self_test_fail},			// Self Test Fail
			{21, &mppt_context.vdata.warnings.OPDC_v_over},				// OP DC Voltage Over
			{22, &mppt_context.vdata.warnings.bat_open},				// Bat Open
			{23, &mppt_context.vdata.warnings.current_sensor_fail},		// Current Sensor Fail
			{24, &mppt_context.vdata.warnings.battery_short},			// Battery Short
			{25, &mppt_context.vdata.warnings.power_limit},				// Power limit
			{26, &mppt_context.vdata.warnings.PV_v_high},				// PV voltage high
			{27, &mppt_context.vdata.warnings.MPPT_overload_fault},		// MPPT overload fault
			{28, &mppt_context.vdata.warnings.MPPT_overload_warning},	// MPPT overload warning
			{29, &mppt_context.vdata.warnings.battery_low_to_charge},	// Battery too low to charge
	};
	int sz = ARRAY_SIZE(warn);
	int i;

	memset(&(mppt_context.vdata.warnings), 0, sizeof(mppt_context.vdata.warnings));
	for (i = 0; i < sz; i++)
		if (*(mppt_context.cmd_buff + warn[i].bit + 1) == '1')
			*(warn[i].var) = true;
	DBG_LOG(MPPT, "QPIWS reply: [%s]: ", mppt_context.cmd_buff);
	return 0;
}

// One Time / rearly: QID, QVFW, QFLAG, QDI
// Useful: QPIRI QPIGS QMOD QPIWS
typedef int (*cmd_handler_t) (void);
static struct {
	voltron_qcmd_t id;
	cmd_handler_t cb;
	bool one_time;
	bool send;
	uint16_t min_reply_size;
} voltron_commnds_handler[] = {
		{MPPT_QID, qid_cmd_process, 1, 1, 15},
		{MPPT_QVFW, qvfw_cmd_process, 1, 1, 15},
		{MPPT_QFLAG, qflag_cmd_process, 1, 1, 12},
		{MPPT_QDI, qdi_cmd_process, 1, 1, 76},
		{MPPT_QPIRI, qpiri_cmd_process, 1, 1, 95},
		{MPPT_QPIGS, qpigs_cmd_process, 0, 1, 107},
		{MPPT_QMOD, qmod_cmd_process, 1, 1, 2},
		{MPPT_QPIWS, qpiws_cmd_process, 1, 1, 37},
		{MPPT_QET, qet_cmd_process, 0, 1, 9},
		{MPPT_QMN, qmn_cmd_process, 1, 1, 11},
		{MPPT_QGMN, qgmn_cmd_process, 1, 1, 4},
		{MPPT_QT, qt_cmd_process, 0, 1, 15}
};

static void mppt_send_mqtt_data(void)
{
	mqtt_mppt_data_t data;

	data.ac_out_v		= mppt_context.vdata.qpigs_data.ac_out_v;
	data.ac_out_hz		= mppt_context.vdata.qpigs_data.ac_out_hz;
	data.ac_out_va		= mppt_context.vdata.qpigs_data.ac_out_va;
	data.ac_out_w		= mppt_context.vdata.qpigs_data.ac_out_w;
	data.out_load_p		= mppt_context.vdata.qpigs_data.out_load_p;
	data.bus_v			= mppt_context.vdata.qpigs_data.bus_v;
	data.bat_v			= mppt_context.vdata.qpigs_data.bat_v;
	data.bat_capacity_p	= mppt_context.vdata.qpigs_data.bat_capacity_p;
	data.bat_charge_a	= mppt_context.vdata.qpigs_data.bat_charge_a;
	data.sink_temp		= mppt_context.vdata.qpigs_data.sink_temp;
	data.pv_in_bat_a	= mppt_context.vdata.qpigs_data.pv_in_bat_a;
	data.pv_in_v		= mppt_context.vdata.qpigs_data.pv_in_v;
	data.bat_discharge_a	= mppt_context.vdata.qpigs_data.bat_discharge_a;

	memcpy(data.serial_number, mppt_context.vdata.serial_number, MPPT_PARAM_FIXED_SIZE);
	memcpy(data.firmware_vesion, mppt_context.vdata.firmware_vesion, MPPT_PARAM_FIXED_SIZE);
	memcpy(data.firmware_vesion3, mppt_context.vdata.firmware_vesion3, MPPT_PARAM_FIXED_SIZE);
	memcpy(data.model_name, mppt_context.vdata.model_name, MPPT_PARAM_FIXED_SIZE);
	memcpy(data.gen_model_name, mppt_context.vdata.gen_model_name, MPPT_PARAM_FIXED_SIZE);

	mqtt_data_mppt(&data);
}

static int mppt_cmd_process_known(int len)
{
	const char *cmd;
	int ret = -1;
	int i;

	for (i = 0; i < mppt_context.cmd_count; i++) {
		if (voltron_commnds_handler[i].id == mppt_context.cmd_idx)
			break;
	}

	if (i >= mppt_context.cmd_count)
		return -1;

	if (len >= voltron_commnds_handler[i].min_reply_size) {
		if (voltron_commnds_handler[i].cb)
			ret = voltron_commnds_handler[i].cb();
		if (!ret) {
			if (voltron_commnds_handler[i].one_time)
				voltron_commnds_handler[i].send = false;
			mppt_send_mqtt_data();
		}
	} else {
		mppt_get_qcommand_desc(voltron_commnds_handler[i].id, &cmd, NULL);
	}
	return 0;
}

static void mppt_cmd_process(void)
{
	const char *cmd;
	int len;

	DBG_LOG(MPPT, "Process command %d reply", mppt_context.cmd_idx);
#ifdef MPTT_DBUG
		dump_hex_data(MPPT, mppt_context.cmd_buff, mppt_context.cmd_buf_len);
#endif
	if (mppt_context.cmd_idx >= MPPT_QMAX)
		return;

	len = mppt_verify_reply(mppt_context.cmd_buff, mppt_context.cmd_buf_len);
	if (mppt_cmd_process_known(len)) {
		mppt_get_qcommand_desc(mppt_context.cmd_idx, &cmd, NULL);
		hlog_info(MPPT, "Got reply of unknown command [%s] %d bytes", cmd, len);
	}
}

static void reset_state(void)
{
	int i;

	mppt_context.usb_connected = false;
	mppt_context.send_in_progress = false;
	mppt_context.timeout_state = false;
	for (i = 0; i < mppt_context.cmd_count; i++)
		voltron_commnds_handler[i].send = true;
}

static void mppt_usb_callback(int idx, usb_event_t event, const void *data, int len, void *context)
{
	int i;

	UNUSED(context);
	switch (event) {
	case HID_MOUNT:
		reset_state();
		mppt_context.usb_connected = true;
		hlog_info(MPPT, "Voltron device %d attached", idx);
		break;
	case HID_UNMOUNT:
		reset_state();
		hlog_info(MPPT, "Voltron device %d detached", idx);
		break;
	case HID_REPORT:
		DBG_LOG(MPPT, "Received HID_REPORT %d bytes", len);
#ifdef MPPT_DBUG
		dump_hex_data(MPPT, data, len);
#endif
		if ((mppt_context.cmd_buf_len + len) < CMD_BUF_SIZE) {
			memcpy(mppt_context.cmd_buff + mppt_context.cmd_buf_len, (char *)data, len);
			mppt_context.cmd_buf_len += len;
			for (i = 0; i < len; i++)
				if (((char *)data)[i] == CMD_END_CHAR)
					break;
			if (i < len) {
				mppt_cmd_process();
				mppt_context.cmd_buf_len = 0;
				mppt_context.send_in_progress = false;
				if (mppt_context.timeout_state)
					hlog_info(MPPT, "Got response of cmd %d, exit timeout state", mppt_context.cmd_idx);
				mppt_context.timeout_state = false;
			}
		} else {
			hlog_info(MPPT, "Command buffer overflow %d / %d", CMD_BUF_SIZE, mppt_context.cmd_buf_len + len);
		}
		break;
	default:
		break;
	}
}

static bool mppt_volt_log(void *context)
{

	UNUSED(context);

	if (mppt_context.usb_connected) {
		hlog_info(MPPT, "Connected to Voltronic, connection %s (%d)",
				  mppt_context.timeout_state?"timeout":"is active", mppt_context.timeout_count);
		hlog_info(MPPT, "   Model [%s], generic name [%s], firmware [%s], S/N [%s]",
				  mppt_context.vdata.model_name, mppt_context.vdata.gen_model_name,
				  mppt_context.vdata.firmware_vesion, mppt_context.vdata.serial_number);
		hlog_info(MPPT, "   Mode [%c], Device date [%.2d.%.2d.%.4d %.2dh], Total PV [%d] Wh",
				  mppt_context.vdata.mode?mppt_context.vdata.mode:'?',
				  mppt_context.vdata.date.day, mppt_context.vdata.date.month,
				  mppt_context.vdata.date.year, mppt_context.vdata.date.hour, mppt_context.vdata.pv_total_wh);
	} else {
		hlog_info(MPPT, "Not connected to Voltronic");
	}

	return true;
}

bool mppt_solar_init(void)
{
	memset(&mppt_context, 0, sizeof(mppt_context));
	mppt_context.usb_idx = -1;
	mppt_context.cmd_idx = 0;
	mppt_context.cmd_count = ARRAY_SIZE(voltron_commnds_handler);
	if (!get_mppt_config())
		return false;

	add_status_callback(mppt_volt_log, NULL);

	mppt_context.usb_idx = usb_add_known_device(mppt_context.vid, mppt_context.pid, mppt_usb_callback, NULL);
	if (mppt_context.usb_idx < 0)
		return false;

	return true;
}

#ifdef MPPT_TEST_CMD
static voltron_qcmd_t mppt_solar_cmd_test(void)
{
	static int idx;
	voltron_qcmd_t ids[] = {
			MPPT_QT,
			MPPT_QEY,		/* Query PV generated energy of year */
			MPPT_QEM,		/* Query PV generated energy of month */
			MPPT_QED,		/* Query PV generated energy of day */
			MPPT_QLY,		/* Query output load energy of year */
			MPPT_QLM,		/* Query output load energy of year */
			MPPT_QLD,		/* Query output load energy of day */
	};
	static int qsize = ARRAY_SIZE(ids);

	if (idx >= qsize)
		idx = 0;

	return ids[idx++];
}
#endif

static voltron_qcmd_t mppt_solar_cmd_next(void)
{
	static int idx;

	if (idx >= mppt_context.cmd_count)
		idx = 0;

	while (!voltron_commnds_handler[idx].send) {
		idx++;
		if (idx >= mppt_context.cmd_count)
			idx = 0;
	}
	return voltron_commnds_handler[idx++].id;
}

#define PARAM_MAX_LEN	18
static char *cmd_get(voltron_qcmd_t idx, int *len)
{
	char buf[PARAM_MAX_LEN];
	datetime_t dt, *date = NULL;
	char *param = NULL;

	if (mppt_context.vdata.date.year)
		date = &mppt_context.vdata.date;
	else if (tz_datetime_get(&dt))
		date = &dt;

	switch (idx) {
	case MPPT_QEY:
	case MPPT_QLY:
		if (!date)
			return NULL;
		snprintf(buf, PARAM_MAX_LEN, "%4d", date->year);
		param = buf;
		break;
	case MPPT_QEM:
	case MPPT_QLM:
		if (!date)
			return NULL;
		snprintf(buf, PARAM_MAX_LEN, "%4d%2d", date->year, date->month);
		param = buf;
		break;
	case MPPT_QED:
	case MPPT_QLD:
		if (!date)
			return NULL;
		snprintf(buf, PARAM_MAX_LEN, "%4d%2d%2d", date->year, date->month, date->day);
		param = buf;
		break;
	default:
		break;
	}

	return mppt_get_qcommand(mppt_context.cmd_idx, len, param);
}

void mppt_solar_query(void)
{
	const char *qcmd, *qdesc;
	uint64_t now;
	int len, ret;
	char *cmd;

	if (!mppt_context.usb_connected) {
		now = time_ms_since_boot();
		if ((now - mppt_context.usb_reset_time) > USB_DISCOVERY_MS) {
			usb_bus_restart();
			mppt_context.usb_reset_time = now;
		}
		return;
	}

	now = time_ms_since_boot();
	if ((now - mppt_context.cmd_send_time) > SENT_WAIT_MS) {
		if (!mppt_context.timeout_state) {
			mppt_context.timeout_count++;
			if (!mppt_get_qcommand_desc(mppt_context.cmd_idx, &qcmd, &qdesc))
				hlog_info(MPPT, "Response timeout of %s [%s]", qcmd, qdesc);
			else
				hlog_info(MPPT, "Response timeout of %d", mppt_context.cmd_idx);
		}
		mppt_context.send_in_progress = false;
		mppt_context.timeout_state = true;
	}
	if (!mppt_context.send_in_progress && (now - mppt_context.cmd_send_time) > SENT_MIN_TIME_MS) {
#ifndef MPPT_TEST_CMD
		mppt_context.cmd_idx = mppt_solar_cmd_next();
#else
		mppt_context.cmd_idx = mppt_solar_cmd_test();
#endif
		cmd = cmd_get(mppt_context.cmd_idx, &len);
		if (cmd) {
			mppt_context.cmd_buf_len = 0;
			ret = usb_send_to_device(mppt_context.usb_idx, cmd, len);
			if (!ret) {
				mppt_context.send_in_progress = true;
				mppt_context.cmd_send_time = now;
			}
			mppt_get_qcommand_desc(mppt_context.cmd_idx, &qcmd, &qdesc);
			DBG_LOG(MPPT, "Sent command %d: %s [%s]; %d", mppt_context.cmd_idx, qcmd, qdesc, ret);
		} else {
			hlog_info(MPPT, "Failed to prepare command %d", mppt_context.cmd_idx);
		}
	}
}
