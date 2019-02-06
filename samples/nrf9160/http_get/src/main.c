
#include <bsd.h>
#include <net/http_client.h>
#include <logging/log.h>
#include <net/socket.h>
#include <zephyr/types.h>
#include <flash.h>

LOG_MODULE_REGISTER(main);

#define FLASH_OFFSET		0x56000
#define FLASH_PAGE_SIZE   	4096

/**< The server that hosts the firmwares. */
const char *host = "188.166.45.240";

static char http_resp_buf[1024];
static char http_req_buf[1024] = {
	"GET /cleartext_numbers.txt HTTP/1.1\r\n"
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
	int received_payload_len = 0;
	bool first = true;
	struct device *flash_dev;

	bsd_init();

	flash_dev = device_get_binding(DT_FLASH_DEV_NAME);

	if (!flash_dev) {
		LOG_ERR("Nordic nRF5 flash driver was not found!\n");
		return -1;
	}

	fd = httpc_connect(host, NULL, AF_INET, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("httpc_connect() failed, err %d", errno);
		return -1;
	}

	/* Get first load of packet, extract payload size */
	(void) httpc_request(fd, http_req_buf, strlen(http_req_buf));

	if (flash_erase(flash_dev, FLASH_OFFSET, FLASH_PAGE_SIZE) != 0) {
		LOG_ERR("Flash erase page at %0X failed!\n", (unsigned)FLASH_OFFSET);
		return -1;
	}

	/* Sleep before starting to process downloaded data,
	 * otherwise the program will hang (no log output).
	 * TODO: figure why this is necessary.
	 */
	k_sleep(K_SECONDS(1));

	len = sizeof(http_resp_buf);

	flash_write_protection_set(flash_dev, false);
	u32_t offset = FLASH_OFFSET;
	while (len == sizeof(http_resp_buf)) {
		len = httpc_recv(fd, http_resp_buf, sizeof(http_resp_buf));
		if (!len) {
			LOG_ERR("httpc_recv() failed, err %d", errno);
			return -1;
		}
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

			received_payload_len = len - payload_idx;
			LOG_DBG("Recevied payload first packet: %d", received_payload_len);
			for(u32_t i = 0; i < received_payload_len; ++i) {
				printk("%c", http_resp_buf[i+payload_idx]);
			}
			printk("First packet done\n");
			first = false;
			if (flash_write(flash_dev, offset, &http_resp_buf[payload_idx], received_payload_len) != 0) {
				LOG_ERR("Flash write failed!\n");
				return -1;
			}
			offset += received_payload_len;
		} else {
			printk("Start second\n");
			received_payload_len += len;
			for(u32_t i = 0; i < len; ++i) {
				printk("%c", http_resp_buf[i]);
			}
			if (flash_write(flash_dev, offset, http_resp_buf, len) != 0) {
				LOG_ERR("Flash write failed!\n");
				return -1;
			}
			offset += len;
		}
	}
	flash_write_protection_set(flash_dev, true);
	LOG_DBG("Expected pl %d, got: %d", received_payload_len, payload_size);

	return 0;
}
