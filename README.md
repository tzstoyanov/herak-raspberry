# herak-raspberry

## Overview
Raspberry Pico applications for monitoring and controlling devices in my house. The code is written
using Raspberry C SDK and runs directly on Raspberry Pico W without an Operating System.

### Features:
- Internet connection, using the build-in WiFi module and the lwIP stack.
- Bluetooth connection to devices, using the build-in Bluetooth module.
- USB in host mode.
- NTP client for time synchronization.
- Logging to a remote rsyslog server.
- MQTT client.
- HTTP server.
- HTTP webhook client.
- Watchdog.
- External HD44780 LCD display.

### Sensors:
- Build-in temperature sensor.
- Sonar AJ-SR04M.
- One-wire temperature sensors (Dallas DS18S20).
- [Soil moisture sensor](docs/Soil_moisture_sensor_module_EN.pdf) with analog and digital inputs.

### Devices:
- Voltronic VM III inverter, over USB using [MAX communication protocol](docs/MAX-Communication-Protocol.pdf).
- [DALY BMS](docs/Daly-Communications-Protocol-V1.2.pdf), over Bluetooth and [HLK-B40 serial interface](docs/HLK-B40.pdf).
- [Solid State Relays](docs/SSR_8Channel_EN.pdf).
- [OpenTherm](docs/Opentherm-Protocol-v2-2.pdf) device in master mode.

## Applications:
Applications are built on top of common library, which provides basic functionality:
Internet over WiFi, Bluetooth, remote logging, time synchronization, MQTT client, HTTP client
and server, watchdog, USB in host mode, encrypted key-value store.
All user specific parameters are defined in params.txt file which must be available at
build time in the application directory. The [params-example.txt](app/params_example.txt) file can
be used as a template.

### Shaft
Monitors water level in an underground tank using AJ-SR04M sonar sensor and
sends the data to a remote MQTT server.

### Solar
Monitors Voltronic VM III inverter over USB and DALY BMS over Bluetooth and
sends the data to a remote MQTT server.

### Irrigation
Reads soil moisture sensor and controls set of SSRs using web commands and
sends the data to a remote MQTT server.

### Boiler
Monitors and controls OpenTherm boiler. Sends the data to a remote MQTT
server and executes web control commands. The PICO device has to be
connected to the OT pins of the boiler using
an [OpenTherm Adapter](https://ihormelnyk.com/opentherm_adapter).

## Try it out

### Prerequisites
- Raspberry Pico W.
- Wiring, depending on the use case.

### Get the code
The project uses sub-modules, so clone the repo with all sub-modules:
```
git clone --recurse-submodules https://github.com/tzstoyanov/herak-raspberry
```
Apply all mandatory patches, which are not yet released upstream. Run in the top directory:
```
./scripts/apply_patches.sh 
```

### Build
- Copy [params-example.txt](app/params_example.txt) file as `params.txt` in the application directory
and modify it with your configuration.
- In the `build/<application>` directory, run `cmake ../../app/<applicattion>`
- In the `build/<application>` directory, run `make`

### Installation
#### Manually
- Attach to your Pico W over USB and start it in the bootloader mode (hold down the BOOTSEL button).
- Copy the generated image `build/<application>/<app-name>.uf2` to your Pico W.
#### Using the helper script
The `flash_pico.sh` script uses [picotool](https://github.com/raspberrypi/picotool) to copy image to the device.
If that tool is available on your system, the script can be used to copy the generated image. Attach the device
and run the script:
```
./scripts/flash_pico.sh build/<application>/<app_name>.uf2
```
It automatically reboots the device in bootloader mode and copies the new image.

## Documentation
- [MAX communication protocol](docs/MAX-Communication-Protocol.pdf).
- [DALY BMS protocol](docs/Daly-Communications-Protocol-V1.2.pdf).
- [HLK-B40 serial interface](docs/HLK-B40.pdf).
- [OpenTherm](docs/Opentherm-Protocol-v2-2.pdf).

## License
herak-raspberry is available under the [GPLv2.0 or later license](LICENSE).
