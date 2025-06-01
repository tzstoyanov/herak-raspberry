# Bluethoot

A tiny wrapper around the Raspberry Pico [bluetooth stack](https://www.raspberrypi.com/documentation/pico-sdk/networking.html#group_pico_btstack).

## API
```
int bt_add_known_device(bt_addr_t addr, char *pin, bt_event_handler_t cb, void *context);
int bt_service_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16);
int bt_characteristic_get_uuid(uint32_t id, bt_uuid128_t *u128, uint16_t *u16);
int bt_characteristic_read(uint32_t char_id);
int bt_characteristic_write(uint32_t char_id, uint8_t *data, uint16_t data_len);
int bt_characteristic_notify(uint32_t char_id, bool enable);
```

