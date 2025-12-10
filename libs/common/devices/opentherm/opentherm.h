// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025, Tzvetomir Stoyanov <tz.stoyanov@gmail.com>
 */

#ifndef _OPENTHER_H_
#define _OPENTHER_H_

#include "hardware/pio.h"
#include "herak_sys.h"
#include "common_internal.h"
#include "base64.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTHM_MODULE		"opentherm"

#define LOG_PIO_DEBUG	0x0001
#define LOG_OCMD_DEBUG	0x0002
#define LOG_MQTT_DEBUG	0x0004
#define LOG_UCMD_DEBUG	0x0008

#define OTH_MQTT_DATA_LEN	512
#define OTH_MQTT_COMPONENTS	40

#define GAS_TOTAL_RESET_MSEC	300000	// 5 min
#define MODULATION_MEASURE_MSEC	1000	// 1 sec

typedef struct {
	float ch_temperature_setpoint;			// DATA_ID_TSET
	float dhw_temperature_setpoint;			// DATA_ID_TDHWSET
	float ch_max;							// DATA_ID_MAXTSET
	float dhw_max;							// DATA_ID_TDHWSET
} opentherm_data_write_t;

typedef struct {	/* Static device data */
	bool force;
	uint8_t dev_id;							// DATA_ID_SECONDARY_CONFIG
	uint16_t ot_ver;						// DATA_ID_OPENTHERM_VERSION_SECONDARY
	uint8_t dev_type;						// DATA_ID_SECONDARY_VERSION
	uint8_t dev_ver;						// DATA_ID_SECONDARY_VERSION
	uint8_t dwh_present:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t control_type:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t cool_present:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t dhw_config:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t pump_control:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t ch2_present:1;					// DATA_ID_SECONDARY_CONFIG
	uint8_t max_capacity;					// DATA_ID_MAX_CAPACITY_MIN_MODULATION
	uint8_t min_mode_level;					// DATA_ID_MAX_CAPACITY_MIN_MODULATION
} opentherm_device_static_data_t;

typedef struct {	/* device config */
	bool force;
	uint8_t ch_max_cfg;						// DATA_ID_MAXTSET_BOUNDS
	uint8_t ch_min_cfg;						// DATA_ID_MAXTSET_BOUNDS
	uint8_t dhw_max_cfg;					// DATA_ID_TDHWSET_BOUNDS
	uint8_t dhw_min_cfg;					// DATA_ID_TDHWSET_BOUNDS
	float ch_temperature_setpoint_rangemin; // DATA_ID_MAXTSET_BOUNDS
	float ch_temperature_setpoint_rangemax; // DATA_ID_MAXTSET_BOUNDS
	float dhw_temperature_setpoint_rangemin; // DATA_ID_TDHWSET_BOUNDS
	float dhw_temperature_setpoint_rangemax; // DATA_ID_TDHWSET_BOUNDS
} opentherm_device_config_data_t;

typedef struct {	/* Sensors */
	bool force;
	float flow_temperature;					// DATA_ID_TBOILER
	float return_temperature;				// DATA_ID_TRET
	float flame_current;					// DATA_ID_FLAME_CURRENT
	float dhw_temperature;					// DATA_ID_TDHW
	float modulation_level;					// DATA_ID_REL_MOD_LEVEL
	float ch_pressure;						// DATA_ID_CH_PRESSURE
	float dhw_flow_rate;					// DATA_ID_DHW_FLOW_RATE
	float fan_speed;						// DATA_ID_BOILER_FAN_SPEED
	int16_t exhaust_temperature;			// DATA_ID_TEXHAUST
	/* Calculate mean modulation level in MODULATION_MEASURE_MSEC interval */
	time_t mod_level_time;
	float gas_flow;
	int mod_level_count;
	float mod_level_mean;
} opentherm_measure_data_t;

typedef struct {	/* status */
	bool force;
	uint8_t ch_enabled:1;					// DATA_ID_STATUS
	uint8_t dhw_enabled:1;					// DATA_ID_STATUS
	uint8_t cooling_enabled:1;				// DATA_ID_STATUS
	uint8_t otc_active:1;					// DATA_ID_STATUS
	uint8_t ch2_enabled:1;					// DATA_ID_STATUS
	uint8_t ch_active:1;					// DATA_ID_STATUS
	uint8_t dhw_active:1;					// DATA_ID_STATUS
	uint8_t flame_active:1;					// DATA_ID_STATUS
	uint8_t cooling_active:1;				// DATA_ID_STATUS
	uint8_t ch2_active:1;					// DATA_ID_STATUS
} opentherm_status_data_t;

typedef struct {	/* Errors */
	bool force;
	uint8_t fault_svc_needed:1;				// DATA_ID_ASF_FAULT
	uint8_t fault_low_water_pressure:1;		// DATA_ID_ASF_FAULT
	uint8_t fault_flame:1;					// DATA_ID_ASF_FAULT
	uint8_t fault_low_air_pressure:1;		// DATA_ID_ASF_FAULT
	uint8_t fault_high_water_temperature:1; // DATA_ID_ASF_FAULT
	uint8_t diagnostic_event:1;				// DATA_ID_STATUS
	uint8_t fault_active:1;					// DATA_ID_STATUS
	uint8_t fault_code;						// DATA_ID_ASF_FAULT
	uint16_t fault_burner_starts;			//DATA_ID_UNSUCCESSFUL_BURNER_STARTS
	uint16_t fault_flame_low;				//DATA_ID_FLAME_SIGNAL_LOW_COUNT
} opentherm_errors_data_t;

typedef struct {	/* Statistics */
	bool force;
	uint64_t stat_reset_time;				// Time when statistics had been reseted
	uint16_t stat_burner_starts;			// DATA_ID_BURNER_STARTS
	uint16_t stat_ch_pump_starts;			// DATA_ID_CH_PUMP_STARTS
	uint16_t stat_dhw_pump_starts;			// DATA_ID_DHW_PUMP_STARTS
	uint16_t stat_dhw_burn_burner_starts;	// DATA_ID_DHW_BURNER_STARTS
	uint16_t stat_burner_hours;				// DATA_ID_BURNER_OPERATION_HOURS
	uint16_t stat_ch_pump_hours;			// DATA_ID_CH_PUMP_OPERATION_HOURS
	uint16_t stat_dhw_pump_hours;			// DATA_ID_DHW_PUMP_OPERATION_HOURS
	uint16_t stat_dhw_burn_hours;			// DATA_ID_DHW_BURNER_OPERATION_HOURS
} opentherm_stats_data_t;

typedef struct {
	/* read */
	opentherm_measure_data_t		data;
	opentherm_errors_data_t			errors;
	opentherm_device_config_data_t	dev_config;
	opentherm_device_static_data_t	dev_static;
	opentherm_status_data_t			status;
	opentherm_stats_data_t			stats;
	/* Calculate gas consumption for GAS_TOTAL_RESET_MSEC interval */
	float qmin; // Minimum gas consumption in liters per sec
	float qmax;	// Maximum gas in consumption liters per sec
	time_t gas_reset;
	float gas_total;
	bool  gas_send;

	/* write */
	opentherm_data_write_t param_desired;
	opentherm_data_write_t param_actual;
} opentherm_data_t;

typedef enum {
	CMD_RESPONSE_OK = 0,
	CMD_RESPONSE_L1_ERR = 1,
	CMD_RESPONSE_WRONG_PARAM = 2,
	CMD_RESPONSE_INVALID = 3,
	CMD_RESPONSE_UNKNOWN = 4,
} opentherm_cmd_response_t;

typedef enum {
	MSG_TYPE_READ_DATA = 0,
	MSG_TYPE_WRITE_DATA = 1,
	MSG_TYPE_INVALID_DATA = 2,
	MSG_TYPE_READ_ACK = 4,
	MSG_TYPE_WRITE_ACK = 5,
	MSG_TYPE_DATA_INVALID = 6,
	MSG_TYPE_UNKNOWN_DATA_ID = 7
} opentherm_data_id_t;

typedef enum {
	DATA_ID_STATUS = 0,           // R Master and slave status flags
	DATA_ID_TSET = 1,             // W Control setpoint
	DATA_ID_PRIMARY_CONFIG = 2,   // W Master MemberID code
	DATA_ID_SECONDARY_CONFIG = 3, // R Slave configuration flags
	DATA_ID_COMMAND = 4,          // W Command: BLOR / CHWF
	DATA_ID_ASF_FAULT = 5,        // R Application-specific fault flags
	DATA_ID_RBP_FLAGS = 6,        // R transfer-enable flags & read/write flags
	DATA_ID_COOLING_CONTROL = 7,  // W Cooling control signal
	DATA_ID_TSETCH2 = 8,          // W Control setpoint 2
	DATA_ID_TROVERRIDE = 9,       // R Remote override room setpoint
	DATA_ID_TSP_COUNT = 10,       // R Number of transparent-slave-parameter (TSP) supported by the slave device.
	DATA_ID_TSP_DATA = 11,        // RW Index and Value of following TSP
	DATA_ID_FHB_COUNT = 12,       // R The size of the fault history buffer (FBH)
	DATA_ID_FHB_DATA = 13,        // R Index and Value of following Fault Buffer entry
	DATA_ID_MAX_REL_MODULATION = 14, // W Maximum relative modulation level setting
	DATA_ID_MAX_CAPACITY_MIN_MODULATION = 15, // R Maximum boiler capacity & Minimum modulation level
	DATA_ID_TRSET = 16,           // W Room Setpoint
	DATA_ID_REL_MOD_LEVEL = 17,   // R Relative modulation level
	DATA_ID_CH_PRESSURE = 18,     // R CH water pressure, bar
	DATA_ID_DHW_FLOW_RATE = 19,   // R DHW flow rate, l/min
	DATA_ID_DAY_TIME = 20,        // RW Day of Week & Time of Day
	DATA_ID_DATE = 21,            // RW Date
	DATA_ID_YEAR = 22,            // RW Year 1999-2099
	DATA_ID_TRSETCH2 = 23,        // W Room Setpoint CH2
	DATA_ID_TR = 24,              // W Room temperature
	DATA_ID_TBOILER = 25,         // R Boiler water temp.
	DATA_ID_TDHW = 26,            // R Domestic hot water temperature
	DATA_ID_TOUTSIDE = 27,        // R Outside temperature
	DATA_ID_TRET = 28,            // R Return water temperature
	DATA_ID_TSTORAGE = 29,        // R Solar storage temperature
	DATA_ID_TCOLLECTOR = 30,      // R Solar collector temperature
	DATA_ID_TFLOWCH2 = 31,        // R Flow temperature CH2
	DATA_ID_TDHW2 = 32,           // R DHW2 temperature
	DATA_ID_TEXHAUST = 33,        // R Exhaust temperature
	DATA_ID_HEATE_EXCHANGER = 34, // n R f8.8  Boiler heat exchanger temperature(°C)
	DATA_ID_BOILER_FAN_SPEED = 35,// n R u8/u8 Boiler fan speed Setpoint and actual value
	DATA_ID_FLAME_CURRENT = 36,   // n R f8.8  Electrical current through burner flame[μA]
	DATA_ID_TROOM_CH2 = 37,		  // n R f8.8  Room temperature for 2nd CH circuit(°C)
	DATA_ID_RELATIVE_HUMIDITY = 38, // n R f8.8 Actual relative humidity as a percentage
	DATA_ID_TROOM_OVERRIDE2 = 39, // n R f8.8  Remote Override Room Setpoint 2

	DATA_ID_TDHWSET_BOUNDS = 48,  // R Upper and Lower bound for adjustment of DHW setp
	DATA_ID_MAXTSET_BOUNDS = 49,  // R Upper and Lower bound for adjustment of maxCHsetp
	DATA_ID_HCRATIO_BOUNDS = 50,

	DATA_ID_TDHWSET = 56,         // RW Domestic hot water temperature setpoint
	DATA_ID_MAXTSET = 57,         // RW Maximum allowable CH water setpoint
	DATA_ID_HCRATIO = 58,

	DATA_ID_VENT_SET = 71,		  // n R -/u8  Relative ventilation position (0-100%). 0% is the minimum set ventilation and 100% is the maximum set ventilation.
	DATA_ID_STAT_VHEATR = 70,     // n R flag8/flag8   Master and Slave Status flags ventilation / heat - recovery
	DATA_ID_ASF_OEM_FAULT_CODE_VHEATR = 72, // n R flag8/u8  Application-specific fault flags and OEM fault code ventilation / heat-recovery
	DATA_ID_OEM_DIAG_CODE_VHEATR  = 73,     // n R u16  An OEM-specific diagnostic/service code for ventilation / heat-recovery system
	DATA_ID_SCONFIG_MEMBERID_VHEATR = 74,   // n R flag8/u8  Slave Configuration Flags / Slave MemberID Code ventilation / heat-recovery
	DATA_ID_OT_VER_VHEATR = 75,   // n R f8.8 The implemented version of the OpenTherm Protocol Specification in the ventilation / heat-recovery system.
	DATA_ID_VHEATR_VER = 76,      // n R u8/u8 Ventilation / heat-recovery product version number and type
	DATA_ID_REL_VENT_LEVEL = 77,   // n R -/u8  Relative ventilation (0-100%)
	DATA_ID_REL_HUM_EXHAUST = 78, // n R -/u8  Relative humidity exhaust air (0-100%)
	DATA_ID_CO2_EXHAUST = 79,     // n R u16  CO2 level exhaust air (0-2000 ppm)
	DATA_ID_TSI = 80,             // n R f8.8 Supply inlet temperature (°C)
	DATA_ID_TSO = 81,             // n R f8.8 Supply outlet temperature (°C)
	DATA_ID_TEI = 82,             // n R f8.8 Exhaust inlet temperature (°C)
	DATA_ID_TEO = 83,             // n R f8.8 Exhaust outlet temperature (°C)
	DATA_ID_RPM_EXHAUST = 84,     // n R u16 Exhaust fan speed in rpm
	DATA_ID_RPM_SUPPLY = 85,      // n R u16 Supply fan speed in rpm
	DATA_ID_RBP_FLAGS_VHEATR = 86,// n R flag8/flag8   Remote ventilation / heat-recovery parameter transfer-enable & read/write flags
	DATA_ID_NOM_RVENT = 87,       // n R u8/-  Nominal relative value for ventilation (0-100 %)
	DATA_ID_TSP_VHEATR = 88,      // n R u8/u8 Number of Transparent-Slave-Parameters supported by TSP’s ventilation / heat-recovery
	DATA_ID_TSP_VAL_VHEATR = 89,  // n R u8/u8 Index number / Value of referred-to transparent TSP’s ventilation / heat-recovery parameter.
	DATA_ID_FHB_SIZE_VHEATR = 90, // n R u8/u8 Size of Fault-History-Buffer supported by ventilation / heat-recovery
	DATA_ID_FHB_VAL_VHEATR = 91,  // n R u8/u8 Index number / Value of referred-to fault-history buffer entry ventilation / heat-recovery
	DATA_ID_BRAND = 93,           // n R u8/u8 Index number of the character in the text string ASCII character referenced by the above index number
	DATA_ID_BRAND_VER = 94,       // n R u8/u8 Index number of the character in the text string ASCII character referenced by the above index number
	DATA_ID_BRAD_SNUMBER = 95,    // n R u8/u8 Index number of the character in the text string ASCII character referenced by the above index number
	DATA_ID_COOL_OPER_HOURS = 96, // n R u16 Number of hours that the slave is in Cooling Mode. Reset by zero is optional for slave
	DATA_ID_POWER_CYCLES = 97,    // n R u16 Number of Power Cycles of a slave (wake-up after Reset), Reset by zero is optional for slave
	DATA_ID_RF_SENSOR_STAT = 98,  // n R special/special   For a specific RF sensor the RF strength and battery level is written
	DATA_ID_REMOTE_OVERRIDE_OPMODE_DHW = 99, // n R special/special   Operating Mode HC1, HC2/ Operating Mode DHW
	DATA_ID_REMOTE_OVERRIDE_FUNCTION = 100,  // n R R Remote override function
	DATA_ID_STAT_SSTORAGE = 101,  // n R flag8/flag8   Master and Slave Status flags Solar Storage
	DATA_ID_ASF_OEM_FAUL_CODE_SSTORAGE = 102,// n R flag8/u8  Application-specific fault flags and OEM fault code Solar Storage
	DATA_ID_SMEMBER_IDCODE_SSTORAGE = 103,   // n R flag8/u8  Slave Configuration Flags / Slave MemberID Code Solar Storage
	DATA_ID_VER_SSTORAGE = 104,   // n R u8/u8 Solar Storage product version number and type
	DATA_ID_TSP_SSTORAGE = 105,   // n R u8/u8 Number of Transparent - Slave - Parameters supported by TSP’s Solar Storage
	DATA_ID_TSP_VAL_SSTORAGE = 106, // n R u8/u8 Index number / Value of referred - to transparent TSP’s Solar Storage parameter.
	DATA_ID_FHB_SIZE_SSTORAGE = 107,// n R u8/u8     Size of Fault - History - Buffer supported by Solar Storage
	DATA_ID_FHB_VAL_SSTORAGE  = 108,// n R u8/u8     Index number / Value of referred - to fault - history buffer entry Solar Storage
	DATA_ID_ELPROD_STARTS = 109,    // n R U16     Number of start of the electricity producer.
	DATA_ID_ELPROD_HOURS = 110,     // n R U16     Number of hours the electricity produces is in operation
	DATA_ID_ELPROD = 111,           // n R U16     Current electricity production in Watt.
	DATA_ID_ELPROD_CUMULATIVE = 112,// n R U16     Cumulative electricity production in KWh.
	DATA_ID_UNSUCCESSFUL_BURNER_STARTS = 113, // n R u16 Number of un - successful burner starts
	DATA_ID_FLAME_SIGNAL_LOW_COUNT = 114, // n R u16 Number of times flame signal was too low
	DATA_ID_OEM_DIAGNOSTIC_CODE = 115,     // R OEM diagnostic code
	DATA_ID_BURNER_STARTS = 116,           // RW Burner starts
	DATA_ID_CH_PUMP_STARTS = 117,          // RW CH pump starts
	DATA_ID_DHW_PUMP_STARTS = 118,         // RW DHW pump/valve starts
	DATA_ID_DHW_BURNER_STARTS = 119,       // RW DHW burner starts
	DATA_ID_BURNER_OPERATION_HOURS = 120,  // RW Burner operation hours
	DATA_ID_CH_PUMP_OPERATION_HOURS = 121, // RW CH pump operation hours
	DATA_ID_DHW_PUMP_OPERATION_HOURS = 122,// RW DHW pump/valve operation hours
	DATA_ID_DHW_BURNER_OPERATION_HOURS = 123, // RW DHW burner operation hours
	DATA_ID_OPENTHERM_VERSION_PRIMARY = 124,  // W OpenTherm version Master
	DATA_ID_OPENTHERM_VERSION_SECONDARY = 125,// R OpenTherm version Slave
	DATA_ID_PRIMARY_VERSION = 126,            // W Master product version
	DATA_ID_SECONDARY_VERSION = 127,           // R Slave product version
	DATA_ID_CMD_MAX   = 128                   // MAX command ID, invalid
} opentherm_cmd_id_t;

enum {
	MQTT_SEND_DATA = 0,
	MQTT_SEND_STATS,
	MQTT_SEND_ERR,
	MQTT_SEND_MAX
};

typedef struct {
	mqtt_component_t *data;		/* ch_set */
	mqtt_component_t *errors;	/*  */
	mqtt_component_t *stats;
	mqtt_component_t mqtt_comp[OTH_MQTT_COMPONENTS];
	char payload[OTH_MQTT_DATA_LEN + 1];
	uint8_t send_id;
	uint64_t last_send;
} opentherm_mqtt_t;

typedef struct {
	int pin;
	int sm;
	PIO p;
	uint offset;
	const pio_program_t *program;
	pio_sm_config cfg;
	gpio_function_t pio_func;
} pio_prog_t;

typedef struct {
	int rx_hz;
	uint32_t *log_mask;
	bool attached;
	int conn_count;
	uint64_t last_valid;
	pio_prog_t pio_rx;
	pio_prog_t pio_tx;
} opentherm_pio_t;

typedef union {
	uint16_t u16;
	int16_t i16;
	float f;
	int8_t i8arr[2];
	uint8_t u8arr[2];
} ot_data_t;

struct opentherm_context_type;

typedef opentherm_cmd_response_t (*data_handler_t)(struct opentherm_context_type *ctx,
												   opentherm_cmd_id_t cmd, ot_data_t *out,
												   ot_data_t *in, bool write);
typedef struct {
	opentherm_cmd_id_t id;
	int cmd_type;
	int supported;
	data_handler_t func;
} ot_commands_t;

typedef struct {
	uint64_t last_send;
	uint64_t last_dev_lookup;
	uint64_t last_err_read;
	uint64_t last_stat_read;
	uint64_t last_cfg_read;
	ot_commands_t ot_commands[DATA_ID_CMD_MAX];
} opentherm_dev_t;

typedef struct opentherm_context_type {
	sys_module_t mod;
	uint32_t log_mask;
	opentherm_data_t data;
	opentherm_pio_t pio;
	opentherm_dev_t dev;
	opentherm_mqtt_t mqtt;
} opentherm_context_t;

typedef struct {
	uint8_t msg_type;
	uint8_t id;
	uint16_t value;
} opentherm_msg_t;

bool opentherm_log(void *context);

int opentherm_dev_init(opentherm_context_t *ctx);
void opentherm_dev_run(opentherm_context_t *ctx);
bool opentherm_dev_log(opentherm_context_t *ctx);
opentherm_cmd_response_t
opentherm_dev_read(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value);
opentherm_cmd_response_t
opentherm_dev_write(opentherm_context_t *ctx, opentherm_cmd_id_t cmd, uint16_t send, uint16_t *value);
void opentherm_dev_scan_all(opentherm_context_t *ctx);
void opentherm_reset_statistics(opentherm_context_t *ctx);

int opentherm_dev_pio_init(opentherm_pio_t *pio);
int opentherm_dev_pio_exchange(opentherm_pio_t *pio, opentherm_msg_t *request, opentherm_msg_t *reply);
int opentherm_dev_pio_attached(opentherm_pio_t *pio);
int opentherm_dev_pio_find(opentherm_pio_t *pio);
void opentherm_dev_pio_log(opentherm_pio_t *pio);

void opentherm_mqtt_init(opentherm_context_t *ctx);
void opentherm_mqtt_send(opentherm_context_t *ctx);

app_command_t *opentherm_user_comands_get(int *size);

#endif /* _OPENTHER_H_ */
