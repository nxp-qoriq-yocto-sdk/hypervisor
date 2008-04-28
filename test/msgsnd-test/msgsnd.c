/*
 * msgsnd test program
 */

#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>

extern void init(void);

void ext_int_handler(trapframe_t *frameptr)
{
	printf("External interrupt\n");
}

void start(void)
{
	uint32_t status;
	char *str;
	uint32_t channel;
	uint32_t rxavail;
	uint32_t txavail;
	char buf[16];
	uint32_t x;
	unsigned int cnt;
	int i;

	init();

	enable_extint();
	enable_critint();

	printf("Hello World\n");

	channel = 0;

        // Normal doorbell
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	// Critical doorbell
	asm volatile ("msgsnd %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));

#define TEST
#ifdef TEST
	while (1) {
		status = fh_byte_channel_poll(channel,&rxavail,&txavail);
		if (rxavail > 0) {
			status = fh_byte_channel_receive(channel, &cnt, buf);
			for (i=0; i < cnt; i++) {
				printf("%c",buf[i]);
			}
		}
	}
#endif

}
