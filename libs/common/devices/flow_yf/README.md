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
&nbsp;&nbsp;&nbsp;&nbsp;`total:<value>` - Total liquid passed through the sensor during the last flow, in liters. Reset to 0 when new flow starts.  
&nbsp;&nbsp;&nbsp;&nbsp;`last:<value>` - Time when the last flow started.  
&nbsp;&nbsp;&nbsp;&nbsp;`duration:<value>` - Duration of the last flow, in minutes.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/flow_yf/status`

