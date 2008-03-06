
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/mpic.h>
#include <libos/8578.h>
#include <libos/io.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libfdt.h>

extern void init(unsigned long);

int extint_cnt = 0;;
void ext_int_handler(trapframe_t *frameptr)
{
	uint8_t c;

	extint_cnt++;

	printf("ext int\n");

	c = in8((uint8_t *)(CCSRBAR_VA+0x11d500));
}

int test_init(unsigned long devtree_ptr)
{
	int ret;
	int node;
	int *handle_p;
	void *fdt = (void *)devtree_ptr;
	const char *s;
	const char *path;
	int len;
	int i;

	ret = fdt_check_header(fdt);
	if (ret)
		return -1;

	/* look up the stdout alias */

        ret = fdt_subnode_offset(fdt, 0, "aliases");
        if (ret < 0)
                return ret;
        path = fdt_getprop(fdt, ret, "stdout", &ret);
        if (!path)
                return -1;

	/* get the interrupt handle for the serial device */
        node = fdt_path_offset(fdt, path);
	handle_p = (int *)fdt_getprop(fdt, node, "interrupts", &len);

	/* VMPIC config */
	fh_vmpic_set_int_config(*handle_p,1,15,0);
	fh_vmpic_set_mask(*handle_p, 0);
	fh_vmpic_set_priority(*handle_p, 0);

	/* enable interrupts at the UART */
	out8((uint8_t *)(CCSRBAR_VA+0x11d501),0x1);

	/* enable interrupts at the CPU */
	enable_extint();

	return 0;
}

void dump_dev_tree(unsigned long devtree_ptr)
{
	void *fdt = (void *)devtree_ptr;
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

void start(unsigned long devtree_ptr)
{
	uint32_t status;
	char *str;
	uint32_t channel;
	uint32_t x;
	uint8_t c;
	int rc;

	init(devtree_ptr);

	printf("vmpic test\n");

	dump_dev_tree(devtree_ptr);

	rc = test_init(devtree_ptr);
	if (rc) {
		printf("error: test init failed\n");
	}

	printf("press keys to generate an ext int\n");
	while (1) {
		c = in8((uint8_t *)(CCSRBAR_VA+0x11d502));
		if (extint_cnt > 1) {
			break;
		}
	}

	printf("done\n");


}


