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

static void tlb1_init(void);
static void core_init(void);
void start_guest(void);

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0x01000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M
#define UART_OFFSET		0x11c500

void start(unsigned long devtree_ptr)
{
	core_init();
	console_init(CCSRBAR_VA + UART_OFFSET);

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

extern int print_ok;  /* set to indicate printf can work now */

static void tlb1_init(void)
{

	tlb1_set_entry(62, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	print_ok = 1;
}
