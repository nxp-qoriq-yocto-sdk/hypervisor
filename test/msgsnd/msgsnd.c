/*
 * msgsnd test program
 */

#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>

extern void init(void);

volatile unsigned int doorbell;
volatile unsigned int critical_doorbell;

void ext_int_handler(trapframe_t *frameptr)
{
	printf("External interrupt\n");
}

void ext_doorbell_handler(trapframe_t *frameptr)
{
	doorbell = 1;
}

void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
	critical_doorbell = 1;
}

void start(void)
{
	volatile unsigned int timeout;

	init();

	printf("msgsnd test\n");

	disable_extint();
	disable_critint();
	sync();

        // Normal doorbell
	printf(" > msgsnd while interrupts enabled: ");

	doorbell = 0;
	sync();
	enable_extint();
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	timeout = 10000;
	while (--timeout && !doorbell);
	if (timeout)
		printf("PASSED\n");
	else
		printf("FAILED\n");

	// Delayed doorbell
	printf(" > msgsnd while interrupts disabled: ");

	disable_extint();
	sync();
	doorbell = 0;
	sync();
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	timeout = 10000;
	while (--timeout && !doorbell);
	if (timeout)
		printf("failed (timeout=%u)\n", timeout);
	else {
		doorbell = 0;
		sync();
		enable_extint();
		timeout = 10000;
		while (--timeout && !doorbell);
		if (timeout)
			printf("PASSED\n");
		else
			printf("FAILED\n");
	}
	disable_extint();

	// Critical doorbell
	printf(" > critical msgsnd while interrupts enabled: ");

	critical_doorbell = 0;
	sync();
	enable_critint();
	sync();
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));

	timeout = 10000;
	while (--timeout && !doorbell);
	if (timeout)
		printf("PASSED\n");
	else
		printf("FAILED\n");

	// Delayed critical doorbell
	printf(" > critical msgsnd while interrupts disabled: ");

	disable_critint();
	sync();
	critical_doorbell = 0;
	sync();
	asm volatile ("msgsnd %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));

	timeout = 10000;
	while (--timeout && !critical_doorbell);
	if (timeout)
		printf("FAILED (timeout=%u)\n", timeout);
	else {
		critical_doorbell = 0;
		sync();
		enable_critint();
		timeout = 10000;
		while (--timeout && !critical_doorbell);
		if (timeout)
			printf("PASSED\n");
		else
			printf("FAILED\n");
	}
	disable_critint();

	// Normal doorbell msgclr
	printf(" > msgclr test: ");

	disable_extint();
	sync();
	doorbell = 0;
	sync();
	asm volatile ("msgsnd %0" : : "r" (mfspr(SPR_PIR)));
	sync();
	asm volatile ("msgclr %0" : : "r" (mfspr(SPR_PIR)));
	sync();
	enable_extint();
	sync();

	timeout = 10000;
	while (--timeout && !doorbell);
	if (timeout)
		printf("FAILED (timeout=%u)\n", timeout);
	else {
		printf("PASSED\n");
	}
	disable_extint();

	// Normal doorbell msgclr
	printf(" > critical msgclr test: ");

	disable_critint();
	sync();
	critical_doorbell = 0;
	sync();
	asm volatile ("msgsnd %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));
	sync();
	asm volatile ("msgclr %0" : : "r" (0x08000000 | mfspr(SPR_PIR)));
	sync();
	enable_critint();
	sync();

	timeout = 10000;
	while (--timeout && !critical_doorbell);
	if (timeout)
		printf("FAILED (timeout=%u)\n", timeout);
	else {
		printf("PASSED\n");
	}
	disable_critint();


	printf("Test Complete\n");

}
