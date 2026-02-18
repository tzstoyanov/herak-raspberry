# JK BMS
Support for [JK BMS](../../../../docs/jk-bms-manual-1520084771.pdf) over bluetooth and [HLK-B40](../../../../docs/HLK-B40.pdf) serial interface.

## Configuration
Configuration parameters in params.txt file:
```
BMS_BT          <XX:XX:XX:XX:XX:XX>,<pin>;<XX:XX:XX:XX:XX:XX>,<pin> ...
BMS_MODEL       JK;JK ...
BMS_TIMEOUT_SEC <seconds>;<seconds>...
BMS_CELL_LEVELS <low>,<high>;<low>,<high>...
BMS_BATT_SWITCH <ssrID>-<ssr state on normal battery>;<ssrID>-<ssr state on normal battery>...
BMS_NAME        <name1>;<name2>...
BMS_NOTIFY      <0/1>
```
Where `<XX:XX:XX:XX:XX:XX>` is the bluetooth address of the BMS, `<pin>` is the pin code used for authorization. Up to 4 devices are supported, separated by `;`. All parameters follow the same list logic - configuration per BMS, separated by `;` and the order corresponds to the BMSs in the `BMS_BT` list. The `BMS_MODEL` parameter must be set to `JK`. The `BMS_TIMEOUT_SEC` parameter is optional. If set, the raspberry pico will be rebooted if there is no valid response from a connected BMS device since `<seconds>`. The optional `BMS_CELL_LEVELS` parameter is used to track if the battery level is low. If any cell voltage is below the configured `<low>` threshold, the battery state is considered `low`. If the voltages of all cells are above the configured `<high>` threshold, the battery state is considered `normal`. The optional `BMS_BATT_SWITCH` parameter is used to switch attached SSR depending on the battery state. The `<ssrID>` is the ID af an attached external [SSR](../ssr/README.md). The `<ssr state on normal battery>` is the desired state of that SSR when the battery level is normal.  The `BMS_NOTIFY` parameter controls whether to send webhook notifications when the battery state changes, if the logic for battery level id enabled with the `BMS_CELL_LEVELS` parameter. The optional `BMS_NAME` parameter sets a user friendly name of the battery, connected to that BMS. The name is used in logs, notifications and name prefix of the auto scripts. If not set, the ID of the battery is used.  
Example configuration:
```
BMS_BT                  11:2A:33:2B:3C:44,1234;22:3A:44:3B:4C:55,0000;
BMS_MODEL               JK;JK
BMS_TIMEOUT_SEC         1200;600
BMS_CELL_LEVELS         3.0,3.1;2.8,2.9
BMS_BATT_SWITCH         0-1;2-1
BMS_NAME                MainBattery
BMS_NOTIFY              1
```

## Auto Scripts
A set of scripts can be executed when the battery changes its state from `low` to `normal` and vice versa. In order to work, the `BMS_CELL_LEVELS` must be set - defining what levels are considered `low` and `normal`. The scripts names must be prefixed with `BMS_NAME`, or if this parameter is not set - the ID of the battery, as it is reported by the JK BMS. Scripts are detected only at boot time, so when a new script is copied the device must be rebooted.
- When the battery voltage falls below the configured `low` threshold, all scripts prefixed with `<BMS_NAME>_low_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_normal_` are disabled.
- When the battery voltage raises above the configured `high` threshold, all scripts prefixed with `<BMS_NAME>_normal_` are executed. If a cron schedule is configured for a given script, it is enabled. All scripts prefixed with `<BMS_NAME>_low_` are disabled.

## Monitor
### MQTT
MQTT BMS sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/bms_jk<id>/cell_0_v/status` - Voltage of all cells:  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_<id>_v:<value>>` - Voltage of cell with `<id>`, V.  

`<user-topic>/bms_jk<id>/cell_0_r/status` - Resistance of all cells:  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_<id>_r:<value>` - Resistance of cell with `<id>`, Ohms.    

`<user-topic>/bms_jk<id>/v_avg/status` - Various BMS sensors:  
&nbsp;&nbsp;&nbsp;&nbsp;`v_avg:<value>` - Average voltage of the battery, V.   
&nbsp;&nbsp;&nbsp;&nbsp;`v_delta:<value>` - Voltage difference between cells with minimum and maximum voltage, V.  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_v_min:<value>` - Index of the cell with lowest voltage.  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_v_max:<value>` - Index of the cell with highest voltage.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_action:<value>` - Battery action: 0 - Off, 1 - Charging, 2 - Discharging.  
&nbsp;&nbsp;&nbsp;&nbsp;`power_temp:<value>` - Power tube temperature, °C.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_temp1:<value>` - Temperature sensor 1, °C.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_temp2:<value>` - Temperature sensor 2, °C.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_temp_mos:<value>` - MOS Temperature, °C.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_volt:<value>` - Battery voltage, V.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_power:<value>` - Battery power, W.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_state:<value>` - Battery charge state, %.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_cycles:<value>` - Battery charging cycles.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_charge_curr:<value>` - Battery charge / discharge current, A.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_balance_curr:<value>` - Battery balance current, A.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_cap_rem:<value>` - Battery capacity remaining, Ah.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_cycles_cap:<value>` - Battery charging cycle capacity, Ah.  
&nbsp;&nbsp;&nbsp;&nbsp;`soh:<value>` - State Of Health, %.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_heat_a:<value>` - Heating current, A.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_low:<value>` - Calculated battery state: 1 - low, 0 - normal.

`<user-topic>/bms_jk<id>/Vendor/status` - BMS information:  
&nbsp;&nbsp;&nbsp;&nbsp;`Vendor:<string>` - BMS vendor.  
&nbsp;&nbsp;&nbsp;&nbsp;`Model:<string>` - BMS model.  
&nbsp;&nbsp;&nbsp;&nbsp;`Hardware:<string>` - Hardware revision.  
&nbsp;&nbsp;&nbsp;&nbsp;`Software:<string>` - Software revision.  
&nbsp;&nbsp;&nbsp;&nbsp;`SerialN:<string>` - Serial number.  
&nbsp;&nbsp;&nbsp;&nbsp;`Uptime:<string>` - Uptime.  
&nbsp;&nbsp;&nbsp;&nbsp;`PowerOnCount:<value>` - Power On Count.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/bms_jk/status`

