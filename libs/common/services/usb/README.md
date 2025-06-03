# USB Host
A tiny wrapper around the [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) library.

## Configuration
Configuration parameters in `params.txt` file:  
```
USB_PORTS   <dp_pin>,<dm_pin>;<dp_pin>,<dm_pin>;...
```
Where `dp_pin` is GPIO pin where the USB D+ is attached, `dm_pin` is GPIO pin where D- is attached. Up to 4 USB ports are supported.  
Example configurations:
```
USB_PORTS	1,2;8,9
```

## API
```
int usb_send_to_device(int idx, char *buf, int len);
int usb_add_known_device(uint16_t vid, uint16_t pid, usb_event_handler_t cb, void *context);
```
