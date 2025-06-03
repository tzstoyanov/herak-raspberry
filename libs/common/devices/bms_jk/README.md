# JK BMS
Support for [JK BMS](../../../../docs/jk-bms-manual-1520084771.pdf) over bluetooth and [HLK-B40](../../../../docs/HLK-B40.pdf) serial interface.

## Configuration
Configuration parameters in params.txt file:
```
BMS_BT          <XX:XX:XX:XX:XX:XX>;<pin>
BMS_MODEL       JK
```
Where `<XX:XX:XX:XX:XX:XX>` is the bluetooth address of the BMS, `<pin>` is the pin code used for authorization.  
Example configuration:
```
BMS_BT                  11:2A:33:2B:3C:44;1234
BMS_MODEL               JK
```

## Monitor
### MQTT
MQTT BMS sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/bms_jk/cell_0_v/status` - Voltage of all cells:  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_<id>_v:<value>>` - Voltage of cell with `<id>`, V.  

`<user-topic>/bms_jk/cell_0_r/status` - Resistance of all cells:  
&nbsp;&nbsp;&nbsp;&nbsp;`cell_<id>_r:<value>` - Resistance of cell with `<id>`, Ohms.    

`<user-topic>/bms_jk/v_avg/status` - Various BMS sensors:  
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
&nbsp;&nbsp;&nbsp;&nbsp;`batt_balance_curr:<value>` - Battery ballance current, A.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_cap_rem:<value>` - Battery capacity remaining, Ah.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_cycles_cap:<value>` - Battery charging cycle capacity, Ah.  
&nbsp;&nbsp;&nbsp;&nbsp;`soh:<value>` - State Of Health, %.  
&nbsp;&nbsp;&nbsp;&nbsp;`batt_heat_a:<value>` - Heating current, A.  

`<user-topic>/bms_jk/Vendor/status` - BMS information:  
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

