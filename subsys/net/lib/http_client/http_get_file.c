#include <bsd.h>
#include <net/http_client.h>
#include <net/http_get_file.h>
#include <logging/log.h>
#include <net/socket.h>
#include <zephyr/types.h>
#include <stdio.h>

LOG_MODULE_REGISTER(http_get_file);

const u32_t family = AF_INET;
const u32_t proto = IPPROTO_TCP;

static int get_content_length(char * rsp)
{
	u32_t len = 0;
	u32_t accumulator = 1;
	char * start = rsp;

	LOG_DBG("Looking for content len");
	while(strncmp(rsp++, "Content-Length: ", 16) != 0) {
		if (rsp - start > 500) {
			return -1;
		}
	}
	LOG_DBG("Found 'Content-Length:' at address: %p", rsp);
	rsp += 16;
	start = rsp-1;

	while(*(rsp++) != '\n');

	rsp -= 2;

	while (rsp-- != start) {
		u32_t val = rsp[0] - '0';
		LOG_DBG("Converting: %c, val: %d, current len: %d", rsp[0], val, len);
		len += val * accumulator;
		accumulator *= 10;
	}

	LOG_DBG("Length: %d", len);
	return len;
}

int http_get_file(struct get_file_param *param)
{
    int len = 0;
	int fd;
    int payload_size = 0;
	int payload_idx;
	int received_payload_len = 0;
	bool header_received = false;

	bsd_init();

    snprintf(param->req_buf, param->req_buf_len, "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n", param->filename, param->host);

	fd = httpc_connect(param->host, NULL, family, proto);
	if (fd < 0) {
		LOG_ERR("httpc_connect() failed, err %d", errno);
		return -1;
	}

	/* Get first load of packet, extract payload size */
	(void) httpc_request(fd, param->req_buf, strlen(param->req_buf));

	while(!header_received)
	{
		len = httpc_recv(fd, param->resp_buf, param->resp_buf_len, MSG_PEEK);
		if (len == -1) {
			if (errno == EAGAIN)
			{
				continue;
			}
			LOG_ERR("httpc_recv() failed, err %d", errno);
			return -1;
		}
		char *pstr = strstr(param->resp_buf, "\r\n\r\n");
		if (pstr != NULL) {
			payload_idx = pstr - param->resp_buf;
			if (payload_idx > 0) {
				payload_idx += 4;
				len = httpc_recv(fd, param->resp_buf, param->resp_buf_len, 0);
				header_received = true;
			}
		}
	}
	payload_size = get_content_length(param->resp_buf);
	if (payload_size < 0) {
		LOG_ERR("Could not find 'Content Length'");
		return -1;
	}

	received_payload_len = len - payload_idx;
	while (received_payload_len > 0) {
		
        param->callback(&param->resp_buf[payload_idx], received_payload_len);
		payload_idx = 0;
		received_payload_len = httpc_recv(fd, param->resp_buf, param->resp_buf_len, 0);
	}
    return payload_size;
}