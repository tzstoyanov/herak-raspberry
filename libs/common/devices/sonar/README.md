# AJ-SR04M sonar sensor

Reads [AJ-SR04M sonar sensor](../../../../docs/AJ-SR04M-sonar.pdf).

## Configuration
Configuration parameters in params.txt file:
```
SONAR_CONFIG   <echo pin>;<trigger pin>;...
```
Where `<echo pin>` is the GPIO where the Echo pin of the sensor is attached, `trigger pin` is the GPIO where the Trigger pin of the sensor is attached.

Example configuration of five sensors:
```
SONAR_CONFIG   0;1
```

## Monitor
### MQTT
MQTT AJ-SR04M sensor is auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/sonar/sonar_sensor/status` - Status of the sonar sensor:  
&nbsp;&nbsp;&nbsp;&nbsp;`distance:<value>` - Measured distance, in cm.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/sonar/status`

