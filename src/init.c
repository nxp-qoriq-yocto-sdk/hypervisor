#include <uv.h>
#include <libos/console.h>
#include <percpu.h>
#include <libos/spr.h>
#include <libos/trapframe.h>

static gcpu_t noguest = {
	// FIXME 64-bit
	.tlb1_free = { ~0UL, ~0UL },
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest,
};

extern void tlb1_init(void);
static void core_init(void);
void start_guest(void);

void start(unsigned long devtree_ptr)
{
	core_init();
	console_init();

	printf("=======================================\n");
	printf("Freescale Ultravisor 0.1\n");

	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DTLBGS | EHCSR_ITLBGS |
	      EHCSR_DSIGS | EHCSR_ISIGS | EHCSR_DUVD);

	start_guest();

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
