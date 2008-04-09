
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

extern void init(unsigned long);
volatile int extint_cnt;
int *handle_p_int;
extern void *fdt;

void ext_int_handler(trapframe_t *frameptr)
{
	extint_cnt++;
	fh_vmpic_eoi(*handle_p_int);

}

int test_init(void)
{
	int len;
	int off = -1, ret;

	ret = fdt_check_header(fdt);
	if (ret)
		return -1;
	ret = fdt_node_offset_by_compatible(fdt, off, "fsl,doorbell-receive");
	if (ret == -FDT_ERR_NOTFOUND)
		return ret;
	if (ret < 0)
		return ret;
	off = ret;
	handle_p_int = (int *)fdt_getprop(fdt, off, "interrupts", &len);

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p_int, 1, 15, 0x00000001);
	fh_vmpic_set_mask(*handle_p_int, 0);
	fh_vmpic_set_priority(*handle_p_int, 0);
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
