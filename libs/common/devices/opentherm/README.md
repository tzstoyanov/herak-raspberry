# OpenTherm device

Controls heating device attached to Raspberry using [OpenTherm](../../../../docs/Opentherm-Protocol-v2-2.pdf) protocol.
The device must be connected using hardware interface board, as the electrical signals are not directly compatible. Tested with that [OpenTherm Adapter](https://ihormelnyk.com/opentherm_adapter).

## Configuration
Configuration in the params.txt file:
```
OPENTHERM_PINS     <RX gpio pin>;<TX gpio pin>
OPENTHERM_Q         <Qmin>;<Qmax>
```
Where `<RX gpio pin>` and `<TX gpio pin>` are the Raspberry PINs where OpenTherm RX and TX are attached.
`Qmin` and `Qmax` is the minimum and maximum gas consumption, in l/h float number. The `OPENTHERM_Q` parameter is optional, used to calculate gas consumption based on relative modulation levels. If not set, gas consumption will not be calculated.  
For LPG gas, use that formula to convert from kg/h to l/h:  
`<l/h> = <kg/h> / 0.514`

Example configuration OpenTherm device, attached to GPIO 15 and 14 pins with gas consumption between 0.47kg/h and
2.24kg/h:
```
OPENTHERM_PINS     15;14
OPENTHERM_Q        0.914396887;4.357976654
```

## Monitor
### MQTT
MQTT sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The MQTT server connection details are also set in `params.txt`.  
`<user-topic>/opentherm/CH_set/status` - Status of the Open Therm device:  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_set":<0/1>`        - The temperature set-point of Central Heating.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_set":<0/1>`       - The temperature set-point of Domestic How Water.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch":<0/1>`            - If the Central Heating is currently running.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw":<0/1>`           - If the Domestic How Water is currently running.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_enabled":<0/1>`    - If the Central Heating is enabled.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_enabled":<0/1>`   - If the Domestic How Water is enabled.  
&nbsp;&nbsp;&nbsp;&nbsp;`"flame":<0/1>`         - If the Burner is currently running.  
&nbsp;&nbsp;&nbsp;&nbsp;`"flow_temp":<0..100>`  - Flow water temperature.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ret_temp":<0..100>`   - Return water temperature.  
&nbsp;&nbsp;&nbsp;&nbsp;`"exh_temp":<0..100>`   - Exhaust temperature.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_temp":<0..100>`   - Domestic hot water temperature.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_press":<0..5>`     - Water pressure of the Central Heating circuit.  
&nbsp;&nbsp;&nbsp;&nbsp;`"mdl_level":<0..100>`  - Percent modulation between min and max modulation levels.  
&nbsp;&nbsp;&nbsp;&nbsp;`"gas_flow":<float>`    - Current gas consumption, in L/h.  
&nbsp;&nbsp;&nbsp;&nbsp;`"gas_total":<float>`   - Accumulated gas consumption for the last 5 minutes, in L.  
&nbsp;&nbsp;&nbsp;&nbsp;`"flame_ua":<0..100>`   - Flame power.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_max":<0..127>`     - Upper bound for adjustment of max Central Heating set-point.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_min":<0..127>`     - Lower bound for adjustment of max Central Heating set-point.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_max":<0..127>`    - Upper bound for adjustment of Domestic Hot Water set-point.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_min":<0..127>`    - Lower bound for adjustment of Domestic Hot Water set-point.  
`<user-topic>/opentherm/Diagnostic/status` - Diagnostic and faults of the Open Therm device:  
&nbsp;&nbsp;&nbsp;&nbsp;`"diag":<0/1>`                  - Diagnostic event.  
&nbsp;&nbsp;&nbsp;&nbsp;`"service":<0/1>`               - Service request.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault":<0/1>`                 - Fault indication.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_lwp":<0/1>`             - Low water pressure.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_fl":<0/1>`              - Flame fault.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_lap":<0/1>`             - Low air pressure.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_hwt":<0/1>`             - High water temperature.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_code":<0..255>`         - OEM-specific fault code.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_burn_start":<0..65535>` - Number of unsuccessful burner starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`"fault_low_flame":<0..65535>`  - Number of times the flame signal was too low.  
`<user-topic>/opentherm/Stat_Reset_Time/status` - Statistics of the Open Therm device:  
&nbsp;&nbsp;&nbsp;&nbsp;`"stat_reset_time":"<date>"`        - Time since the statistics were retested.  
&nbsp;&nbsp;&nbsp;&nbsp;`"burner_starts":<0..65535>`        - Number of burner starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_pump_starts":<0..65535>`       - Number of Central Heating pump starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_pump_starts":<0..65535>`      - Number of Domestic Hot Water pump starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_burner_starts":<0..65535>`    - Number of Domestic Hot Water burner starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`"burner_hours":<0..65535>`         - Number of hours that burner is in operation.  
&nbsp;&nbsp;&nbsp;&nbsp;`"ch_pump_hours":<0..65535>`        - Number of hours that Central Heating pump is in operation.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_pump_hours":<0..65535>`       - Number of hours that Domestic Hot Water pump is in operation.  
&nbsp;&nbsp;&nbsp;&nbsp;`"dhw_burner_hours":<0..65535>`     - Number of hours that Domestic Hot Water burner is in operation.  

### WEB
The status of the Open Therm device is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
`http://<ip>:<port>/opentherm/status`  

## Control
### MQTT
The parameters of the Open Therm device can be configured with MQTT commands, send to this topic, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`:  
&nbsp;&nbsp;&nbsp;&nbsp;`<user-topic>/command`   - Set the state of the Open Therm device, request body:  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?dhw:<0/1>`         - Enable / Disable Domestic Hot Water.  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?ch:<0/1>`          - Enable / Disable Central Heating.  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?dhw_temp:<0..100>` - Temperature of Disable Domestic Hot Water.  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?ch_temp:<0..100>`  - Temperature of Central Heating.  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?stat_reset`        - Reset statistics.  
### WEB
The state of the Open Therm device can be set with this http request. The request parameters are the same as in the MQTT request body:  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?dhw:<0/1>`  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?dhw:<0/1>`  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?ch:<0/1>`  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?dhw_temp:<0..100>`  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?ch_temp:<0..100>`  
&nbsp;&nbsp;&nbsp;&nbsp;`http://<ip>:<port>/opentherm?stat_reset`  

## Example
Set the temperature to 45°C and turn `on` the Central Heating:  
-  with MQTT, send requests to `<user-topic>/command`:  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?ch_temp:60`  
&nbsp;&nbsp;&nbsp;&nbsp;`opentherm?ch:1`  
- with HTTP, send these request to a device with IP address `192.168.0.1` and port `8080`:  
&nbsp;&nbsp;&nbsp;&nbsp;`curl http://192.168.0.1:8080/opentherm?ch_temp:60`  
&nbsp;&nbsp;&nbsp;&nbsp;`curl http://192.168.0.1:8080/opentherm?ch:1`  

## Credits
[https://ihormelnyk.com/opentherm_adapter](https://ihormelnyk.com/opentherm_adapter)  
[https://github.com/adq/picotherm](https://github.com/adq/picotherm)
