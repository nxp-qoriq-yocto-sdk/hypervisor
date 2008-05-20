
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

void init(unsigned long devtree_ptr);

extern void *fdt;

void ext_int_handler(trapframe_t *frameptr)
{
#if 0
	uint16_t vector;
	fh_vmpic_iack(&vector);
	printf("ext int %d\n",vector);
	fh_vmpic_eoi(irq);
#endif
}

void dump_dev_tree(void)
{
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n",s);
	}
	printf("------\n");
}

int test_dma_memcpy(void)
{
	int ret;

	printf("DMA0 mode register = 0x%x\n", 
			in32((uint32_t *)(CCSRBAR_VA+0x100100)));

	/* basic DMA direct mode test */
	out32((uint32_t *)(CCSRBAR_VA+0x100100), 0x00000404);
	out32((uint32_t *)(CCSRBAR_VA+0x100120), 0x20);	/* count */
	out32((uint32_t *)(CCSRBAR_VA+0x100110), 0x00050000); /* read,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100118), 0x00050000); /* write,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100114), 0x0); /* src */
	out32((uint32_t *)(CCSRBAR_VA+0x10011c), 0x20); /* dest */

	printf("DMA0 mode register after memcpy = 0x%x\n", 
			in32((uint32_t *)(CCSRBAR_VA+0x100100)));
	printf("DMA0 dest. register after memcpy = 0x%x\n", 
			in32((uint32_t *)(CCSRBAR_VA+0x10011c)));
	printf("DMA0 status register after memcpy = 0x%x\n", 
			in32((uint32_t *)(CCSRBAR_VA+0x100104)));

	return 0;
}


void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	dump_dev_tree();

	enable_extint();

	printf("DMA test code for guest memcpy\n");

	test_dma_memcpy();
}
