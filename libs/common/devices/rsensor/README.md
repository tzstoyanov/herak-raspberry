# Remote Sensor

Reads data from remote sensors via [MQTT](../../services/mqtt/README.md).

## Configuration
Configuration parameters in params.txt file:
```
REMOTE_SENSOR         <sensor name>:<mqtt topic>:<value key>;<sensor name>:<mqtt topic>:<value key>;...
```
Where:
- `<sensor name>` is the name of the sensor, used in logs and to address the sensor.
- `<mqtt topic>` is the MQTT topic to subscribe to in order to receive sensor updates.
- `<value key>` the JSON key of the data into the MQTT payload, if the payload is in JSON format. This parameter is optional. If `<value key>` is not set the MQTT payload is not parsed as JSON, the entire payload is considered as sensor data.

Example configuration of three remote sensors:
 - `sensor1` transmits data on the MQTT topic `test/sensors/data` in JSON format, using the `temperature` key.
 - `sensor2` transmits data on the MQTT topic `test/health`, not using JSON.
 - `sensor3` transmits data on the MQTT topic `test/sensors/data` in JSON format, using the `pressure` key.
```
REMOTE_SENSOR     sensor1:test/sensors/data:temperature;sensor2:test/health;sensor3:test/sensors/data:pressure
```

## APIs
```
int rsensor_get_index(char *name);
int rsensor_get_value(int index, float *val);
```