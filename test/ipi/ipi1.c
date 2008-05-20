
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>

extern void init(unsigned long);
volatile int extint_cnt;
int *handle_p_int;
extern void *fdt;

/* reads data from shared memory region on interrupt */
void rd_shm(void)
{
	int ret, off = -1;
	phys_addr_t addr = 0;
	void *vaddr;
	int len;
	char buf[20];
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
	printf("shared memory addr = %llx\n", addr);
	vaddr = valloc(4 * 1024, 4 * 1024);
	if (!vaddr) {
		printf("valloc failed \n");
		return;
	}
	tlb1_set_entry(1, (unsigned long)vaddr, addr, TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KERN, 0, 0, 0);
	memcpy(buf, vaddr, strlen("hello") + 1);
	printf("read '%s' from shared memory on interrupt \n", buf);
	printf("expected 'hello', got '%s'\n", buf);
	if (!strcmp(buf, "hello"))
		printf("Shared memory test ---- PASSED\n");
	else
		printf("Shared memory test ---- FAILED\n");
}

void ext_int_handler(trapframe_t *frameptr)
{
	extint_cnt++;
	fh_vmpic_eoi(*handle_p_int);
	printf("Got an external interrupt, reading shared memory now\n");
	rd_shm();
}

void ext_doorbell_handler(trapframe_t *frameptr)
{
	printf("Doorbell\n");
}

void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
	printf("Critical doorbell\n");
}

int test_init(void)
{
	int len;
	int off = -1, ret;

	ret = fdt_check_header(fdt);
	if (ret)
		return -1;
	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-doorbell-receive-handle");
	if (ret == -FDT_ERR_NOTFOUND)
		return ret;
	if (ret < 0)
		return ret;
	off = ret;
	handle_p_int = (int *)fdt_getprop(fdt, off, "interrupts", &len);

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p_int, 1, 15, 0x00000001);
	fh_vmpic_set_mask(*handle_p_int, 0);
	 /*VMPIC*/ enable_critint();
	enable_extint();

	return 0;
}

void dump_dev_tree(void)
{
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n", s);
	}
	printf("------\n");
}

void start(unsigned long devtree_ptr)
{
	int rc;

	init(devtree_ptr);

	printf("ipi_doorbell-p2 test\n");

	dump_dev_tree();

	rc = test_init();
	if (rc) {
		printf("error: test init failed\n");
	}

	while (1) {
		if (extint_cnt) {
			printf("external interrupt\n");
			break;
		}
	}

	printf("done\n");
}
