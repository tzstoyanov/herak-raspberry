# Wake on LAN sender
Send Wake on LAN packets to devices in the local network.

## Commands
- `send:<mac_address>` - Send WoL packet to `mac_address`

Example command for sending WoL packet. The raspberry device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`. The WoL packet is send to a device with mac address `11:AA:22:BB:33:CC`.
- Using HTTP: `curl http://192.168.1.1:8080/wol?send:11:AA:22:BB:33:CC`
- Using MQTT: send request to topic `test/dev/command` with content `wol?send:11:AA:22:BB:33:CC`.

## API
```
int wol_send (uint8_t *mac);
```
