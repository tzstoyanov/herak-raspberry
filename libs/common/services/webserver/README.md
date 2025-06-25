# Web server
Support for running local Web server. The server is used to receive and execute commands.

## Configuration
Configuration parameters in `params.txt` file:  
```
WEBSERVER_PORT        <port>
```
Where `port` is the local TCP port of the Web server.

Example configurations:
```
WEBSERVER_PORT        8080
```

## API
```
int webserv_client_send_data(int client_idx, char *data, int datalen);
int webserv_port(void);
int webserv_client_close(int client_idx);
```
