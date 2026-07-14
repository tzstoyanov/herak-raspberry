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
The status of these sensors is reported over [MQTT](../../services/mqtt/README.md):  
`<user-topic>/temperature/temperature_chip_0/status` - Status of all sensors:  
- `temperature_<type>_<id>:<val>` - The current temperature measurement `val`, where `type` is type of sensor: `chip` or `ntc` and `id` is an identifier of the sensor.  

## API
```
enum temp_sensor_type {
	TEMPERATURE_TYPE_INTERNAL,
	TEMPERATURE_TYPE_NTC,
};

float temperature_internal_get(void);
int temperature_get_count(enum temp_sensor_type type, uint8_t *count);
int temperature_get_data(enum temp_sensor_type type, int id, float *temperature);
```
