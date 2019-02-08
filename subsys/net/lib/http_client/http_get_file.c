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

static int get_payload_idx(char * rsp)
{
	char * start = rsp;
	while(strncmp(rsp++, "\r\n\r\n", 4)) {
		if (rsp - start > 500) {
			return -1;
		}
	}

	LOG_DBG("Payload index: %d", rsp-start);
	return rsp-start + 3;
}

int http_get_file(const char *const host, const char *const port, const char *filename, char *req_buf, size_t req_buf_len, char *resp_buf, size_t resp_buf_len, void (*callback)(char *, int))
{
    int len;
	int fd;
    int payload_size = 0;
	int payload_idx;
	int received_payload_len = 0;
	bool first = true;

	bsd_init();

    snprintf(req_buf, req_buf_len, "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n", filename, host);

	fd = httpc_connect(host, NULL, family, proto);
	if (fd < 0) {
		LOG_ERR("httpc_connect() failed, err %d", errno);
		return -1;
	}

	/* Get first load of packet, extract payload size */
	(void) httpc_request(fd, req_buf, strlen(req_buf));

	/* Sleep before starting to process downloaded data,
	 * otherwise the program will hang (no log output).
	 * TODO: figure why this is necessary.
	 */
	// k_sleep(K_SECONDS(1));

	len = resp_buf_len;

	while (len == resp_buf_len) {
		len = httpc_recv(fd, resp_buf, resp_buf_len);
		if (len == -1) {
			LOG_ERR("httpc_recv() failed, err %d", errno);
			return -1;
		}
		if (first) {
            payload_size = get_content_length(resp_buf);
			if (payload_size < 0) {
				LOG_ERR("Could not find 'Content Length'");
				return -1;
			}
			payload_idx = get_payload_idx(resp_buf);
			if (payload_idx < 0) {
				LOG_ERR("Could not find payload index");
				return -1;
			}

			received_payload_len = len - payload_idx;
			LOG_DBG("Recevied payload first packet: %d", received_payload_len);
			printk("First packet done\n");
			first = false;
            callback(&resp_buf[payload_idx], received_payload_len);
		} else {
			printk("Start next\n");
			received_payload_len += len;
            callback(resp_buf, len);
		}
	}
    return payload_size;
}