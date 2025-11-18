# TFTP Client
A tiny wrapper around the [lwip tftp](https://www.nongnu.org/lwip/2_0_x/group__tftp.html) implementation.

## API
```
int tftp_url_parse(char *url, struct tftp_file_t *file);
int tftp_file_get(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context);
int tftp_file_put(const struct tftp_context *hooks, struct tftp_file_t *file, void *user_context);
```
