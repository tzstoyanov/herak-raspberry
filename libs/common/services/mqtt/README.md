# MQTT Client

A tiny wrapper around the [lwip mqtt](https://www.nongnu.org/lwip/2_0_x/group__mqtt.html) implementation.

## Configuration
Configuration parameters in `params.txt` file:  
```
MQTT_SERVER_ENDPOINT   <mqtt_server>:<port>
MQTT_USER	           <user>;<password>
MQTT_TOPIC             <topic>
MQTT_RATE_PPM	       <min>;<max>
```
Where `mqtt_server` is domain name or IP address of a mqtt server, `port` is the TCP port of the server.
If not set, default port `1883` is used. Credential are configured with `user` and `password` parameters.
The prefix used by all mqtt messages is defined with `topic`. There is a rate limit of the messages -
count of `min` and `max` messages send per minute.  
Example configurations:
```
MQTT_SERVER_ENDPOINT	192.168.1.1:514
MQTT_SERVER_ENDPOINT	example.com:514
...
MQTT_USER	           guest;qwerty
MQTT_TOPIC             <test/data>
MQTT_RATE_PPM	       1;60
```

## API
```
int mqtt_msg_publish(char *topic, char *message, bool force);
int mqtt_msg_component_publish(mqtt_component_t *component, char *message);
int mqtt_msg_component_register(mqtt_component_t *component);
int mqtt_add_commands(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data);

bool mqtt_is_connected(void);
bool mqtt_is_discovery_sent(void);
```
