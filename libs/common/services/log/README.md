# Log subsystem
A single set of log APIs with printf style of parameters. The same log message is sent to multiple destinations at the same time,
depending on the configuration and the use case:
 - System console, if attached to the USB port.
 - Remote syslog server, if configured in `params.txt` and there is connectivity to it.
 - On a http session, if the log output is redirected with a special web command:  
 `curl http://<ip_address>:<port>/sys?log_on`

## Configuration
Configuration parameters in `params.txt` file:  
```
SYSLOG_SERVER_ENDPOINT	<syslog_server>:<port>
```
Where `syslog_server` is URL or IP address of a remote syslog server, `port` is the UDP port of the server.
If not set, default port `514` is used. That configuration is optional.  
Example configurations:
```
SYSLOG_SERVER_ENDPOINT	192.168.1.1:514
SYSLOG_SERVER_ENDPOINT	example.com:514
```

## API
```
bool hlog_remoute(void);
void hlog_web_enable(bool set);
void log_level_set(uint32_t level);

void hlog_info(topic, args...);
void hlog_warning(topic, args...);
void hlog_err(topic, args...);
void hlog_dbg(topic, args...);
void hlog_null(topic, args...);
```
