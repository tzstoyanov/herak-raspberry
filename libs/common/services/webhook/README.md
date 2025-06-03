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
int webhook_state(int idx, bool *connected, bool *sending);
int webhook_send(int idx, char *data, int datalen);
int webhook_add(char *addr, int port, char *content_type, char *endpoint, char *http_command,
				bool keep_open, webhook_reply_t user_cb, void *user_data);
```
