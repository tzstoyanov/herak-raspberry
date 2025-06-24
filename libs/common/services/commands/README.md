# Commands engine

Store, parse and execute commands on the system. There is no shell, but there are other ways to interact with the system.  
The format of a command is `<module_name>?<command>[:<param1>:[param2]:...]`. The command string can be passed as URL, that's why there are no intervals in it.  
There are 2 ways to send commands to a device:  
- *Web command*. If the web server is enabled, it listens on its configured port for commands. The command is passed as part of the URL: `curl http://<ip_address>:<port>/<module_name>?<command>[:<param1>:[param2]:...]`.  
- *MQTT command*. If the MQTT client is enabled, it is registered for topic `<mqtt_topic>/command`. The command can be send to that topic: `<module_name>?<command>[:<param1>:[param2]:...]`.  

The output of the command is send back to the user in these channels:  
- System console, if attached to the usb port.  
- Logs to a remote log server, if it is configured and connected when the command is executed.  
- HTTP response packet, if the command is received on the web server.  

All modules can register their own commands using the api `cmd_handler_add()`. Additional to them, these commands are auto created for each module:
 - `<module_name>?status` - Calls the module log callback, if registered.
 - `<module_name>?debug:<int>` - Sets the debug flags for the module. These flags are set only runtime, cleared on reboot.
 - `<module_name>?help` - Prints all commands, supported by the module.  
 
 There is also a help command, that prints all currently registered commands from all modules. This command does not belong to a module: `help`.  

## Example  
Example command for rebooting the device. This command is part of the [syscmd module](../syscmd/README.md). The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`  
- Using HTTP: `curl http://192.168.1.1:8080/sys?reboot`  
- Using MQTT: send request to topic `test/dev/command` with content `sys?reboot`.  

## API
```
int cmd_handler_add(char *module, app_command_t *commands, int commands_cont, char *description, void *user_data);
int cmd_exec(cmd_run_context_t *cmd_ctx, char *cmd_str);
```
