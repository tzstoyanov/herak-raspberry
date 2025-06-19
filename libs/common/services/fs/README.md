# File system
A small fail-safe filesystem based on [littlefs](https://github.com/littlefs-project/littlefs).

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `ls:[<path>]` - List a directory content.  
- `cat:<path>`  - Display content of a file.  
- `format`      - Format the file system.  
- `status`      - Current status of the file system.  
- `rm:<path>`   - Delete file or directory (the directory must be empty).  

Example command for listing the content of the top directory. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`
- Using HTTP: `curl http://192.168.1.1:8080/fs?ls:/`
- Using MQTT: send request to topic `test/dev/command` with content `fs?ls:/`.

## API
```
bool fs_is_mounted(void);
```

## Credits
- [littlefs](https://github.com/littlefs-project/littlefs)
- [pico-littlefs](https://github.com/lurk101/littlefs-lib)
