# A20 Ultrasonic Distance Sensor

Reads [A0221AT](../../../../docs/A02-Datasheet.pdf), UART controlled ultrasonic distance sensor.

## Configuration
### Compile time option
Use this flag in your CMake file to enable it:  
```
option(ADD_SONIC_A20 "A20 Ultrasonic Distance Sensor" ON)
```
### Configuration parameters in `params.txt` file:
```
SONIC_A20   <TX gpio pin>,<RX gpio pin>;...
```
- `SONIC_A20 <RX gpio pin>,<TX gpio pin>`, mandatory. The Raspberry pin where the sensor TX and RX pins are attached. Up to 6 sensors are supported. Each configured sensor has an ID, starting from 0.  

Example configuration of two sensors.
```
SONIC_A20   0,1;14,15
```
Sensor 0 <TX,RX> pins are attached to GPIO0,GPIO1.
Sensor 1 <TX,RX> pins are attached to GPI14,GPI15.

## Monitor
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<user-topic>/sonica20/Distance_<id>/status` - Status of the sensor with the given `id`:  
- `distance:<value>` - Distance, in cm.  
