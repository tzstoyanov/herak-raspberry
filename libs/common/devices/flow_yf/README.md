# YF Liquid Flow sensor

Reads [Liquid Flow YF Sensor](../../../../docs/YF-Datasheet.pdf).

## Configuration
Configuration parameters in params.txt file:
```
FLOW_YF   <gpio pin>:<pps>;<gpio pin>:<ppl>;...
```
Where `<gpio pin>` is the Raspberry PIN where the YF sensor is attached, `<ppl>` is pulses per second per litre/minute of flow for that sensor, in float. Up to 6 sensors are supported. Each configured sensor has an ID, starting from 0.

Example configuration of five sensors:
```
FLOW_YF   0:6.6;1:7.9;2:7.9;4:11.0;6:11.0
```
Sensor 0 is attached to GPIO0, has 6.6 pps per litre/minute of flow.
Sensor 1 is attached to GPIO1, has 7.9 pps per litre/minute of flow.
Sensor 2 is attached to GPIO2, has 7.9 pps per litre/minute of flow.
Sensor 3 is attached to GPIO4, has 11.0 pps per litre/minute of flow.
Sensor 4 is attached to GPIO6, has 11.0 pps per litre/minute of flow.

## Monitor
### MQTT
MQTT FLOW YF sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/flow_yf/Flow_<id>/status` - Status of the sensor with the given `id`:  
&nbsp;&nbsp;&nbsp;&nbsp;`flow:<value>` - Current liquid flow, in L/min.  
&nbsp;&nbsp;&nbsp;&nbsp;`total_flow:<value>` - Total liquid passed through the sensor during the last flow, in liters. Reset to 0 when new flow starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`last_flow:<value>` - Time when the last flow started.  
&nbsp;&nbsp;&nbsp;&nbsp;`duration:<value>` - Duration of the last flow, in minutes.  
&nbsp;&nbsp;&nbsp;&nbsp;`total:<value>` - Total liquid passed through the sensor since the boot or last reset.  
&nbsp;&nbsp;&nbsp;&nbsp;`last_reset:<value>` - Time when the statistics of total accumulated flow was reset.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/flow_yf/status`

## Control
### MQTT
The accumulated statistics can be cleared with MQTT commands, send to this topic, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`:  
`<user-topic>/command`:  
&nbsp;&nbsp;&nbsp;&nbsp;`flow_yf?reset:<id>` - Where `id` is optional, index of the sensor for reset. If the `id` is omitted the statistics for all sensors is cleared.
### HTTP
The accumulated statistics can be cleared with this http request. The request parameters are the same as in the MQTT request body:  
`curl http://<device_ip>:<port>/flow_yf?reset:<id>`
## Example
Reset all statistics for sensor 0:
- with MQTT, send request to `<user-topic>/command`:  
&nbsp;&nbsp;&nbsp;&nbsp;`flow_yf?reset:0`  
- with HTTP, send this request to a device with IP address `192.168.0.1` and port `8080`  
&nbsp;&nbsp;&nbsp;&nbsp;`curl http://192.168.0.1:8080/flow_yf?reset:0`  

