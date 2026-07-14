# YF Liquid Flow sensor

Reads [Liquid Flow YF Sensor](../../../../docs/YF-Datasheet.pdf).

## Configuration
Configuration parameters in params.txt file:
```
FLOW_YF   <gpio pin>:<ppls>;<gpio pin>:<ppl>;...
FLOW_ACC_SEC
```
- `FLOW_YF`, mandatory. `<gpio pin>` is the Raspberry pin where the YF sensor is attached, `<ppl>` is pulses per second per litre/minute of flow for that sensor, in float. Up to 6 sensors are supported. Each configured sensor has an ID, starting from 0. 
- `FLOW_ACC_SEC`, optional - defines the period in seconds for accumulating the measured flow. At the end of that period the accumulated value is sent over MQTT and reset the value. If `FLOW_ACC_SEC` is not defined, or set to `0`, the measured flow is accumulated until a `flow_yf?reset` command is executed and the value is sent over MQTT on each change.

Example configuration of five sensors that accumulate and report the flow on every 5 minutes:
```
FLOW_YF   0:6.6;1:7.9;2:7.9;4:11.0;6:11.0
FLOW_ACC_SEC    300
```
Sensor 0 is attached to GPIO0, has 6.6 pps per litre/minute of flow.
Sensor 1 is attached to GPIO1, has 7.9 pps per litre/minute of flow.
Sensor 2 is attached to GPIO2, has 7.9 pps per litre/minute of flow.
Sensor 3 is attached to GPIO4, has 11.0 pps per litre/minute of flow.
Sensor 4 is attached to GPIO6, has 11.0 pps per litre/minute of flow.

## Monitor
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<topic>/flow_yf/Flow_<id>/status` - Status of the sensor with the given `id`:  
- `flow:<value>` - Current liquid flow, in L/min.  
- `total_flow:<value>` - Total liquid passed through the sensor during the last flow, in liters. Reset to 0 when new flow starts.  
- `last_flow:<value>` - Time when the last flow started.  
- `duration:<value>` - Duration of the last flow, in minutes.  
- `total:<value>` - Total liquid passed through the sensor since the boot or last reset.  
- `last_reset:<value>` - Time when the statistics of total accumulated flow was reset.  

## Commands
The commands can be executed using the [commands engine](../../services/commands/README.md).  
- `flow_yf?reset:<id>` - Reset the accumulated statistics. The `id` is optional, index of the sensor for reset. If the `id` is omitted the statistics for all sensors is cleared.  

## Example
Reset all statistics for sensor 0:
- with MQTT, send request to `<topic>/command`:  
  `flow_yf?reset:0`  
- with HTTP, send this request to a device with IP address `192.168.0.1` and port `8080`  
  `curl http://192.168.0.1:8080/flow_yf?reset:0`  

