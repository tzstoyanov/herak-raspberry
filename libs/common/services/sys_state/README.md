# System status
Report periodically status of the entire system on console, log server and MQTT.

## Configuration
Configuration parameters in `params.txt` file:  
```
SYS_STATE_LOG_SEC   <seconds>
```
Where `seconds` is the period on which the status is reported. If not set, by default is 1 hour. Specify 0 to disable status reporting.  
Example configurations, report system state on 30 minutes:
```
SYS_STATE_LOG_SEC	1800
```
