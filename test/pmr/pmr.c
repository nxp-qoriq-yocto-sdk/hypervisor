
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

void init(unsigned long devtree_ptr);

int irq;
extern void *fdt;
volatile int exception;

#define PMLCa0 144

void prog_interrupt(trapframe_t *frameptr)
{
	printf("program interrupt exception -- FAILED\n");
	exception++;
	frameptr->srr0 += 4;
}

void start(unsigned long devtree_ptr)
{
	uint32_t pmr_reg, pmr_val;

	init(devtree_ptr);

	enable_extint();

	printf("PMR test\n");

	pmr_reg = PMLCa0;
	asm volatile("mfpmr %0, %1" : "=r" (pmr_val) : "i" (pmr_reg) : "memory");

#ifdef DEBUG
	printf("pmr reg read val =%x\n", pmr_val);
#endif
	if (!exception)
		printf("PMR reg read successful -- PASSED\n");

	printf("Test Complete\n");
}
