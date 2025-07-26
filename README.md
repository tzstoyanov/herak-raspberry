# herak-raspberry

## Overview
Raspberry Pico applications for monitoring and controlling devices in my house. The code is written
using Raspberry C SDK and runs directly on Raspberry Pico W without an operating system.  
Support for all services, devices and sensors is implemented as modules. Modules can be easily selected and combined together into a single application, depending on the use cases and available hardware resources.

### Services:
- [Internet](libs/common/services/wifi/README.md)
- [File System](libs/common/services/fs/README.md)
- [NTP client](libs/common/services/ntp/README.md)
- [Bluetooth](libs/common/services/bt/README.md)
- [USB Host](libs/common/services/usb/README.md)
- [Logs](libs/common/services/log/README.md)
- [MQTT client](libs/common/services/mqtt/README.md)
- [HTTP server](libs/common/services/webserver/README.md)
- [TFTP Server](libs/common/services/tftp_srv/README.md)
- [Webhooks](libs/common/services/webhook/README.md)
- [Commands engine](libs/common/services/commands/README.md)
- [System Commands](libs/common/services/syscmd/README.md)
- [Persistent user configuration](libs/common/services/cfg_store/README.md)
- [Scripts](libs/common/services/scripts/README.md)
- Watchdog

### Devices and Sensors:
- [Analog Temperature Sensor](libs/common/devices/temperature/README.md)
- [HD44780](libs/common/devices/lcd/README.md) LCD display
- [JK BMS](libs/common/devices/bms_jk/README.md)
- [OpenTherm](libs/common/devices/opentherm/README.md)
- [Solid State Relays](libs/common/devices/ssr/README.md)
- [Soil Moisture Sensor](libs/common/devices/soil/README.md)
- [SHT20 temperature and humidity sensor](libs/common/devices/sht20/README.md)
- [AJ-SR04M sonar sensor](libs/common/devices/sonar/README.md)
- [One-Wire sensor](libs/common/devices/one_wire/README.md)
- [YF Liquid Flow sensor](libs/common/devices/flow_yf/README.md)
- [DALY BMS](docs/Daly-Communications-Protocol-V1.2.pdf), over Bluetooth and [HLK-B40 serial interface](docs/HLK-B40.pdf)
- Voltronic VM III inverter, over USB using [MAX communication protocol](docs/MAX-Communication-Protocol.pdf)
- Build-in temperature sensor

## Applications:
Applications are built on top of common library, that provides support for services, devices, sensors and
encrypted key-value store. All user specific parameters are defined in `params.txt` file that must
be available at build time in the application directory. The [params-example.txt](app/params_example.txt)
file can be used as a template.

### [Common](app/common/main.c)
Minimal application - only the system main loop. Uses the system modules defined in `params.txt` file.

## Try it out

### Prerequisites
- Raspberry Pico W.
- Wiring, depending on the use case.

### Get the code
The project uses sub-modules, so clone the repo with all sub-modules:
```
git clone --recurse-submodules  https://github.com/tzstoyanov/herak-raspberry.git
```
Apply all mandatory patches, which are not yet released upstream. Run in the top directory:
```
./scripts/apply_patches.sh 
```

### Build
- Copy [params-example.txt](app/params_example.txt) file as `params.txt` in the `app/common/` directory
and modify it with your configuration. 
- In the `app/common/CMakeLists.txt` file, modify the first lines with the configuration, specific to your application. Name of the project, heap size, select the modules that will be compiled and linked to the project:
```
set(PROJECT_NAME herak-common)
set(HEAP_SIZE 32768)
set(DEBUG_BUILD false)

# Select the modules used in this application. ON / OFF
option(ADD_SSR "Solid State Relays" ON)		# libs/common/devices/ssr/README.md
option(ADD_SOIL "Soil sensor" ON)			# libs/common/devices/soil/README.md
option(ADD_SHT20 "SHT20 sensor" ON)			# libs/common/devices/sht20/README.md
option(ADD_OPENTHERM "OneTherm device" ON)	# libs/common/devices/opentherm/README.md
...

```
- In the `build/common` directory, run `cmake ../../app/common`
- In the `build/common` directory, run `make`

### Installation
#### Manually
- Attach to your Pico W over USB and start it in the bootloader mode (hold down the BOOTSEL button).
- Copy the generated image `build/common/<application_name>.uf2` to your Pico W.
#### Using the helper script
The `flash_pico.sh` script uses [picotool](https://github.com/raspberrypi/picotool) to copy image to the device.
If that tool is available on your system, the script can be used to copy the generated image. Attach the device
and run the script:
```
./scripts/flash_pico.sh build/common/<application_name>.uf2
```
It automatically reboots the device in bootloader mode and copies the new image.

## License
herak-raspberry is available under the [GPLv2.0 or later license](LICENSE).
