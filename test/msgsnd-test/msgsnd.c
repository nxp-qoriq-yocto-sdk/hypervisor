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

	printf("msgsnd test\n");

	channel = 0;

        // Normal doorbell
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	// Critical doorbell
	asm volatile ("msgsnd %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));

	printf("Test Complete\n");

}
