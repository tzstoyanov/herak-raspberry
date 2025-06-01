# System commands
Set of useful commands. The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.

## Commands
- `reboot:<delay_ms>` - Reboot the device after `delay_ms` milliseconds.  
- `status` - Print the full status of the system, all services and attached devices.  
- `log_sys` - Print the status of the system - uptime, chip temperature, system resources and errors.  
- `ping` - Returns `pong` answer, used to check if the system is alive.  
- `periodic_log:<delay_ms>` - Prints the full status of the system periodically on each `delay_ms` milliseconds.  
- `log_on` - Redirect all logs on the current web session.  
- `log_off` - Stop redirecting logs to all of the opened web sessions.  
- `reset` - Reset the debug state of all services and devices, disable all previously configured debug switches.  
- `log:<emerg|alert|crit|err|warn|notice|info|debug>` - Set the severity level of the printed logs.  

Example command for rebooting the device after 20 seconds. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`
- Using HTTP: `curl http://192.168.1.1:8080/sys?reboot:20000`
- Using MQTT: send request to topic `test/dev/command` with content `sys?reboot:20000`.

## Configuration
Configuration parameters in the `params.txt` file:  
```
SYS_CMD_DEBUG   <value>
```
Where `value` is the default debug mask, set at boot time.  
Example configurations:
```
SYS_CMD_DEBUG	0x0
```
## API
```
int syscmd_log_send(char *logbuff);
void debug_log_forward(int client_idx);
```
