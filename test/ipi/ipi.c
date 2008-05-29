
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>

#undef DEBUG

extern void init(unsigned long devtree_ptr);
int extint_cnt;
int *handle_p_int;
extern void *fdt;
extern int coreint;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int irq;
	
	if (coreint)
		irq = mfspr(SPR_EPR);
	else
		fh_vmpic_iack(&irq);
	
	if (irq != *handle_p_int)	
		printf("Unknown extirq %d\n", irq);
	else
		extint_cnt++;

	fh_vmpic_eoi(irq);
}

void ext_doorbell_handler(trapframe_t *frameptr)
{
	printf("Doorbell\n");
}

void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
	printf("Critical doorbell\n");
}

/* Writes data to shared memory region with partition 2 */
void wr_shm(void)
{
	int ret, off = -1;
	phys_addr_t addr = 0;
	void *vaddr;
	int len;
	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,share-mem-test");
	if (ret == -FDT_ERR_NOTFOUND) {
		printf("fdt_node_offset failed, NOT FOUND\n");
		return;
	}
	if (ret < 0) {
		printf("fdt_node_offset failed = %d\n", ret);
		return;
	}
	off = ret;
	const uint32_t *reg = fdt_getprop(fdt, off, "reg", &len);
	if (!reg)
		printf("get prop failed for reg in share =%d\n", len);
	/* assuming address_cells = 2 and size_cells = 1 */
	addr |= ((uint64_t) *reg) << 32;
	reg++;
	addr |= *reg;
#ifdef DEBUG
	printf("shared memory phys addr = %llx\n", addr);
#endif
	vaddr = valloc(4 * 1024, 4 * 1024);
	if (!vaddr) {
		printf("valloc failed \n");
		return;
	}
	tlb1_set_entry(1, (unsigned long)vaddr, addr, TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KERN, 0, 0, 0);
	memcpy(vaddr, "hello", strlen("hello") + 1);
#ifdef DEBUG
	printf("written '%s' to shared memory\n", (unsigned char *)vaddr);
#endif
}

int *get_handle(const char *dbell_type, const char *prop, void *fdt)
{
	int off = -1, ret;
	int len;

	ret = fdt_check_header(fdt);
	if (ret)
		return NULL;
	ret = fdt_node_offset_by_compatible(fdt, off, dbell_type);

	if (ret == -FDT_ERR_NOTFOUND)
		return NULL;
	if (ret < 0)
		return NULL;

	off = ret;

	return (int *)fdt_getprop(fdt, off, prop, &len);
}

int test_init(void)
{
	int ret;
	int *handle_p;

	handle_p_int = get_handle("fsl,hv-doorbell-receive-handle", "interrupts", fdt);

	if (!handle_p_int) {
		printf("Couldn't get recv doorbell handle\n");
		return -1;
	}

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p_int, 1, 15, 0x00000001);
	fh_vmpic_set_mask(*handle_p_int, 0);
	 /*VMPIC*/ enable_critint();
	enable_extint();

	handle_p = get_handle("fsl,hv-doorbell-send-handle", "reg", fdt);

	if (!handle_p) {
		printf("Couldn't get send doorbell handle\n");
		return -1;
	}

	fh_partition_send_dbell(*handle_p);

	return 0;
}

void dump_dev_tree(void)
{
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n", s);
	}
	printf("------\n");
#endif
}

void start(unsigned long devtree_ptr)
{
	int rc;

	init(devtree_ptr);

	printf("inter-partition doorbell test #1\n");

	dump_dev_tree();

	wr_shm();

	printf(" > sending doorbell to self...");

	rc = test_init();
	if (rc) {
		printf("error: test init failed\n");
	}

	while (1) {
		if (extint_cnt) {
			printf("got external interrupt: PASSED\n");
			break;
		}
	}

	printf("Test Complete\n");
}