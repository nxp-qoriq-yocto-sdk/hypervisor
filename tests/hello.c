
#include <libos/libos.h>
#include <libos/hcalls.h>

extern void init(void);

void start(void)
{
	uint32_t status;
	char *str;
	uint32_t channel;

	init();

	printf("Hello World\n");

	channel = 0;

	str = "byte-channel:hi!";  // 16 chars
	status = fh_byte_channel_send(channel, 16, *(uint32_t *)&str[0], *(uint32_t *)&str[4], *(uint32_t *)&str[8],*(uint32_t *)&str[12]);

}
