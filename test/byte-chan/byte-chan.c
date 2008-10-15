
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */



#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);

#define debug(X...)
//#define debug(X...) printf(X)

int *irq1, *irq2;
uint32_t handle[2];
extern void *fdt;

#define BC_INT_Q_SIZE 4096

int process_status(int status)
{
	switch (status) {
	case 0 :
		return 0;
	case EINVAL :
		printf("Invalid parameter\n");
		return -1;
	case EAGAIN :
		printf("byte channel buffer full\n");
		return 0;
	default :
		printf("unknown error condition\n");
		return -1;
	}
	return 0;
}

volatile int rx_intr_state= 0;

void process_rx_intr(uint32_t handle_num)
{
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
	int ret;
	char buf[BC_INT_Q_SIZE*2];
	unsigned int cnt, count = 0;


	status = fh_byte_channel_poll(handle_num, &rxavail, &txavail);
	ret = process_status(status);
	if (ret < 0)
		return;
	if (rxavail > 0) {
		while (1) {
			cnt = 16;
			status = fh_byte_channel_receive(handle_num, &cnt, &buf[count]);
			ret = process_status(status);
			if (ret < 0)
				return;
			if (cnt == 0) {
				debug("Received %d bytes, byte channel status = empty\n", count);
				if (count == rxavail)
					printf(" > Expected bytes =%d, received bytes =%d: PASSED\n", rxavail, count);
				else
					printf("Expected bytes =%d, received bytes =%d--FAILED\n", rxavail, count);
				break;
			}
			count += cnt;
		}
#ifdef DEBUG
		printf("---Receive data start---\n");
		for (i = 0; i < count; i++) {
			printf("%c", buf[i]);
		}
		printf("\n");
		printf("---Receive data end---\n");
#endif
	} else {
		printf("no data to read in byte channel rx intr \n");
	}

	rx_intr_state++;
}

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
	int ret;

	fh_vmpic_iack(&vector);
	debug("external interrupt ... vector  %d\n", vector);
	if (vector == irq1[0]) {
		debug("byte channel 1 rx interrupt\n");
		process_rx_intr(handle[0]);
		debug("\n");
	} else if (vector == irq1[2]) {
		debug("byte channel 1 tx interrupt\n");
		status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);
		ret = process_status(status);
		if (ret < 0)
			return;
		debug("tx avail =%d\n", txavail);
		debug("\n");
	} else if (vector == irq2[0]) {
		debug("byte channel 2 rx interrupt\n");
		process_rx_intr(handle[1]);
		debug("\n");
	} else if (vector == irq2[2]) {
		debug("byte channel 2 tx interrupt\n");
		status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);
		ret = process_status(status);
		if (ret < 0)
			return;
		debug("tx avail =%d\n", txavail);
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
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("node = %s\n", s);
	}
	printf("------\n");
#endif
}

void start(unsigned long devtree_ptr)
{
	char *str;
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
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

	debug("byte-channel 1 irqs = %d %d\n", irq1[0], irq1[2]);
	debug("byte-channel 2 irqs = %d %d\n", irq2[0], irq2[2]);

	/* set int config for byte channel 1 */
	if ((status = fh_vmpic_set_int_config(irq1[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 1 rxint\n");
		goto bad;
	}
	if ((status = fh_vmpic_set_int_config(irq1[2], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 1 txint\n");
		goto bad;
	}
	/* set int config for byte channel 2 */
	if ((status = fh_vmpic_set_int_config(irq2[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 2 rxint\n");
		goto bad;
	}
	if ((status = fh_vmpic_set_int_config(irq2[2], 0, 0, 0x00000001)))	{/* set int to cpu 0 */
		printf("fh_vmpic_set_int_config failed for byte channel 2 txint\n");
		goto bad;
	}

	str = "byte-channel:-A!";	/* 16 chars*/
	status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);
	if (txavail == BC_INT_Q_SIZE-1) {
		printf(" > Polled byte channel A...expected and got %d: PASSED\n",txavail);
	} else {
		printf("Byte channel A txavail not empty before send start ---> FAILED\n");
		goto bad;
	}
	avail = txavail / 16;
	bal = txavail % 16;
	while (1) {
		if (avail > 0) {
			status = fh_byte_channel_send(handle[0], 16, str);
			--avail;
		} else
			status = fh_byte_channel_send(handle[0], bal, str);

		if (status == EAGAIN) 
			break;
	}

	if (avail == 0)
		printf(" > Completed send: PASSED\n");
	else
		printf(" > Error avail != 0: FAILED\n");

	status = fh_byte_channel_poll(handle[0], &rxavail, &txavail);

	if (txavail == 0)
		printf(" > Polling. Expected txavail = 0 & rc = EAGAIN, byte channel full: PASSED\n");
	else
		printf("Txavail = %d != 0 but rc = EAGAIN incorrect --->FAILED\n", txavail);

	debug("enabling byte channel 1 tx intr and byte channel 2 rx intr\n");

	if ((status = fh_vmpic_set_mask(irq1[2], 0))) {/* enable byte channel A txintr */
		printf("fh_vmpic_set_mask failed for byte channel 1 txint\n");
		disable_extint();
		goto bad;
	}

	if ((status = fh_vmpic_set_mask(irq2[0], 0))) {/* enable byte channel B rxintr */
		printf("fh_vmpic_set_mask failed for byte channel 2 rxint\n");
		disable_extint();
		goto bad;
	}

	str = "byte-channel:-B!";	/*16 chars*/
	status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);

	if (txavail == BC_INT_Q_SIZE-1) {
		printf(" > Polled byte channel B...expected and got tx %d: PASSED\n",txavail);
	} else {
		printf("Byte channel B txavail not empty before send start ---> FAILED\n");
		goto bad;
	}


	avail = txavail / 16;
	bal = txavail % 16;
	while (1) {
		if (avail > 0) {
			status = fh_byte_channel_send(handle[1], 16, str);
			--avail;
		} else
			status = fh_byte_channel_send(handle[1], bal, str);

		if (status == EAGAIN) 
			break;
	}

	if (avail == 0)
		printf(" > Completed send: PASSED\n");
	else
		printf(" > Error avail != 0: FAILED\n");

	status = fh_byte_channel_poll(handle[1], &rxavail, &txavail);

	if (txavail == 0)
		printf(" > Polling. Expected txavail = 0 & rc = EAGAIN, byte channel full: PASSED\n");
	else
		printf("Txavail = %d != 0 but rc = EAGAIN incorrect --->FAILED\n", txavail);

	debug("enabling byte channel 2 tx intr and byte channel 1 rx intr\n");
	if ((status = fh_vmpic_set_mask(irq2[2], 0))) {/* enable byte channel 2 txintr */
		printf("fh_vmpic_set_mask failed for byte channel 2 txint\n");
		disable_extint();
		goto bad;
	}
	debug("\n");
	if ((status = fh_vmpic_set_mask(irq1[0], 0))) {/* enable byte channel 1 rxintr */
		printf("fh_vmpic_set_mask failed for byte channel 1 rxint\n");
		disable_extint();
		goto bad;
	}

	while (rx_intr_state < 2);

	printf("Test Complete\n");

	return;

bad :
	printf("Fatal error, can't continue test failed status = %d\n", status);
}
