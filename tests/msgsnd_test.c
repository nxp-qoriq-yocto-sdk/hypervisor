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
	uint32_t channel = 0;
	uint32_t rxavail;
	uint32_t txavail;
	uint8_t buf[16];
	uint32_t x;
	int cnt;
	int i;

	init();

	printf("Hello World\n");

	// Normal doorbell

	enable_extint();
	printf("Interrupts enabled\n");
	printf("Sending message\n");
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	disable_extint();
	printf("Interrupts disabled\n");
	printf("Sending message\n");
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	enable_extint();
	printf("Interrupts enabled\n");

	disable_extint();
	printf("Interrupts disabled\n");

	printf("Sending message\n");
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	printf("Clearing message\n");
	asm volatile ("msgclr %0" : : "r" (mfspr(SPR_PIR)));

	enable_extint();
	printf("Interrupts enabled\n");

	// Critical doorbell

	printf("Critical interrupts disabled\n");
	printf("Sending critical message\n");
	asm volatile ("msgsnd %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));

	printf("Critical interrupts enabled\n");
	enable_critint();

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
