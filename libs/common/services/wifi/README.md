# Network
Network access using build in WiFi module and [lwIP](https://savannah.nongnu.org/projects/lwip/lwIP) TCP stack.

## Configuration
Configuration parameters in `params.txt` file:  
```
WIFI_SSD	        <ssd_1>;<ssd_2>
WIFI_PASS	        <password_1>;<password_2>
```
- `WIFI_SSD`, mandatory. `ssd_1` is the name of the WiFi network to connect, `password_1` is the password used for that connection. Up to 3 WiFi networks are supported. The first successful connection wins.
- `WIFI_PASS`, mandatory. List of WiFi passwords, corresponding to the list of SSDs.

Example configurations:
```
WIFI_SSD    public
WIFI_PASS   guest    
```

## API
```
wifi_state_t wifi_get_state(void);
```
