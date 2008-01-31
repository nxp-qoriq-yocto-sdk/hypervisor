
#include <libos/libos.h>
#include <libos/hcalls.h>

extern void init(void);

void start(void)
{
	uint32_t status;
	char *str;
	uint32_t channel;
	uint32_t rxavail;
	uint32_t txavail;

	init();

	printf("Hello World\n");

	channel = 0;

	str = "byte-channel:hi!";  // 16 chars
	status = fh_byte_channel_send(channel, 16, *(uint32_t *)&str[0], *(uint32_t *)&str[4], *(uint32_t *)&str[8],*(uint32_t *)&str[12]);

#ifdef TEST
	while (1) {
		status = fh_byte_channel_poll(channel,&rxavail,&txavail);
		printf("poll status=%d, rx=%d, tx=%d\n",status,rxavail,txavail);
	}
#endif

}
