# Persistent configuration store

Store user defined configuration in the local flash file system.  

## Commands
Commands can be executed using the [commands engine](../commands/README.md).  
- `set:<name>:<value>` - Set user parameter.  
- `del:<name>`  - Delete user parameter.  
- `reset`       - Reset to default all user configuration.  
- `list`        - List supported user configurable parameters. Those who have local configuration are marked with [*].
- `purge`       - Delete unknown user configuration.  

Example command for listing the content of the supported user configurable parameters. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`
- Using HTTP: `curl http://192.168.1.1:8080/config?list`  
- Using MQTT: send request to topic `test/dev/command` with content `config?list`.  

## API
```
char *cfgs_param_get(char *name);
```
