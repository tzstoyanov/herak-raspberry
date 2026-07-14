# MQTT Client

A tiny wrapper around the [lwip mqtt](https://www.nongnu.org/lwip/2_0_x/group__mqtt.html) implementation.

## Configuration
Configuration parameters in `params.txt` file:  
```
MQTT_SERVER_ENDPOINT   <mqtt_server>:<port>
MQTT_USER	           <user>;<password>
MQTT_TOPIC             <topic>
MQTT_RATE_PPM	       <max>
```
- `MQTT_SERVER_ENDPOINT`, mandatory. `mqtt_server` is domain name or IP address of a mqtt server, `port` is the TCP port of the server. If not set, default port `1883` is used. 
- `MQTT_USER`, mandatory. Credential `user` and `password` for the MQTT server.
- `MQTT_TOPIC`, mandatory. `topic` is the prefix used by all mqtt messages. 
- `MQTT_RATE_PPM`, optional. Rate limit of the messages - count of `max` messages send per minute.  

Example configurations:
```
MQTT_SERVER_ENDPOINT	192.168.1.1:514
MQTT_SERVER_ENDPOINT	example.com:514
...
MQTT_USER	           guest;qwerty
MQTT_TOPIC             <test/data>
MQTT_RATE_PPM	       60
```

## Home Assistant integration
All MQTT sensors are auto-discovered by Home Assistant. The state is published using the topic:
```
<MQTT_TOPIC>/<module>/...
```

## API
```
int mqtt_msg_publish(char *topic, char *message, bool force);
int mqtt_msg_component_publish(mqtt_component_t *component, char *message);
int mqtt_msg_component_register(mqtt_component_t *component);

int mqtt_topic_listen(char *topic, mqtt_topic_cb_t func, void *context, bool json);

bool mqtt_is_connected(void);
bool mqtt_is_discovery_sent(void);
```
