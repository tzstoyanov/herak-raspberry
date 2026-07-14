# OpenTherm device

Controls heating device attached to Raspberry using [OpenTherm](../../../../docs/Opentherm-Protocol-v2-2.pdf) protocol.
The device must be connected using hardware interface board, as the electrical signals are not directly compatible. Tested with that [OpenTherm Adapter](https://ihormelnyk.com/opentherm_adapter).

## Configuration
Configuration in the params.txt file:
```
OPENTHERM_PINS     <RX gpio pin>;<TX gpio pin>
OPENTHERM_Q         <Qmin>;<Qmax>
```
- `OPENTHERM_PINS`, mandatory. `<RX gpio pin>` and `<TX gpio pin>` are the Raspberry pins where OpenTherm RX and TX are attached.
- `OPENTHERM_Q`, optional.`Qmin` and `Qmax` is the minimum and maximum gas consumption, in l/h float number. The parameter is used to calculate gas consumption based on relative modulation levels. If not set, gas consumption will not be calculated.  
For LPG gas, use that formula to convert from kg/h to l/h:  
`<l/h> = <kg/h> / 0.514`

Example configuration OpenTherm device, attached to GPIO 15 and 14 pins with gas consumption between 0.47kg/h and
2.24kg/h:
```
OPENTHERM_PINS     15;14
OPENTHERM_Q        0.914396887;4.357976654
```

## Monitor
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<user-topic>/opentherm/CH_set/status` - Status of the Open Therm device:  
- `"ch_set":<0/1>`        - The temperature set-point of Central Heating.  
- `"dhw_set":<0/1>`       - The temperature set-point of Domestic How Water.  
- `"ch":<0/1>`            - If the Central Heating is currently running.  
- `"dhw":<0/1>`           - If the Domestic How Water is currently running.  
- `"ch_enabled":<0/1>`    - If the Central Heating is enabled.  
- `"dhw_enabled":<0/1>`   - If the Domestic How Water is enabled.  
- `"flame":<0/1>`         - If the Burner is currently running.  
- `"flow_temp":<0..100>`  - Flow water temperature.  
- `"ret_temp":<0..100>`   - Return water temperature.  
- `"exh_temp":<0..100>`   - Exhaust temperature.  
- `"dhw_temp":<0..100>`   - Domestic hot water temperature.  
- `"ch_press":<0..5>`     - Water pressure of the Central Heating circuit.  
- `"mdl_level":<0..100>`  - Percent modulation between min and max modulation levels.  
- `"gas_flow":<float>`    - Current gas consumption, in L/h.  
- `"gas_total":<float>`   - Accumulated gas consumption for the last 5 minutes, in L.  
- `"flame_ua":<0..100>`   - Flame power.  
- `"ch_max":<0..127>`     - Upper bound for adjustment of max Central Heating set-point.  
- `"ch_min":<0..127>`     - Lower bound for adjustment of max Central Heating set-point.  
- `"dhw_max":<0..127>`    - Upper bound for adjustment of Domestic Hot Water set-point.  
- `"dhw_min":<0..127>`    - Lower bound for adjustment of Domestic Hot Water set-point.  

`<user-topic>/opentherm/Diagnostic/status` - Diagnostic and faults of the Open Therm device:  
- `"diag":<0/1>`                  - Diagnostic event.  
- `"service":<0/1>`               - Service request.  
- `"fault":<0/1>`                 - Fault indication.  
- `"fault_lwp":<0/1>`             - Low water pressure.  
- `"fault_fl":<0/1>`              - Flame fault.  
- `"fault_lap":<0/1>`             - Low air pressure.  
- `"fault_hwt":<0/1>`             - High water temperature.  
- `"fault_code":<0..255>`         - OEM-specific fault code.  
- `"fault_burn_start":<0..65535>` - Number of unsuccessful burner starts.  
- `"fault_low_flame":<0..65535>`  - Number of times the flame signal was too low.  

`<user-topic>/opentherm/Stat_Reset_Time/status` - Statistics of the Open Therm device:  
- `"stat_reset_time":"<date>"`        - Time since the statistics were retested.  
- `"burner_starts":<0..65535>`        - Number of burner starts.  
- `"ch_pump_starts":<0..65535>`       - Number of Central Heating pump starts.  
- `"dhw_pump_starts":<0..65535>`      - Number of Domestic Hot Water pump starts.  
- `"dhw_burner_starts":<0..65535>`    - Number of Domestic Hot Water burner starts.  
- `"burner_hours":<0..65535>`         - Number of hours that burner is in operation.  
- `"ch_pump_hours":<0..65535>`        - Number of hours that Central Heating pump is in operation.  
- `"dhw_pump_hours":<0..65535>`       - Number of hours that Domestic Hot Water pump is in operation.  
- `"dhw_burner_hours":<0..65535>`     - Number of hours that Domestic Hot Water burner is in operation.  

## Commands
The commands can be executed using the [commands engine](../../services/commands/README.md).  
- `opentherm?dhw:<0/1>`         - Enable / Disable Domestic Hot Water.  
- `opentherm?ch:<0/1>`          - Enable / Disable Central Heating.  
- `opentherm?dhw_temp:<0..100>` - Temperature of Disable Domestic Hot Water.  
- `opentherm?ch_temp:<0..100>`  - Temperature of Central Heating.  
- `opentherm?stat_reset`        - Reset statistics.  

## Example
Set the temperature to 45°C and turn `on` the Central Heating:  
- MQTT, send requests to `<user-topic>/command`:  
  - `opentherm?ch_temp:60`  
  - `opentherm?ch:1`  
- HTTP, send these request to a device with IP address `192.168.0.1` and port `8080`:  
  - `curl http://192.168.0.1:8080/opentherm?ch_temp:60`  
  - `curl http://192.168.0.1:8080/opentherm?ch:1`  

## Credits
[https://ihormelnyk.com/opentherm_adapter](https://ihormelnyk.com/opentherm_adapter)  
[https://github.com/adq/picotherm](https://github.com/adq/picotherm)
