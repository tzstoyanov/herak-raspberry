# Scripts

The script engine runs scripts from files, saved in the file system of the device. A script is a list of commands, executed one after another in the order from the file. On startup, all files from the `/scripts` directory with extension `.run` are loaded as scripts. The files can be uploaded to the device using [tftp](../tftp_srv/README.md).  
- Scripts are loaded only at boot time. If a new script is uploaded, the device must be rebooted to load it.  
- The format of the script file is:  
`@name <script name>` - optional, the name of the script, used to address it. If the name is not set, the name of the file is used (without the extension).  
`@desc <script description>` - optional, description of the script.  
`@corn <cron string>` - optional, cron schedule for running the script.  
`@cron_enable <0/1>` - optional, enable or disable the cron schedule of the script.  
`@notify <0/1>` - optional, enable or disable sending the web hook notifications when the script is started.  
`#` - any line starting with this symbol is a comment and is not parsed by the script engine.  
`<module_name>?<command>[:<param1>:[param2]:...]` - command to be executed, one per line.  

## Example
Example script to trigger given set of [SSRs](../../devices/ssr/README.md) which runs on every 10 minutes:  
```
@name ssr_trigger
@desc Run the given relays for 20sec, one after another
#    sec min  hour  dom  month  dow
@cron 0  */10   *    *      *    *
@cron_enable 1
ssr?reset
ssr?set:0:1>:20:0
ssr?set:1:1>:20:20
ssr?set:4:1>:20:40
ssr?set:7:1>:20:60
```
Save the file as `ssr.run` and upload it to the device:  
` tftp <ip_addr> -p put ssr.run /scripts/ssr.run`  
Execute the script:  
`curl http://<ip_addr>:<port>/scripts?run:ssr_trigger`

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `run:<name>` - run the script with given name.
- `auto_run:<name>:<0/1>` - disable / enable auto run of the script with given name, according to the configured cron schedule.

Example command for running a script. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`. The name of the script is `ssr_trigger`.
- Using HTTP: `curl http://192.168.1.1:8080/scripts?run:ssr_trigger`
- Using MQTT: send request to topic `test/dev/command` with content `scripts?run:ssr_trigger`.


## Credits
[https://github.com/staticlibs/ccronexpr](https://github.com/staticlibs/ccronexpr)
[https://github.com/shaneapowell/time.ccronexpr](https://github.com/shaneapowell/time.ccronexpr)
