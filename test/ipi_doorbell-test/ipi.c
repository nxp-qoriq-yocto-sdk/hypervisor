
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);
int extint_cnt;
int *handle_p_int;
extern void *fdt;

void ext_int_handler(trapframe_t *frameptr)
{
	extint_cnt++;
	fh_vmpic_eoi(*handle_p_int);

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

	handle_p = get_handle("fsl,doorbell-send-handle", "reg", fdt);

	if (!handle_p) {
		printf("Couldn't get send doorbell handle\n");
		return -1;
	}

	fh_partition_send_dbell(*handle_p);

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

	printf("ipi_doorbell-p1 test\n");

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
