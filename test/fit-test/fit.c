#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

void init(unsigned long devtree_ptr)

void fit_handler(trapframe_t *frameptr)
{
	printf("FIT\n");
	mtspr(SPR_TSR, TSR_FIS);
}

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	printf("Fixed Interval Timer test\n");

	enable_extint();
	mtspr(SPR_TCR, 0x00816000);
	
	for (;;);
}
