#include <stddef.h>

int http_get_file(const char *const host, const char *const port, const char *filename, char *req_buf, size_t req_buf_len, char *resp_buf, size_t resp_buf_len, void (*callback)(char *, int));
