
#include <bsd.h>
#include <net/http_client.h>
#include <logging/log.h>
#include <net/socket.h>
#include <zephyr/types.h>

LOG_MODULE_REGISTER(main);

/**< The server that hosts the firmwares. */
const char *host = "188.166.45.240";

static char http_resp_buf[1024];
static char http_req_buf[1024] = {
	"GET /lol.txt HTTP/1.1\r\n"
	"Host: 188.166.45.240\r\n\r\n"
};

const static char * CL = "Content-Length: ";


int get_content_length(char * rsp)
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

int get_payload_idx(char * rsp)
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


int main(void)
{
	int len;
	int fd;
	int payload_size;
	int payload_idx;
	int received_payload_len;
	bool first = true;

	bsd_init();


	fd = httpc_connect(host, NULL, AF_INET, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("httpc_connect() failed, err %d", errno);
		return -1;
	}

	/* Get first load of packet, extract payload size */
	(void) httpc_request(fd, http_req_buf, strlen(http_req_buf));

	/* Sleep before starting to process downloaded data,
	 * otherwise the program will hang (no log output).
	 * TODO: figure why this is necessary.
	 */
	k_sleep(K_SECONDS(1));

	len = sizeof(http_resp_buf);

	while (len == sizeof(http_resp_buf)) {
		len = httpc_recv(fd, http_resp_buf, sizeof(http_resp_buf));
		if (first) {
			payload_size = get_content_length(http_resp_buf);
			if (payload_size < 0) {
				LOG_ERR("Could not find 'Content Length'");
				return -1;
			}
			payload_idx = get_payload_idx(http_resp_buf);
			if (payload_idx < 0) {
				LOG_ERR("Could not find payload index");
				return -1;
			}
			for(u32_t i = payload_idx; i < len; ++i) {
				printk("%c", http_resp_buf[i]);
			}

			first = false;
		}
		if (!len) {
			LOG_ERR("httpc_recv() failed, err %d", errno);
			return -1;
		}

		LOG_DBG("httpc_recv(), received %d bytes from 0x%x", len, fd);
		for(u32_t i = 0; i < len; ++i) {
			printk("%c", http_resp_buf[i]);
		}
	}
	LOG_DBG("Expected pl len %d, got: %d", received_payload_len, len);

	return 0;
}
