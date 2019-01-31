
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

int main(void)
{
	int len;
	int fd;

	bsd_init();


	fd = httpc_connect(host, NULL, AF_INET, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("httpc_connect() failed, err %d", errno);
		return -1;
	}

	(void) httpc_request(fd, http_req_buf, strlen(http_req_buf));

	/* Sleep before starting to process downloaded data,
	 * otherwise the program will hang (no log output).
	 * TODO: figure why this is necessary.
	 */
	k_sleep(K_SECONDS(1));

	len = httpc_recv(fd, http_resp_buf, sizeof(http_resp_buf));
	if (!len) {
		LOG_ERR("httpc_recv() failed, err %d", errno);
		return -1;
	}

	LOG_DBG("httpc_recv(), received %d bytes from 0x%x", len, fd);
	for(u32_t i = 0; i < len; ++i) {
		printk("%c", http_resp_buf[i]);
	}

	return 0;
}
