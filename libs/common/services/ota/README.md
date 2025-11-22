# OTA updates
Over The Air updates of the firmware, using tftp client to download the new image. When `OTA` is enabled, the 
[fota](https://github.com/JZimnol/pico_fota_bootloader) bootloader is used to boot the device. The `pico_fota_bootloader.uf2` file is generated during the compilation, and on first install must be copied on the device using the `BOOTSEL` button method. When `pico_fota_bootloader` boots, the application image `<device name>.uf2` can be copied using the same `BOOTSEL` button method. Two additional files are generated during the compilation, that can be used for OTA updates:  
- `<device name>_fota_image.bin` - unencrypted application image, can be used only if AES encryption is not enabled.
- `<device name>_fota_image_encrypted.bin` - encrypted application image, can be used only if AES encryption is enabled.

## Configuration
In application CMakeList.txt file:  
`set(BOOT_AES_KEY "secret key")`: If defined, the generated image will be AES encrypted with that key. The `pico_fota_bootloader` must be generated using the same key. When AES encryption is used, if there is mismatch in keys between the bootloader and the application, the new image will not be applied. That check is only for OTA updates, the `BOOTSEL` button method still can be used to copy any image. The `secret key` must be 32 bytes long and contain only characters from the set [0-9a-zA-Z].  

## Commands
- `update:tftp://<server>[:<port>]/<file>` - Download new `file` image from tftp `server` running on `port`. If `port` is omitted, the default tftp port is used. The `server` can be domain name or IP address. The image is validated and if valid the device is rebooted with the new image.
- `cancel` - cancel update in progress.

Example command for OTA update. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`. The tftp server is running on `192.168.1.10` / `example.com`, using port `6969`. The new image file is `test_fota_image_encrypted.bin`:  
- Using HTTP: `curl http://192.168.1.1:8080/ota?update:tftp://192.168.1.10:6969/test_fota_image_encrypted.bin`  
- Using HTTP: `curl http://192.168.1.1:8080/ota?update:tftp://example.com:6969/test_fota_image_encrypted.bin`  
- Using MQTT: send request to topic `test/dev/command` with content  
`ota?update:tftp://192.168.1.10:6969/test_fota_image_encrypted.bin`.  



