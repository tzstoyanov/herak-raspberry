# File system
A small fail-safe filesystem based on [littlefs](https://github.com/littlefs-project/littlefs).

## Commands
The commands can be sent to the device with a HTTP or a MQTT request. The result is printed on the current HTTP session, on the system console and on a remote log server. The device listens for HTTP commands on the `WEBSERVER_PORT` HTTP port and on `<MQTT_TOPIC>/command` MQTT topic, where these are configured in the `params.txt` file.  
- `ls:[<path>]` - List a directory content.  
- `cat:<path>`  - Display content of a file.  
- `format`      - Format the file system.  
- `status`      - Current status of the file system.  
- `rm:<path>`   - Delete file or directory (the directory must be empty).  
- `close_all`   - Close all opened files.  
- `cp:<source>?<destination>` - Copy `source` file to `destination`. Source and destination can be local files (with full path) or tftp url in format `tftp://<IP-ADDR>[:<PORT-NUM>]/<FILENAME>`. 

Example command for listing the content of the top directory. The device has address `192.168.1.1`, listens on HTTP port `8080` and uses MQTT topic `test/dev`  
- Using HTTP: `curl http://192.168.1.1:8080/fs?ls:/`  
- Using MQTT: send request to topic `test/dev/command` with content `fs?ls:/`.  

Example copy commands:
- Copy local file `/file1.ext` to : `/tmp/file2.ext` using HTTP:  
`curl http://192.168.1.1:8080/fs?cp:/file1.ext?/tmp/file2.ext`  
- Copy local file `/file1.ext` to : `/tmp/file2.ext` using MQTT:  
send request to topic `test/dev/command` with content `fs?cp:/file1.ext?/tmp/file2.ext`.  
- Copy remote file `file1.ext` from tftp server `192.168.1.1` running on default tftp port as local file with the same name in `/tmp` directory, using HTTP:  
`curl http://192.168.1.1:8080/fs?cp:tftp://192.168.1.1/file1.ext?/tmp/`  
- Copy remote file `file1.ext` from tftp server `192.168.1.1` running on default tftp port as local file in `/tmp/file2.ext` directory, using MQTT:  
send request to topic `test/dev/command` with content `fs?cp:tftp://192.168.1.1/file1.ext?/tmp/file2.ext`.  

## API
```
bool fs_is_mounted(void);
char *fs_get_err_msg(int err);
int fs_get_files_count(char *dir_path);
int fs_open(char *path, enum lfs_open_flags flags);
void fs_close(int fd);
int fs_gets(int fd, char *buff, int buff_size);
int fs_read(int fd, char *buff, int buff_size);
int fs_write(int fd, char *buff, int buff_size);
```

## Credits
- [littlefs](https://github.com/littlefs-project/littlefs)
- [pico-littlefs](https://github.com/lurk101/littlefs-lib)
