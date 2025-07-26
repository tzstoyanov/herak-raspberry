# Analog Temperature Sensor

Analog sensors for temperature measurements. Currently supported:  
- Raspberry Pico internal chip sensor, enabled by default.  
- NTC sensors attached to ADC GPIO pins with 5K pull-up resistor.  

## Configuration
Configuration parameters in params.txt file:  
```
TEMPERATURE_NTC     <gpio>:<nominal>:<const>;  
```
Where `<gpio>` the GPIO pin where the NTC sensor is attached, must be one of the 3 Raspberry ADC GPIO pins - `26`, `27` or `28`. The `nominal` parameter is the NTC resistance at 25°C. The `const` parameter is the NTC Beta coefficient, used to calculate the temperature. Up to 3 external sensors are supported.  

Example configuration of two sensors, attached to GPIO 26 and 28 with resistance 50KΩ at 25°C and Beta coefficient 3950:  
```
TEMPERATURE_NTC     26:50000:3950;28:50000:3950  
```

## Monitor
### MQTT
MQTT temperature sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/temperature/temperature_chip_0/status` - Status of all sensors:  
&nbsp;&nbsp;&nbsp;&nbsp;`temperature_<type>_<id>:<val>` - The current temperature measurement `val`, where `type` is type of sensor: `chip` or `ntc` and `id` is an identifier of the sensor.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/temperature/status`  
