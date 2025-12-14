# Solid State Relays

Controls SSRs attached directly to the Raspberry device. Tested with [8 Channel Solid State Relay](../../../../docs/SSR_8Channel_EN.pdf).

## Configuration
Configuration parameters in params.txt file:
```
SSR             <id>:<gpio pin>;<id>:<gpio pin> ....
SSR_TRIGGER     <0/1>
```
Where `<id>` is an identifier of the relay, `<gpio pin>` is the Raspberry PIN where this relay is attached. Supported are up to `29` relays, attached to `GPIO 0 - 28`. The `SSR_TRIGGER` parameter defines the state of the PIN for triggering the relay `ON`.

Example configuration of 8 relays, attached to `GPIO0 - GPIO7`, triggered by state `1`:
```
SSR             0:0;1:1;2:2;3:3;4:4;5:5;6:6;7:7
SSR_TRIGGER     1
```

## Monitor
### MQTT
MQTT SSR sensors are auto-discovered by Home Assistant. The state is published using the following topics, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`. The connection details for the MQTT server are also set in `params.txt`.  
`<user-topic>/ssr/Relay_<0-28>/status` - Status of the relay with given id:  
&nbsp;&nbsp;&nbsp;&nbsp;`ssr_id:<id>`       - Index of the relay, 0-28.  
&nbsp;&nbsp;&nbsp;&nbsp;`ssr_state:<0/1>`   - The current state.  
&nbsp;&nbsp;&nbsp;&nbsp;`run_time:<sec>`    - Number of seconds the relay is going to stay in the current state, 0 means infinitely.  
&nbsp;&nbsp;&nbsp;&nbsp;`delay:<sec>`       - Number of seconds the relay waits before switching to the opposite state, 0 means never.  
### HTTP
The status of all relays is reported with this http request, where `port` is defined in `params.txt` - as `WEBSERVER_PORT`:  
    `curl http://<device_ip>:<port>/ssr/status`

## Control
### MQTT
The state of a relay can be set with MQTT commands, send to this topic, where `<user-topic>` is defined in `params.txt` - as `MQTT_TOPIC`:  
`<user-topic>/command` - Set the state of a SSR, request body:  
&nbsp;&nbsp;&nbsp;&nbsp;`ssr?set:<id>:<state>:<time_sec>:<delay_sec>` - Where `id` is index of the relay 0-28, `state` is the desired state 0-1, `time_sec` is time in seconds the relay must stay in the desired state, `delay_sec` is the delay after that the relay will transition into the desired state. The `time_sec` and `delay_sec` are optional. If `time_sec` is 0 or is omitted, the relay will stay in the desired state infinitely. If `delay_sec` is 0 or is omitted, the relay will transition into the desired state immediately.
### HTTP
The state of a relay can be set with this http request. The request parameters are the same as in the MQTT request body:  
`curl http://<device_ip>:<port>/ssr?set:<id>:<state>:<time_sec>:<delay_sec>`
## Example
Turn `on` a relay with index `2` for `60` seconds after a delay of `30` seconds:  
- with MQTT, send request to `<user-topic>/command`:  
&nbsp;&nbsp;&nbsp;&nbsp;`ssr?set/2:1:60:30`  
- with HTTP, send this request to a device with IP address `192.168.0.1` and port `8080`  
&nbsp;&nbsp;&nbsp;&nbsp;`curl http://192.168.0.1:8080/ssr?set:2:1:60:30`  

Turn `off` a relay with index `2` immediately:  
- with MQTT, send request to `<user-topic>/command/ssr`:  
&nbsp;&nbsp;&nbsp;&nbsp;`ssr/set/2:0`  
- with HTTP, send this request to a device with IP address `192.168.0.1` and port `8080`  
&nbsp;&nbsp;&nbsp;&nbsp;`curl http://192.168.0.1:8080/ssr/set:2:0`  
