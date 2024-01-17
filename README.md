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
- HTTP Webhooks.
- Watchdog.
- External HD44780 LCD display.

### Sensors:
- Build-in temperature sensor.
- Sonar AJ-SR04M.
- One-wire temperature sensors (Dallas DS18S20).

### Devices:
- Voltronic VM III inverter, over USB using [MAX communication protocol](docs/MAX-Communication-Protocol.pdf).
- [DALY BMS](docs/Daly-Communications-Protocol-V1.2.pdf), over Bluetooth and [HLK-B40 serial interface](docs/HLK-B40.pdf).

## Applications:
Applications are built on top of common library, which provides basic functionality:
Internet over WiFi, Bluetooth, remote logging, time synchronization, MQTT client, Webhook client,
watchdog, encrypted key-value store.
All user specific parameters are defined in params.txt file which must be available at
build time in the application directory. The [params-example.txt](app/params_example.txt) file can
be used as template.

### Shaft
Monitor water level in an underground tank using AJ-SR04M sonar sensor and send the data to
a remote MQTT server.

### Solar
Monitor Voltronic VM III inverter over USB and DALY BMS over Bluetooth and send the data to
a remote MQTT server.

## Try it out

### Prerequisites
- Raspberry Pico W.
- Wiring, depending on the use case.

### Get the code
The project uses sub-modules, so clone the repo with all sub-modules:
```
git clone --recurse-submodules https://github.com/tzstoyanov/herak-raspberry
```

### Build
- Copy [params-example.txt](app/params_example.txt) file as params.txt in the application directory
and modify it with your configuration.
- In the `build/<application>` directory, run `cmake ../../app/<applicattion>`
- In the `build/<application>` directory, run `make`

### Installation
- Attach to your Pico W over USB and start it in the bootloader mode (hold down the BOOTSEL button).
- Copy the generated image `build/<application>/<app-name>.uf2` to your Pico W.

## Documentation
- [MAX communication protocol](docs/MAX-Communication-Protocol.pdf).
- [DALY BMS protocol](docs/Daly-Communications-Protocol-V1.2.pdf)
- [HLK-B40 serial interface](docs/HLK-B40.pdf)

## License
herak-raspberry is available under the [GPLv2.0 or later license](LICENSE).
