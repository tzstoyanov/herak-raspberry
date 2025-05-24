# One-wire sensors

Support for reading one-wire devices such as temperature sensors Dallas DS18S20, [DS18B20](../../../../docs/OneWire-ds18b20.pdf), DS1822, Maxim MAX31820 and MAX31826.

## Configuration
Configuration parameters in params.txt file:
```
ONE_WIRE_DEVICES   <gpio pin>;<gpio pin>;...
```
Where `<gpio pin>` is the Raspberry pin where one-wire sensors are attached. Up to 10 pins can be configured, with up to 3 sensors attached to each pin.

Example configuration of 2 pins:
```
ONE_WIRE_DEVICES   2;8
```

## Monitor
### MQTT
MQTT one-wire sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/one_wire/Temperature_0x<address>/status` - Status of the sensor with the given `address`:  
&nbsp;&nbsp;&nbsp;&nbsp;`id:<value>` - Address of the sensor.  
&nbsp;&nbsp;&nbsp;&nbsp;`temperature:<value>` - Temperature, in °C.  

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/one_wire/status`

