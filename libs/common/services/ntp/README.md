# NTP Client
NTP client, synchronizing local time and date with a set of remote NTP servers.

## Configuration
Configuration parameters in `params.txt` file:  
```
NTP_SERVERS   <ntp_server>;<ntp_server>;...
```
Where `ntp_server` is domain name or IP address of a NTP server. Up to 3 servers are supported.  
Example configurations:
```
NTP_SERVERS	192.168.1.1;pool.ntp.org
```
## API
```
bool ntp_connected(void);
```
