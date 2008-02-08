
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>

extern void init(void);

void ext_int_handler(trapframe_t *frameptr)
{
	printf("ext int\n");

}

void start(void)
{
	uint32_t status;
	char *str;
	uint32_t channel;
	uint32_t rxavail;
	uint32_t txavail;
	uint8_t buf[16];
	uint32_t x;
	int cnt;
	int i;

	init();

	enable_extint();

	printf("Hello World\n");

	channel = 0;

	str = "byte-channel:hi!";  // 16 chars
	status = fh_byte_channel_send(channel, 16, *(uint32_t *)&str[0], *(uint32_t *)&str[4], *(uint32_t *)&str[8],*(uint32_t *)&str[12]);

	str = "type some chars:";  // 16 chars
	status = fh_byte_channel_send(channel, 16, *(uint32_t *)&str[0], *(uint32_t *)&str[4], *(uint32_t *)&str[8],*(uint32_t *)&str[12]);

#define TEST
#ifdef TEST
	while (1) {
		status = fh_byte_channel_poll(channel,&rxavail,&txavail);
		if (rxavail > 0) {
			status = fh_byte_channel_receive(channel,16,&buf[0],&cnt);
			for (i=0; i < cnt; i++) {
				printf("%c",buf[i]);
			}
		}
	}
#endif

}
