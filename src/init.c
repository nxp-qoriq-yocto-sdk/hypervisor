#include <uv.h>
#include <libos/console.h>
#include <percpu.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <mpic.h>

static gcpu_t noguest = {
	// FIXME 64-bit
	.tlb1_free = { ~0UL, ~0UL },
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest,
};

struct console_calls console = {
	.putc = uart_putc
};

int global_init_done = 0;

static void tlb1_init(void);
static void core_init(void);
static void release_secondary_cores(unsigned long devtree_ptr);
static void partition_init(unsigned long devtree_ptr);
void start_guest(void);

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0x01000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M
#define UART_OFFSET		0x11c500

void start(unsigned long devtree_ptr)
{
	core_init();

	if (!global_init_done) {

		printf("=======================================\n");
		printf("Freescale Ultravisor 0.1\n");

		uart_init(CCSRBAR_VA + UART_OFFSET);

		console_init();

		mpic_init(devtree_ptr);

		// pamu init

		global_init_done = 1;

		release_secondary_cores(devtree_ptr);
	}

	partition_init(devtree_ptr);

	start_guest();

}

static void release_secondary_cores(unsigned long devtree_ptr)
{
	// go thru cpu nodes in device tree
	// for each cpu
	//    get cpu release method
	//    if not spin, error
	//    get release address
	//    write address of secondary entry point (head.S)
}

static void partition_init(unsigned long devtree_ptr)
{
	// -init/alloc partition data structure
	// -identify partition node in device tree for
	//  this partition, do partition init
	//    -configure all interrupts-- irq#,cpu#
	//    -add entries to PAMU table
	// -create guest device tree
}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();

	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DTLBGS | EHCSR_ITLBGS |
	      EHCSR_DSIGS | EHCSR_ISIGS | EHCSR_DUVD);
}

extern int print_ok;  /* set to indicate printf can work now */

static void tlb1_init(void)
{

	tlb1_set_entry(62, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	print_ok = 1;
}
