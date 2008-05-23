#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

void init(unsigned long devtree_ptr);

volatile int count = 0;

void fit_handler(trapframe_t *frameptr)
{
	printf(" > got FIT interrupt...PASSED\n");
	mtspr(SPR_TSR, TSR_FIS);
	count++;
}

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	printf("Fixed Interval Timer test\n");

	enable_extint();
	mtspr(SPR_TCR, 0x00816000);
	
	while (count < 3);
	
	disable_extint();

	printf("Test Complete\n");
}
