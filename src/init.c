#include "uv.h"
#include "console.h"
#include "percpu.h"

static gcpu_t noguest;
hcpu_t hcpu0 = {
	.gcpu = &noguest,
};

extern void tlb1_init(void);

static void core_init(void);

void init(unsigned long devtree_ptr)
{
	core_init();
	console_init();

	printf("=======================================\n");
	printf("Freescale Ultravisor 0.1\n");

#if 0
	if (first instance) {
		platform_hw_init();
		uv_global_init();
	}

	uv_instance_init();

	guest_init();

	run_guest();

	/* run_guest() never returns */

#endif
}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();
}
