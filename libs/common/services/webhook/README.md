# Webhook
Support for webhooks for sending notifications to a remote server.   

## Configuration
Configuration parameters in `params.txt` file:  
```
WEBHOOK_SERVER      <web_server>
WEBHOOK_PORT        <port>
WEBHOOK_ENDPOINT    <url>
```
Where `web_server` is domain name or IP address of a webhook server and `port` is the TCP port of that server. If no port is specified, `80` is used by default.
The webhook URL is defined by the `url` parameter.  

Example configurations:
```
WEBHOOK_SERVER	    192.168.1.1
WEBHOOK_PORT        8080
WEBHOOK_ENDPOINT    /api/webhook/-V1AFRE3zagLAUJh8FQxkbnUb
```

## API
```
bool webhook_connected();
int webhook_send( char *data, int datalen, char *http_command, char *content_type);
```
