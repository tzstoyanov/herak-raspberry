# Analog pressure sensor

Reads pressure sensor, attached directly to ADC. The sensor output must be between 0v and 3.3V. Tested with YD4080.

## Configuration
Configuration parameters in params.txt file:
```
APRESS_PIN   <gpio pin>;<gpio pin>;...
APRESS_CORR  <a>:<b>;<a>:<b>;...
```
Where `APRESS_PIN <gpio pin>` is the Raspberry PIN where the output of the sensor is attached, one of the Raspberry ADC pins. The `APRESS_CORR <a>:<b>` are coefficients in float, used to calculate pressure from the ADC measurement using the formula `pressure = <a> + <b>*<adc input>`. The list with coefficients must correspond to the sensors from the `APRESS_PIN` list. Up to 3 sensors are supported, starting from `id` 0.
Example configuration of two sensors. First one is attached to GPIO26 and the pressure is calculated with `0 + 1.5*<adc>`. The second one is attached to GPIO28 and the pressure is calculated with `1.1 + 2.5*<adc>`.
```
APRESS_PIN  26;28
APRESS_CORR 0:1.5;1.1:2.5
```
## Monitor
### MQTT
MQTT analog pressure sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/apress/Sensor_<id>/status` - Status of the sensor with the given `id`:  
&nbsp;&nbsp;&nbsp;&nbsp;`pressure:<value>` - Pressure, in bars.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/apress/status`

