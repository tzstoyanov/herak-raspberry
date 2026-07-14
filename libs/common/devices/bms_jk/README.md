# JK BMS
Support for [JK BMS](../../../../docs/jk-bms-manual-1520084771.pdf) over bluetooth and [HLK-B40](../../../../docs/HLK-B40.pdf) serial interface.

## Configuration
Configuration parameters in `params.txt` file:
```
BMS_BT          <XX:XX:XX:XX:XX:XX>,<pin>;<XX:XX:XX:XX:XX:XX>,<pin> ...
BMS_MODEL       JK;JK ...
BMS_TIMEOUT_SEC <seconds>;<seconds>...
BMS_CELL_LEVELS <low>,<high>;<low>,<high>...
BMS_NAME        <name1>;<name2>...
BMS_NOTIFY      <0/1>
BMS_CHARGE_CURRENT_THRESHOLD  <ampere1>;<ampere2>...
```
- `BMS_BT`, mandatory. `<XX:XX:XX:XX:XX:XX>` is the bluetooth address of the BMS, `<pin>` is the pin code used for authorization. Up to 4 devices are supported, separated by `;`. All parameters follow the same list logic - configuration per BMS, separated by `;` and the order corresponds to the BMSs in the `BMS_BT` list. The `BMS_MODEL` parameter must be set to `JK`.  
- `BMS_TIMEOUT_SEC`, optional. If set, the raspberry pico will be rebooted if there is no valid response from a connected BMS device since `<seconds>`. 
- `BMS_CELL_LEVELS`, optional - track the battery charge level. See [Track battery state](#track-battery-state) section.
- `BMS_NAME`, optional -  sets a user friendly name of the battery, connected to that BMS. The name is used in logs, notifications and name prefix of the auto scripts. If not set, the ID of the battery is used.  
- `BMS_NOTIFY`, optional - controls whether to send webhook notifications when the battery state changes, if the logic for battery level id enabled with the `BMS_CELL_LEVELS` parameter. 
- `BMS_CHARGE_CURRENT_THRESHOLD`, optional - track the level of the solar energy. See [Track Solar energy](#track-solar-energy) section.

Example configuration:
```
BMS_BT                  11:2A:33:2B:3C:44,1234;22:3A:44:3B:4C:55,0000;
BMS_MODEL               JK;JK
BMS_TIMEOUT_SEC         1200;600
BMS_CELL_LEVELS         3.0,3.1;2.8,2.9
BMS_NAME                MainBattery
BMS_NOTIFY              1
```

## Automation
### Track battery state
If `BMS_CELL_LEVELS` parameter is defined, the charge state of the battery is tracked. A set of [scripts](../../services/scripts/README.md) can be executed when the battery changes its state from `low` to `normal` and vice versa. The scripts names must be prefixed with `BMS_NAME`, or if this parameter is not set - the ID of the battery, as it is reported by the JK BMS. Scripts are detected only at boot time, so when a new script is copied the device must be rebooted.
- When the battery voltage falls below the configured `low` threshold, all scripts prefixed with `<BMS_NAME>_batt_low_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_batt_normal_` are disabled.
- When the battery voltage raises above the configured `high` threshold, all scripts prefixed with `<BMS_NAME>_batt_normal_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_batt_low_` are disabled.

### Track Solar energy
if `BMS_CHARGE_CURRENT_THRESHOLD` parameter is defined, the level of the solar energy is tracked.
- Solar deficiency:
  - If the battery charge current is negative.
  - If the battery charge state is less than **90%** AND the battery charge current is less than `BMS_CHARGE_CURRENT_THRESHOLD`.
- Solar excess:
  - If the battery charge state is more or equal to **90%** OR the battery charge current is bigger or equal to `BMS_CHARGE_CURRENT_THRESHOLD`.  

A set of [scripts](../../services/scripts/README.md) can be executed when solar excess or deficiency is detected. The scripts names must be prefixed with `BMS_NAME`, or if this parameter is not set - the ID of the battery, as it is reported by the JK BMS. Scripts are detected only at boot time, so when a new script is copied the device must be rebooted.
- When a solar excess is detected, all scripts prefixed with `<BMS_NAME>_solar_on_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_solar_off_` are disabled.
- When a solar deficiency is detected, all scripts prefixed with `<BMS_NAME>_solar_off_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_solar_on_` are disabled.
The solar energy algorithm works using the try-and-see approach. If solar excess is detected and declared, new consumer can be activated. That can cause a solar deficiency, if the consumer consumes more that the power excess. In that case, the algorithm waits for `2 minutes` in `solar deficiency` state, before trying to detect an excess. If the state bounds between `excess` and `deficiency` on each 2 min, most probably the power excess is less than the activated power consumer.

## Monitor
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<topic>/bms_jk<id>/cell_0_v/status` - Voltage of all cells:  
- `cell_<id>_v:<value>>` - Voltage of cell with `<id>`, V.  

`<topic>/bms_jk<id>/cell_0_r/status` - Resistance of all cells:  
- `cell_<id>_r:<value>` - Resistance of cell with `<id>`, Ohms.    

`<topic>/bms_jk<id>/v_avg/status` - Various BMS sensors:  
- `v_avg:<value>` - Average voltage of the battery, V.   
- `v_delta:<value>` - Voltage difference between cells with minimum and maximum voltage, V.  
- `cell_v_min:<value>` - Index of the cell with lowest voltage.  
- `cell_v_max:<value>` - Index of the cell with highest voltage.  
- `batt_action:<value>` - Battery action: 0 - Off, 1 - Charging, 2 - Discharging.  
- `power_temp:<value>` - Power tube temperature, °C.  
- `batt_temp1:<value>` - Temperature sensor 1, °C.  
- `batt_temp2:<value>` - Temperature sensor 2, °C.  
- `batt_temp_mos:<value>` - MOS Temperature, °C.  
- `batt_volt:<value>` - Battery voltage, V.  
- `batt_power:<value>` - Battery power, W.  
- `batt_state:<value>` - Battery charge state, %.  
- `batt_cycles:<value>` - Battery charging cycles.  
- `batt_charge_curr:<value>` - Battery charge / discharge current, A.  
- `batt_balance_curr:<value>` - Battery balance current, A.  
- `batt_cap_rem:<value>` - Battery capacity remaining, Ah.  
- `batt_cycles_cap:<value>` - Battery charging cycle capacity, Ah.  
- `soh:<value>` - State Of Health, %.  
- `batt_heat_a:<value>` - Heating current, A.  
- `batt_low:<value>` - Calculated battery state: 1 - low, 0 - normal.  
- `solar_excess:<value>` - Detected solar excess: 1 - on, 0 - off.

`<topic>/bms_jk<id>/Vendor/status` - BMS information:  
- `Vendor:<string>` - BMS vendor.  
- `Model:<string>` - BMS model.  
- `Hardware:<string>` - Hardware revision.  
- `Software:<string>` - Software revision.  
- `SerialN:<string>` - Serial number.  
- `Uptime:<string>` - Uptime.  
- `PowerOnCount:<value>` - Power On Count.  

## Commands
The commands can be executed using the [commands engine](../../services/commands/README.md).  

- `charge_current_threshold", ":<id>:<int, ampere>` - threshold of the charge current, used for solar excess detection.The `id` parameter is the index of a BMS device, which follows the order of device in `BMS_BT` list. The first device defined there is with index `0`, the second is with index `1` and so forth. The second parameter `<int, ampere>` is the charge current used to detect if there is solar excess - overwrites the `BMS_CHARGE_CURRENT_THRESHOLD` parameter. If `<int, ampere>` is negative or 0, the solar energy tracker is disabled for this BMS device.

## Example
Set charge current threshold of first BMS device to `50` ampere and disable solar energy tracker for the second BMS device:  
- with MQTT, send request to `<user-topic>/command`:  
`bms_jk?charge_current_threshold:0:50`  
`bms_jk?charge_current_threshold:1:0`  
`bms_jk?status`  

- with HTTP, send this request to a device with IP address `192.168.0.1` and port `8080`  
`curl http://192.168.0.1:8080/bms_jk?charge_current_threshold:0:50`  
`curl http://192.168.0.1:8080/bms_jk?charge_current_threshold:1:0`  
`curl http://192.168.0.1:8080/bms_jk/status`

## API
```
int bms_jk_is_battery_full(uint32_t bms_id);
int bms_jk_has_solar_excess(uint32_t bms_id);
```