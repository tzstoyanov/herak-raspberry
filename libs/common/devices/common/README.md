# Device helper functions

## Analog sensors API

- Allocates new ADC `sensor`. The `pin` parameter in GPIO pin where the sensor is attached, must be one of the device ADC pins or `-1` for internal temperature sensor. The value is calculated with `a` and `b` coefficients using the formula `value = <a> + <b>*<adc input>`, The API returns a pointer to newly allocated sensor on success, or NULL on error. The returned pointer can be freed with `free()`.  
```
struct adc_sensor_t *adc_sensor_init(int pin, float a, float b)
```

- Measure the value of the analog `sensor`. Returns `true` if new value is measured, or `0` if the value is not changed.  
```
bool adc_sensor_measure(struct adc_sensor_t *sensor)
```

- Get the last measured value of the sensor, calculated using the coefficients, in the range `a : (a + b*4095)`.  
```
float adc_sensor_get_value(struct adc_sensor_t *sensor)
```

- Get the last measured voltage of the sensor, calculated using the coefficients, in the range `a : (a + b*3.3)V`.  
```
float adc_sensor_get_volt(struct adc_sensor_t *sensor)
```

- Get the last measured value of the sensor as percent within the ADC range, `0-100%`.  
```
int adc_sensor_get_percent(struct adc_sensor_t *sensor)
```