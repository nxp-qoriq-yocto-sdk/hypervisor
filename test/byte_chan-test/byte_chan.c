
#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/spr.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);

int *irq1, *irq2;
uint32_t handle[2];
extern void *fdt;

void process_rx_intr(uint32_t handle_num)
{
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
	int i;
	char buf[512];
	unsigned int cnt, count = 0;
	status = fh_byte_channel_poll(handle_num, &rxavail, &txavail);
	if (rxavail > 0) {
		while (1) {
			cnt = 16;
			status = fh_byte_channel_receive(handle_num, &cnt, &buf[count]);
			if (cnt == 0) {
				printf
				    ("Received %d bytes, byte channel status = empty\n",
				     count);
				if (count == rxavail)
					printf("Expected bytes =%d, received bytes =%d --PASSED\n", rxavail, count);
				else
					printf("Expected bytes =%d, received bytes =%d--FAILED\n", rxavail, count);
				break;
			}
			count += cnt;
		}
		printf("---Receive data start---\n");
		for (i = 0; i < count; i++) {
			printf("%c", buf[i]);
		}
		printf("\n");
		printf("---Receive data end---\n");
	} else {
		printf("no data to read in byte channel rx intr \n");
	}
}

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;

	fh_vmpic_iack(&vector);
	printf("external interrupt ... vector  %d\n", vector);
	if (vector == irq1[0]) {
		printf("byte channel 1 rx interrupt\n");
		process_rx_intr(handle[0]);
		printf("\n");
	} else if (vector == irq1[1]) {
		printf("byte channel 1 tx interrupt\n");
		status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);
		printf("tx avail =%d\n", txavail);
		printf("\n");
	} else if (vector == irq2[0]) {
		printf("byte channel 2 rx interrupt\n");
		process_rx_intr(handle[1]);
		printf("\n");
	} else if (vector == irq2[1]) {
		printf("byte channel 2 tx interrupt\n");
		status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);
		printf("tx avail =%d\n", txavail);
		printf("\n");
	}
	fh_vmpic_eoi(vector);

}

int get_prop(const char *byte_channel, const char *prop, int **ptr)
{
	int ret;
	int len;

	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	ret = fdt_path_offset(fdt, byte_channel);
	if (ret < 0)
		return ret;

	*ptr = (int *)fdt_getprop(fdt, ret, prop, &len);

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
		printf("node = %s\n", s);
	}
	printf("------\n");
}

void start(unsigned long devtree_ptr)
{
	char *str;
	uint32_t status;
	uint32_t save_status;
	uint32_t rxavail;
	uint32_t txavail;
	int i;
	int node = -1;
	int len;
	int ret;
	int *prop;
	int avail, bal;

	init(devtree_ptr);

	dump_dev_tree();

	enable_extint();

	printf("Byte channel test\n");

	ret = get_prop("/handles/byte-channelA", "reg", &prop);
	if (ret) {
		printf("get_prop failed\n");
		return;
	}
	if (prop) {
		handle[0] = *prop;
	} else {
		printf("device tree error\n");
		return;
	}

	ret = get_prop("/handles/byte-channelB", "reg", &prop);
	if (ret) {
		printf("get_prop failed\n");
		return;
	}
	if (prop) {
		handle[1] = *prop;
	} else {
		printf("device tree error\n");
		return;
	}

	ret =
	    get_prop("/handles/byte-channelA", "interrupts",
		     &prop);
	if (ret) {
		printf("get_prop failed\n");
		return;
	}
	if (prop) {
		irq1 = prop;
	} else {
		printf("device tree error\n");
		return;
	}

	ret =
	    get_prop("/handles/byte-channelB", "interrupts",
		     &prop);
	if (ret) {
		printf("get_prop failed\n");
		return;
	}
	if (prop) {
		irq2 = prop;
	} else {
		printf("device tree error\n");
		return;
	}
	printf("byte-channel 1 irqs = %d %d\n", irq1[0], irq1[1]);
	printf("byte-channel 2 irqs = %d %d\n", irq2[0], irq2[1]);

	/* set int config for byte channel 1 */
	if ((status = fh_vmpic_set_int_config(irq1[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 1 rxint\n");
		goto bad;
	}
	if ((status = fh_vmpic_set_int_config(irq1[1], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 1 txint\n");
		goto bad;
	}
	/* set int config for byte channel 2 */
	if ((status = fh_vmpic_set_int_config(irq2[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 2 rxint\n");
		goto bad;
	}
	if ((status = fh_vmpic_set_int_config(irq2[1], 0, 0, 0x00000001)))	{/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 2 txint\n");
		goto bad;
	}

	str = "byte-channel:-A!";	/* 16 chars*/
	status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);
	if (txavail == 255) {
		printf("Byte channel 1 tx space available = 255 before send start ---> PASSED\n");
	} else {
		printf("Byte channel 1 txavail != 255 before send start ---> FAILED\n");
		goto bad;
	}
	printf("\n");
	avail = txavail / 16;
	bal = txavail % 16;
	while (1) {
		if (avail > 0) {
			status = fh_byte_channel_send(handle[0], 16, str);
			--avail;
		} else {
			status = fh_byte_channel_send(handle[0], bal, str);
		}
		if (status == -3) {
			save_status = status;
			printf("Check if transmitted %d bytes on byte channel 1\n",
			       txavail);
			status =
			    fh_byte_channel_poll(handle[0], &rxavail, &txavail);
			if (txavail == 0)
				printf("Expected txavail = 0 & rc =-3 byte channel full --->PASSED\n");
			else
				printf("Txavail = %d != 0 but rc = -3 incorrect --->FAILED\n", txavail);
			status = save_status;
			break;
		}

	}
	if (status != -3) {
		status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);
		printf("Byte channel 1");
		if (txavail != 0)
			printf("Failed to transmit 255 bytes, transmitted %d bytes\n", 255 - txavail);
		else
			printf("Didn't receive FULL status, txavail = 0\n");
		goto bad;
	}
	printf("enabling byte channel 1 tx intr and byte channel 2 rx intr\n");
	if ((status = fh_vmpic_set_mask(irq1[1], 0))) {/* enable byte channel 1 txintr */
		printf("fh_vmpic_set_mask failed for byte channel 1 txint\n");
		disable_extint();
		goto bad;
	}
	if ((status = fh_vmpic_set_mask(irq2[0], 0))) {/* enable byte channel 2 rxintr */
		printf("fh_vmpic_set_mask failed for byte channel 2 rxint\n");
		disable_extint();
		goto bad;
	}
	printf("\n");
	str = "byte-channel:-B!";	/*16 chars*/
	status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);
	if (txavail == 255) {
		printf("Byte channel 2 tx space available = 255 before send start ---> PASSED\n");
	} else {
		printf("Byte channel 2 txavail != 255 before send start ---> FAILED\n");
		goto bad;
	}
	printf("\n");
	avail = txavail / 16;
	bal = txavail % 16;
	while (1) {
		if (avail > 0) {
			status = fh_byte_channel_send(handle[1], 16, str);
			--avail;
		} else {
			status = fh_byte_channel_send(handle[1], bal, str);
		}
		if (status == -3) {
			save_status = status;
			printf("Check if transmitted %d bytes on byte channel 2\n",
			       txavail);
			status =
			    fh_byte_channel_poll(handle[1], &rxavail, &txavail);
			if (txavail == 0)
				printf("Expected txavail = 0 & rc =-3 byte channel full --->PASSED\n");
			else
				printf("Txavail = %d != 0 but rc = -3 incorrect --->FAILED\n", txavail);
			status = save_status;
			break;
		}
	}
	if (status != -3) {
		status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);
		printf("Byte channel 2");
		if (txavail != 0)
			printf("Failed to transmit 255 bytes, transmitted %d bytes\n", 255 - txavail);
		else
			printf("Didn't receive FULL status, txavail = 0\n");
		goto bad;
	}
	printf("enabling byte channel 2 tx intr and byte channel 1 rx intr\n");
	if ((status = fh_vmpic_set_mask(irq2[1], 0))) {/* enable byte channel 2 txintr */
		printf("fh_vmpic_set_mask failed for byte channel 2 txint\n");
		disable_extint();
		goto bad;
	}
	printf("\n");
	if ((status = fh_vmpic_set_mask(irq1[0], 0))) {/* enable byte channel 1 rxintr */
		printf("fh_vmpic_set_mask failed for byte channel 1 rxint\n");
		disable_extint();
		goto bad;
	}
	return;
bad :
	printf("Fatal error, can't continue test failed status = %d\n", status);
}
