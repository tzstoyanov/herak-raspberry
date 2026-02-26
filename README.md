# herak-raspberry

## Overview
Raspberry Pico applications for monitoring and controlling devices in my house. The code is written
using Raspberry C SDK and runs directly on Raspberry PicoW and Pico2W without an operating system.  
Support for all services, devices and sensors is implemented as modules. Modules can be easily selected and combined together into a single application, depending on the use cases and available hardware resources.  

### Services:
- [Network](libs/common/services/wifi/README.md)
- [File System](libs/common/services/fs/README.md)
- [NTP client](libs/common/services/ntp/README.md)
- [Bluetooth](libs/common/services/bt/README.md)
- [USB Host](libs/common/services/usb/README.md)
- [Logs](libs/common/services/log/README.md)
- [MQTT client](libs/common/services/mqtt/README.md)
- [HTTP server](libs/common/services/webserver/README.md)
- [TFTP Client](libs/common/services/tftp_client/README.md)
- [Webhooks](libs/common/services/webhook/README.md)
- [Commands engine](libs/common/services/commands/README.md)
- [System Commands](libs/common/services/syscmd/README.md)
- [Persistent user configuration](libs/common/services/cfg_store/README.md)
- [Scripts](libs/common/services/scripts/README.md)
- [OTA Updates](libs/common/services/ota/README.md)
- [WoL sender](libs/common/services/wol/README.md)
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
- [Analog Pressure Sensor](libs/common/devices/pressure/README.md)
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
- Raspberry PicoW or Pico2W.
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
- In the `app/common/CMakeLists.txt` file, modify the first lines with the configuration, specific to your application. Name of the project, heap size, target board, AES key for image encryption, select the modules that will be compiled and linked to the project:
```
set(PROJECT_NAME herak-common)
set(HEAP_SIZE 32768)
set(DEBUG_BUILD false)
set(PICO_BOARD pico_w)
set(BOOT_AES_KEY "AES key")

# Select the modules used in this application. ON / OFF
option(ADD_SSR "Solid State Relays" ON)		# libs/common/devices/ssr/README.md
option(ADD_SOIL "Soil sensor" ON)			# libs/common/devices/soil/README.md
option(ADD_SHT20 "SHT20 sensor" ON)			# libs/common/devices/sht20/README.md
option(ADD_OPENTHERM "OneTherm device" ON)	# libs/common/devices/opentherm/README.md
...

```
- In the `build/common` directory, run `cmake ../../app/common`
- In the `build/common` directory, run `make`
 
### Generated files  
 - `boot loader image` - build/common/pico_fota_bootloader/pico_fota_bootloader.uf2  
 - `application image` - build/common/<application_name>.uf2  
 - `application meta file` - build/common/<application_name>.meta  
 - `OTA image` - build/common/<application_name>_fota_image.bin  
 - `OTA encrypted image` - build/common/<application_name>_fota_image_encrypted.bin  

### Installation
#### With OTA (default)
By default, the image is compiled with additional boot loader to support [OTA Updates](libs/common/services/ota/README.md).
 - On fresh device, first copy the `bootloader image` over USB.  
 - When the bootloader starts, copy the `application image` over USB.  
 - When the image runs, `OTA image` or `OTA encrypted image` can be used with [OTA commands](libs/common/services/ota/README.md) to update the device, using tftp.  

 Encryption can be controlled in application CMakafile.txt using the `BOOT_AES_KEY` variable. If it is defined, both the bootloader and the image will use that key. The bootloader validates only the images, encrypted with that key. The image is encrypted with the key at build time. Both the bootloader and the image must be compiled using the same key. That validation is performed only for OTA updates. The application images copied over USB are not encrypted and not validated.
 ```
 # Must be 32 bytes long and contain only characters from the set [0-9a-zA-Z].
set(BOOT_AES_KEY "AES key")
 ```

Max size of OTA images:
- On PicoW / RP2040 `940K`
- On Pico2W / RP2350 `1964K`

#### Without OTA
The OTA support can be disabled in application CMakefile.txt:
```
option(ADD_OTA "Bootloader and OTA updates" OFF)
```
In that case the bootloader is not compiled and only the `application image` is generated, that can be copied to device directly.

### Copying image files to flash over USB 
##### Manually
- Attach to the device over USB and start it in the bootloader mode (hold down the BOOTSEL button).  
- Pico will appear as storage device, just copy the image file to it. After the copy, the device will reboot automatically.  

##### Using the helper script
The `flash_pico.sh` script uses [picotool](https://github.com/raspberrypi/picotool) to copy image to the device.
If that tool is available on your system, the script can be used to copy the generated image. Attach the device
and run the script:
```
./scripts/flash_pico.sh <image file>
```
It automatically reboots the device in bootloader mode and copies the new image.

## License
herak-raspberry is available under the [GPLv2.0 or later license](LICENSE).
