# Soil Moisture Sensor

Reads [Soil Moisture Sensor](../../../../docs/Soil_moisture_sensor_module_EN.pdf).

## Configuration
Configuration parameters in params.txt file:
```
SOIL_D		            <id>:<gpio pin>;<id>:<gpio pin>
SOIL_A		            <id>:<gpio pin>;<id>:<gpio pin>
```
Where `<id>` is an identifier of the sensor, `<gpio pin>` is the Raspberry PIN where this sensor is attached. The `SOIL_A` parameter is the analog output of the sensor, must be attached to one of the 3 Raspberry ADC GPIO pins - `26`, `27` or `28`. The `SOIL_D` parameter is the digital output of the sensor. Supported are up to `5` sensors, 3 of them can have analog outputs.

Example configuration of five sensors. The first three have analog and digital outputs - attached to GPIO10;GPIO26, GPI11;GPIO27 and GPI12;GPIO28. The last two have only digital outputs, attached to GPIO14 and GPIO15.
```
SOIL_D     0:10;1:11;2:12;3:13;4:14;5:15
SOIL_A     0:26;1:27;2:28
```

## Monitor
### MQTT
MQTT SSR sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/soil/Soil_<id>/status` - Status of the sensor with the given `id`:  
&nbsp;&nbsp;&nbsp;&nbsp;`id:<id>`       - Index of the sensor.  
&nbsp;&nbsp;&nbsp;&nbsp;`value_d:<0/1>` - The current digital state: 1 - `dry`, 0 - `wet`.  
&nbsp;&nbsp;&nbsp;&nbsp;`value_a:<val>` - The current analog state, or 0 if there is no analog output of this sensor.

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/soil/status`

## Notifications
If `WEBHOOK_SERVER` and `WEBHOOK_ENDPOINT` are configured in `params.txt`, web notifications are sent for every change on the digital measurement of a sensor.