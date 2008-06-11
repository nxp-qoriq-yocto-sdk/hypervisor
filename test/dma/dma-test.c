
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

#define DMA_CHAN_BSY 0x00000004

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
	int count = 0x100;
	unsigned int gpa_src = 0x0;
	unsigned int gpa_dst = 0x100;
	unsigned char *gva_src, *gva_dst;
	int i;

	printf("DMA memcpy test begins : ");

	/* basic DMA direct mode test */
	out32((uint32_t *)(CCSRBAR_VA+0x100100), 0x00000404);
	out32((uint32_t *)(CCSRBAR_VA+0x100120), count); /* count */
	out32((uint32_t *)(CCSRBAR_VA+0x100110), 0x00050000); /* read,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100118), 0x00050000); /* write,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100114), gpa_src);
	out32((uint32_t *)(CCSRBAR_VA+0x10011c), gpa_dst);

	while (in32((uint32_t *)(CCSRBAR_VA+0x100104)) & DMA_CHAN_BSY);

	gva_src = (unsigned char *)(gpa_src + PHYSBASE);
	gva_dst = (unsigned char *)(gpa_dst + PHYSBASE);

	if (!memcmp(gva_src, gva_dst, count))
		printf("passed\n");
	else
		printf("failed\n");

	return 0;
}


void start(unsigned long devtree_ptr)
{
	int node = -1;
	int ret, len;
	unsigned long *liodnp;

	init(devtree_ptr);

	dump_dev_tree();

	enable_extint();

	printf("DMA test code for guest memcpy\n");

	ret = fdt_node_offset_by_compatible(fdt, node, "fsl,mpc8578-dma");
	if (ret == -FDT_ERR_NOTFOUND) {
		printf("fdt_node_offset failed, NOT FOUND\n");
		return;
	}
	if (ret < 0) {
		printf("fdt_node_offset failed = %d\n", ret);
		return;
	}
	node = ret;

	liodnp = fdt_getprop_w(fdt, node, "fsl,liodn", &len);
	if (!liodnp)
		printf("fsl,liodn property not found\n");

	printf("liodn = %ld\n", *liodnp);
	fh_dma_enable(*liodnp);

	test_dma_memcpy();
}
