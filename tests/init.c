
#include <libos/libos.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/io.h>
#include <libos/ns16550.h>
#include <libos-client.h>
#include <libfdt.h>

extern uint8_t init_stack_top;

cpu_t cpu0 = {
        .kstack = &init_stack_top - FRAMELEN,
        .client = 0,
};

static void tlb1_init(void);
static void  core_init(void);

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M
#define UART_OFFSET 0x11d500

void *fdt;

void init(unsigned long devtree_ptr)
{
	core_init();

	/* alloc the heap */
        fdt = (void *)(devtree_ptr + PHYSBASE);

        unsigned long heap = (unsigned long)fdt + fdt_totalsize(fdt);
        heap = (heap + 15) & ~15;

        alloc_init(heap, heap + (0x100000-1));  // FIXME: hardcoded 1MB heap

	console_init(ns16550_init((uint8_t *)CCSRBAR_VA + UART_OFFSET, 0, 0, 16));

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}

/*
 *    after tlb1_init:
 *        TLB1[0]  = CCSR
 *        TLB1[15] = OS image 16M
 */



extern int print_ok;  /* set to indicate printf can work now */

static void tlb1_init(void)
{
	tlb1_set_entry(0, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, 0);

	print_ok = 1;
}

void dec_handler(trapframe_t *frameptr)
{
}

int extint_cnt = 0;;
void ext_int_handler(trapframe_t *frameptr)
{
	uint8_t c;

	extint_cnt++;

	printf("ext int\n");

	c = in8((uint8_t *)(CCSRBAR_VA+0x11d500));
}
