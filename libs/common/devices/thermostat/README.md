# Thermostat

Controls temperature within given range, using attached SSR and temperature sensor.

## Configuration
Configuration parameters in params.txt file:
```
THERMOSTAT      <ssr_gpio>:<t_source>-<t_id>;<ssr_gpio>:<t_source>-<t_id>;...
THERMOSTAT_DEF  <on/off>:<temperature>-<hysteresis>;<on/off>:<temperature>-<hysteresis>;...
```
The `THERMOSTAT` parameter defines the thermostats: `<ssr_gpio>` is the Raspberry GPIO PIN where the control SSR for this thermostat is attached. The [SSR module](../ssr/README.md) must be enabled and this SSR must be configured there. The `<t_source>-<t_id>` parameter is the control temperature. The `<t_source>` describes the type of the attached temperature sensor:
- `one_wire` - a [One Wire](../one_wrie/README.md) sensor. The `<t_id>` parameter is a hex number in format `0xGGII`, where `GG` is the index of the one wire line, where the sensor is attached; `II` is the index of the sensor on that line. The index of the line follows the order of which the one_wire lines are described in the `ONE_WIRE_DEVICES` parameter - the first described device is with index `0`. The index of the sensor on the line depends on the discovery order - the first discovered is with index `0`. The [OneWire](../one_wrie/README.md) `map_save` command can be used to pin the OneWire sensors to indexes, thus ensuring the indexes do not depend on discovering order.
- `sht20` - a [SHT20](../sht20/README.md) sensor. The `<t_id>` parameter is the index of the sensor. This follows the order of which the sensors are described in the `SHT20_SDA_PIN` parameter - the first described sensor is with index `0`.
- `ntc` - a [NTC](../temperature/README.md) sensor. The `<t_id>` parameter is the index of the sensor. This follows the order of which the sensors are described in the `TEMPERATURE_NTC` parameter - the first described sensor is with index `0`.

The `THERMOSTAT_DEF` parameter (optional) defines the initial default settings of the thermostats. Order of default values follows the order of which the thermostats are defined in the `THERMOSTAT` parameter. If `THERMOSTAT_DEF` is not defined, by default all thermostats are `off`.
- `<on/off>` - If the thermostat is enabled or disabled.
- `<temperature>` - the desired temperature, float, in *C. When this temperature is reached, the attached SSR is turned off.
- `<hysteresis>` - the hysteresis, float, in *C. When the `temperature - hysteresis` is reached, the attached SSR is turned on.

Example configuration:
- The first thermostat controls SRR attached to GP0 monitoring the temperature of OneWire sensor with index 0 attached to line 0. It is turned on by default. The SSR is turned OFF when the temperature reaches 20.5 *C and turned ON when the temperature is 17.3 *C.
- The second thermostat controls SRR attached to GP4 monitoring the temperature of SHT20 sensor with index 1. It is turned off by default.
- The third thermostat controls SRR attached to GP5 monitoring the temperature of NTC sensor with index 0. It is turned on by default. The SSR is turned OFF when the temperature reaches 15 *C and turned ON when the temperature is 14.5 *C.
```
THERMOSTAT  0:one_wire-0x0000;4:sht20-1;5:ntc-0
THERMOSTAT_DEF on:20.5:3.2;off:0:0;on:15:0.5
```
## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `on:<thermostat_id>` - Turn on the thermostat with given `thermostat_id`.
- `off:<thermostat_id>` - Turn off the thermostat with given `thermostat_id`.
- `temperature:<thermostat_id>:<temperature in *C>` - Set the desired temperature of the thermostat with given `thermostat_id`, float number in *C. When this temperature is reached, the attached SSR is turned off.
- `<hysteresis>:<thermostat_id>:<temperature in *C>` - Set the hysteresis of the thermostat with given `thermostat_id`, float number in *C. When the `temperature - hysteresis` is reached, the attached SSR is turned on.
- `on_all` - Turn on all thermostats.
- `off_all` - Turn off all thermostats.

Example command for setting the temperature to 23.4 *C of the thermostat with index 3. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`  
- Using HTTP: `curl http://192.168.1.1:8080/thermostat?temperature:3:23.4`  
- Using MQTT: send request to topic `test/dev/command` with content `thermostat?temperature:3:23.4`.  
