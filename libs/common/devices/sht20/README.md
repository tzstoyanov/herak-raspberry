# SHT20 temperature and humidity sensor

Reads [SHT20 Sensor](../../../../docs/Sensirion_Datasheet_Humidity_Sensor_SHT20.pdf).

## Configuration
Configuration parameters in params.txt file:
```
SHT20_SDA_PIN   <gpio pin>;<gpio pin>;...
```
Where `<gpio pin>` is the Raspberry PIN where the SDA of the sensor is attached. Up to 6 sensors are supported, attached to the Raspberry I2C outputs. The SCL of the sensor must be attached next to SDA: `<gpio pin>` + 1. Each configured sensor has an ID, starting from 0.

Example configuration of five sensors:
```
SHT20_SDA_PIN   0;2;12;18;26
```
Sensor 0 <SDA,SCL> pins are attached to GPIO0,GPIO1.
Sensor 1 <SDA,SCL> pins are attached to GPIO2,GPIO3.
Sensor 2 <SDA,SCL> pins are attached to GPIO12,GPIO13.
Sensor 3 <SDA,SCL> pins are attached to GPIO18,GPIO19.
Sensor 4 <SDA,SCL> pins are attached to GPIO26,GPIO27.

## Monitor
### MQTT
MQTT SHT20 sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/sht20/Temperature_<id>/status` - Status of the sensor with the given `id`:  
&nbsp;&nbsp;&nbsp;&nbsp;`temperature:<value>` - Temperature, in °C.  
&nbsp;&nbsp;&nbsp;&nbsp;`humidity:<value>` - Humidity, in %.  
&nbsp;&nbsp;&nbsp;&nbsp;`vpd:<value>` - Vapor Pressure Deficit, in kPa.
&nbsp;&nbsp;&nbsp;&nbsp;`dew_point:<value>` - Dew Point, in °C.

### HTTP
The status of all sensors is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/sht20/status`

