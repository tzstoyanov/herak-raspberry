# Persistent configuration store
Using the flash file system to store user defined configuration.

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `set:<name>:<value>` - Set user parameter.  
- `del:<name>`  - Delete user parameter.  
- `reset`       - Reset to default all user configuration.  
- `list`        - List supported user configurable parameters.  
- `purge`       - Delete unknown user configuration.  

Example command for listing the content of the supported user configurable parameters. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`
- Using HTTP: `curl http://192.168.1.1:8080/config?list`  
- Using MQTT: send request to topic `test/dev/command` with content `config?list`.  

## API
```
char *cfgs_param_get(char *name);
```
