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
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<user-topic>/sonar/sonar_sensor/status` - Status of the sonar sensor:  
- `distance:<value>` - Measured distance, in cm.  
