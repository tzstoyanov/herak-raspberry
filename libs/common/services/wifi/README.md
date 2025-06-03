# WiFi
Internet access using build in WiFi module and [lwIP](https://savannah.nongnu.org/projects/lwip/lwIP) TCP stack.

## Configuration
Configuration parameters in `params.txt` file:  
```
WIFI_SSD	        <ssd>
WIFI_PASS	        <password>
```
Where `ssd` is the name of the WiFi network to connect, `password` is the password used for that connection.

Example configurations:
```
WIFI_SSD    public
WIFI_PASS   guest    
```

## API
```
bool wifi_is_connected(void);
```
