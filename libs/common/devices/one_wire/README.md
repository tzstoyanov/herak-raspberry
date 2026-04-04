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

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `map_save` - Store the current mapping of discovered sensors to allocated indexes. This configuration ensures that on every boot, the sensors will have the same indexes.  
- `map_clear` - Deleted saved mapping of discovered sensors to allocated indexes.
- `map_show` - Show saved mapping of discovered sensors to allocated indexes.

Example command for displaying the stored sensors mapping. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`  
- Using HTTP: `curl http://192.168.1.1:8080/one_wire?map_show`  
- Using MQTT: send request to topic `test/dev/command` with content `one_wire?map_show`.  

## API
```
int one_wire_get_lines(uint8_t *count);
int one_wire_get_sensors_on_line(uint8_t line_id, uint8_t *count);
int one_wire_get_sensor_address(uint8_t line_id, int sensor_id, uint64_t *address);
int one_wire_get_sensor_data(uint8_t line_id, int sensor_id, float *temperature);
```
