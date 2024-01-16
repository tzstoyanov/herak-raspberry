// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _MAIN_SOLAR_H_
#define _MAIN_SOLAR_H_

#include "common_lib.h"
#include "base64.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_data_internal_temp(float temp);

/* MPPT Voltron data */
typedef struct {
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
	int		bat_discharge_a;
} mqtt_mppt_data_t;
void mqtt_data_mppt(mqtt_mppt_data_t *data);

typedef struct {
	float bat_v;
	float bat_i;
	float soc_p;
	uint8_t bms_life;
	uint32_t remain_capacity;
} mqtt_bms_data_t;
void mqtt_data_bms(mqtt_bms_data_t *data);

/* MPPT Voltron */
bool mppt_solar_init(void);
void mppt_solar_query(void);
typedef enum {
	MPPT_QPI = 0,	/* Device Protocol ID Inquiry */
	MPPT_QID,		/* The device serial number inquiry */
	MPPT_QVFW,		/* Main CPU Firmware version inquiry */
	MPPT_QVFW2,		/* Another CPU Firmware version inquiry */
	MPPT_QVFW3,		/* Yet another CPU Firmware version */
	MPPT_VERFW,		/* Bluetooth version inquiry */
	MPPT_QPIRI,		/* Device Rating Information inquiry */
	MPPT_QFLAG,		/* Device flag status inquiry */
	MPPT_QPIGS,		/* Device general status parameters inquiry: (input, output voltages, currents, load, etc.) */
	MPPT_QPIGS2,	/* Device general status parameters (48V model) */
	MPPT_QMOD,		/* Device Mode inquiry: (power-on, standby, line mode, battery mode, etc.) */
	MPPT_QPIWS,		/* Device Warning Status inquiry */
	MPPT_QDI,		/* The default setting value information */
	MPPT_QMCHGCR,	/* Enquiry selectable value about max charging current */
	MPPT_QMUCHGCR,	/* Enquiry selectable value about max utility charging current */
	MPPT_QBOOT,		/* Enquiry DSP has bootstrap or not */
	MPPT_QOPM,		/* Enquiry output mode (For 4000/5000) */
	MPPT_QPGSn,		/* Parallel Information inquiry (For 4000/5000 */
	MPPT_QOPPT,		/* Device output source priority time order */
	MPPT_QCHPT,		/* Device charger source priority time order inquiry */
	MPPT_QT,		/* Time inquiry */
	MPPT_QBEQI,		/* Battery equalization status parameters */
	MPPT_QMN,		/* Query model name */
	MPPT_QGMN,		/* Query general model name */
	MPPT_QET,		/* Query total PV generated energy */
	MPPT_QEY,		/* Query PV generated energy of year */
	MPPT_QEM,		/* Query PV generated energy of month */
	MPPT_QED,		/* Query PV generated energy of day */
	MPPT_QLT,		/* Query total output load energy */
	MPPT_QLY,		/* Query output load energy of year */
	MPPT_QLM,		/* Query output load energy of year */
	MPPT_QLD,		/* Query output load energy of day */
	MPPT_QLED,		/* LED status parameters */
	MPPT_QMAX
} voltron_qcmd_t;

typedef enum {
	MPPT_S_PE = 0,	/* <XXX>: setting some status enable */
	MPPT_S_PD,		/* <XXX> setting some status disable*/
	MPPT_S_PF,		/* Setting control parameter to default value */
	MPPT_S_F,		/* <nn>: Setting device output rating frequency */
	MPPT_S_POP,		/* <NN>: Setting device output source priority */
	MPPT_S_PBCV,	/* <nn.n>: Set battery re-charge voltage */
	MPPT_S_PBDV,	/* <nn.n>: Set battery re-discharge voltage */
	MPPT_S_PCP,		/* <NN>: Setting device charger priority */
	MPPT_S_PGR,		/* <NN>: Setting device grid working range */
	MPPT_S_PBT,		/* <NN>: Setting battery type */
	MPPT_S_PSDV,	/* <nn.n>: Setting battery cut-off voltage (Battery under voltage) */
	MPPT_S_PCVV,	/* <nn.n>: Setting battery C.V. (constant voltage) charging voltage */
	MPPT_S_PBFT,	/* <nn.n>: Setting battery float charging voltage */
	MPPT_S_PPVOKC,	/* <n >: Setting PV OK condition */
	MPPT_S_PSPB,	/* <n >: Setting Solar power balance */
	MPPT_S_MCHGC,	/* <mnn>: Setting max charging current */
	MPPT_S_MUCHGC,	/* <mnn>: Setting utility max charging current */
	MPPT_S_POPM,	/* <mn >: Set output mode (For 4000/5000) */
	MPPT_S_PPCP,	/* <MNN>: Setting parallel device charger priority (For 4000/5000) */
	MPPT_S_MAX
} voltron_scmd_t;
char *mppt_get_qcommand(voltron_qcmd_t idx, int *len, char *append);
int mppt_get_qcommand_desc(voltron_qcmd_t idx, const char **cmd, const char **desc);
int mppt_verify_reply(char *reply, int len);
void mppt_volt_log(void);

/* BMS Daly */
bool bms_solar_init(void);
void bms_solar_query(void);
typedef enum {
	DALY_90 = 0,/* Query SOC of Total Voltage Current */
	DALY_91,	/* Query Maximum Minimum Voltage of Monomer */
	DALY_92,	/* Query Maximum minimum temperature of monomer */
	DALY_93,	/* Query Charge/discharge, MOS status */
	DALY_94,	/* Query Status Information 1 */
	DALY_95,	/* Query Cell voltage 1~48 */
	DALY_96,	/* Query Monomer temperature 1~16 */
	DALY_97,	/* Query Monomer equilibrium state */
	DALY_98,	/* Query Battery failure status */
	DALY_50,	/* Query Rated pack capacity and nominal cell voltage */
	DALY_51,	/* Query Number of acquisition board, Cell counts and Temp Sensor counts */
	DALY_53,	/* Query Battery operation mode / Production Date / Battery Type and Automatic sleep time */
	DALY_54,	/* Query Firmware index number */
	DALY_57,	/* Query Battery code */
	DALY_59,	/* Query Level 1 and 2 alarm thresholds for high and low cell voltages */
	DALY_5A,	/* Query Level 1 and 2 alarm thresholds for high and low voltages for the pack as a whole */
	DALY_5B,	/* Query Level 1 and 2 alarm thresholds for charge and discharge current for the pack */
	DALY_5E,	/* Query Level 1 and 2 alarm thresholds for allowable difference in cell voltage and temperature sensor readings */
	DALY_5F,	/* Query Voltage thresholds that control balancing */
	DALY_60,	/* Query Short-circuit shutdown threshold and the current sampling resolution */
	DALY_62,	/* Query Software Version */
	DALY_63,	/* Query Hardware Version */
	DALY_MAX
} daly_qcmd_t;

typedef enum {
	DALY_S_10 = 0,	/* Set the rated pack capacity and nominal cell voltage */
	DALY_S_11,		/* Set the Number of acquisition board, Cell counts and Temp Sensor counts */
	DALY_S_13,		/* Set Battery operation mode / Production Date / Battery Type and Automatic sleep time */
	DALY_S_14,		/* Set the Firmware index number */
	DALY_S_17,		/* Set the Battery code */
	DALY_S_19,		/* Set the Level 1 and 2 alarm thresholds for high and low cell voltages */
	DALY_S_1A,		/* Set the Level 1 and 2 alarm thresholds for high and low voltages for the pack as a whole */
	DALY_S_1B,		/* Set the Level 1 and 2 alarm thresholds for charge and discharge current for the pack */
	DALY_S_1E,		/* Set the Level 1 and 2 alarm thresholds for allowable difference in cell voltage and temperature sensor readings */
	DALY_S_1F,		/* Set the voltage thresholds that control balancing */
	DALY_S_20,		/* Set the short-circuit shutdown threshold and the current sampling resolution */
	DALY_S_MAX
} daly_scmd_t;
uint8_t *bms_get_qcommand(daly_qcmd_t idx, int *len);
daly_qcmd_t bms_verify_response(uint8_t *buf, int len);
int bms_get_qcommand_desc(daly_qcmd_t idx, const char **cmd, const char **desc);

void wh_notify_send(void);
bool wh_notify_init(void);

#endif /* _MAIN_SOLAR_H_ */
