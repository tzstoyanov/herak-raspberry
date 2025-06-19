# TFTP Server
A tiny wrapper around the [lwip tftp](https://www.nongnu.org/lwip/2_0_x/group__tftp.html) implementation. The server depends on the [file system](../fs/README.md) module - files are served from the local file system.  

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `close_all`   - Close all opened files.

Example command for closing all files currently opened by the server. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`
- Using HTTP: `curl http://192.168.1.1:8080/tftp?close_all`
- Using MQTT: send request to topic `test/dev/command` with content `tftp?close_all`.