
#include <libos/libos.h>
#include <libos/hcalls.h>
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

int get_uart_offset(void);

void *fdt;
int coreint;

int get_uart_offset()
{
	int ret;
	int *handle_p;
	const char *path;

	ret = fdt_subnode_offset(fdt, 0, "aliases");
	if (ret < 0)
		return ret;

	path = fdt_getprop(fdt, ret, "stdout", &ret);
	if (!path)
		return ret;

	ret = fdt_path_offset(fdt, path);

	handle_p = (int *)fdt_getprop(fdt, ret, "reg", &ret);

	return *handle_p;
}

void init(unsigned long devtree_ptr)
{
	int node, len;
	const uint32_t *prop;
	core_init();

	/* alloc the heap */
	fdt = (void *)(devtree_ptr + PHYSBASE);

	uintptr_t heap = (unsigned long)fdt + fdt_totalsize(fdt);
	heap = (heap + 15) & ~15;

	simple_alloc_init((void *)heap, 0x100000); // FIXME: hardcoded 1MB heap
	valloc_init(1024 * 1024, PHYSBASE);
	node = get_uart_offset();

	console_init(ns16550_init((uint8_t *)CCSRBAR_VA + node, 0, 0, 16));

	node = fdt_subnode_offset(fdt, 0, "chosen");
	if (node >= 0) {
		prop = fdt_getprop(fdt, node, "fsl,hv-pic-coreint", &len);
		if (prop)
			coreint = 1;
	}
}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();
}

void (*secondary_startp)(void) = NULL;

void secondary_init(void)
{
	core_init();
	if (secondary_startp) {
		secondary_startp();
	}
}


/*
 *    after tlb1_init:
 *        TLB1[0]  = CCSR
 *        TLB1[15] = OS image 16M
 */



static void tlb1_init(void)
{
	tlb1_set_entry(0, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, 0);
}

void dec_handler(trapframe_t *frameptr)
{
}

#if 0
int extint_cnt = 0;;
void ext_int_handler(trapframe_t *frameptr)
{
	uint8_t c;
//	uint32_t x;

	extint_cnt++;

	printf("ext int\n");

//	x = mfspr(SPR_GEPR);
//	printf("gepr = %08lx\n",(long)x);

//	c = in8((uint8_t *)(CCSRBAR_VA+0x11d500));
}
#endif
