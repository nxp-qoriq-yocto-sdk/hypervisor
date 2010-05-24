
/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

#define debug(X...)
//#define debug(X...) printf(X)

static const uint32_t *irq1, *irq2;
static uint32_t handle[2];

#define BC_INT_Q_SIZE 4096

static int process_status(int status)
{
	switch (status) {
	case 0 :
		return 0;
	case EV_EINVAL :
		printf("Invalid parameter\n");
		return -1;
	case EV_EAGAIN :
		printf("byte channel buffer full\n");
		return 0;
	default :
		printf("unknown error condition\n");
		return -1;
	}
	return 0;
}

static volatile int rx_intr_state = 0;

static void process_rx_intr(uint32_t handle_num)
{
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
	int ret;
	char buf[BC_INT_Q_SIZE*2];
	unsigned int cnt, count = 0;


	status = ev_byte_channel_poll(handle_num, &rxavail, &txavail);
	ret = process_status(status);
	if (ret < 0)
		return;
	if (rxavail > 0) {
		while (1) {
			cnt = 16;
			status = ev_byte_channel_receive(handle_num, &cnt, &buf[count]);
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

	if (coreint)
		vector = mfspr(SPR_EPR);
	else
		ev_int_iack(&vector);

	debug("external interrupt ... vector  %d\n", vector);
	if (vector == irq1[0]) {
		debug("byte channel A rx interrupt\n");
		process_rx_intr(handle[0]);
		debug("\n");
	} else if (vector == irq1[2]) {
		debug("byte channel A tx interrupt\n");
		status = ev_byte_channel_poll(handle[0], &rxavail, &txavail);
		ret = process_status(status);
		if (ret < 0)
			return;
		debug("tx avail =%d\n", txavail);
		debug("\n");
	} else if (vector == irq2[0]) {
		debug("byte channel B rx interrupt\n");
		process_rx_intr(handle[1]);
		debug("\n");
	} else if (vector == irq2[2]) {
		debug("byte channel B tx interrupt\n");
		status = ev_byte_channel_poll(handle[1], &rxavail, &txavail);
		ret = process_status(status);
		if (ret < 0)
			return;
		debug("tx avail =%d\n", txavail);
	}
	ev_int_eoi(vector);

}

static int get_prop(const char *byte_channel,
                    const char *prop, const uint32_t **ptr)
{
	int ret;
	int len;

	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	ret = fdt_path_offset(fdt, byte_channel);
	if (ret < 0)
		return ret;

	*ptr = fdt_getprop(fdt, ret, prop, &len);

	return 0;
}

static int test_partial_write(uint32_t rhandle, uint32_t shandle,
                              unsigned int to_send, unsigned int expected_to_send)
{
	uint32_t status;
	char buf[16];
	const char *str = "byte-channel:-A!";	/* 16 chars*/

	status = ev_byte_channel_receive(rhandle, &expected_to_send, buf);

	status = ev_byte_channel_send(shandle, &to_send, str);

	if (status || to_send != expected_to_send) {
		printf("ERROR: expected to send %d char, actual "
		       "sent was %d \n", expected_to_send, to_send);
		return 1;
	}

	printf(" > Partial write (%d char): PASSED\n", to_send);
	return 0;
}

static void dump_dev_tree(void)
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

void libos_client_entry(unsigned long devtree_ptr)
{
	const char *str;
	uint32_t status;
	uint32_t rxavail;
	uint32_t txavail;
	int ret;
	const uint32_t *prop;
	unsigned int avail, bal, to_send, cnt, total_sent;
	char buf[16];

	init(devtree_ptr);

	dump_dev_tree();

	enable_extint();

	printf("Byte channel test\n");

	ret = get_prop("/hypervisor/handles/byte-channelA", "reg", &prop);
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

	ret = get_prop("/hypervisor/handles/byte-channelB", "reg", &prop);
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
	    get_prop("/hypervisor/handles/byte-channelA", "interrupts",
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
	    get_prop("/hypervisor/handles/byte-channelB", "interrupts",
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

	debug("byte-channel A irqs = %d %d\n", irq1[0], irq1[2]);
	debug("byte-channel B irqs = %d %d\n", irq2[0], irq2[2]);

	/* set int config for byte channel A */
	if ((status = ev_int_set_config(irq1[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("ev_int_set_config failed for byte channel A rxint\n");
		goto bad;
	}
	if ((status = ev_int_set_config(irq1[2], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("ev_int_set_config failed for byte channel A txint\n");
		goto bad;
	}
	/* set int config for byte channel B */
	if ((status = ev_int_set_config(irq2[0], 0, 0, 0x00000001))) {/* set int to cpu 0 */
		printf("ev_int_set_config failed for byte channel B rxint\n");
		goto bad;
	}
	if ((status = ev_int_set_config(irq2[2], 0, 0, 0x00000001)))	{/* set int to cpu 0 */
		printf("ev_int_set_config failed for byte channel B txint\n");
		goto bad;
	}

	str = "byte-channel:-A!";	/* 16 chars*/
	status = ev_byte_channel_poll(handle[0], &rxavail, &txavail);
	if (txavail == BC_INT_Q_SIZE-1) {
		printf(" > Polled byte channel A...expected and got %d: PASSED\n",txavail);
	} else {
		printf("Byte channel A txavail not empty before send start ---> FAILED\n");
		goto bad;
	}

	total_sent = 0;
	while (1) {
		to_send = 16;
		status = ev_byte_channel_send(handle[0], &to_send, str);
		total_sent += to_send;

		if (status != 0)
			break;
	}

	if (total_sent == txavail)
		printf(" > Completed send: PASSED\n");
	else
		printf(" > Error: only sent %d bytes: FAILED\n", total_sent);

	status = ev_byte_channel_poll(handle[0], &rxavail, &txavail);

	if (txavail == 0)
		printf(" > Polling. Expected txavail = 0 & rc = EAGAIN, byte channel full: PASSED\n");
	else
		printf("Txavail = %d != 0 but rc = EAGAIN incorrect --->FAILED\n", txavail);

	debug("enabling byte channel A tx intr and byte channel B rx intr\n");

	if ((status = ev_int_set_mask(irq1[2], 0))) {/* enable byte channel A txintr */
		printf("ev_int_set_mask failed for byte channel A txint\n");
		disable_extint();
		goto bad;
	}

	if ((status = ev_int_set_mask(irq2[0], 0))) {/* enable byte channel B rxintr */
		printf("ev_int_set_mask failed for byte channel B rxint\n");
		disable_extint();
		goto bad;
	}

	str = "byte-channel:-B!";	/*16 chars*/
	status = ev_byte_channel_poll(handle[1], &rxavail, &txavail);

	if (txavail == BC_INT_Q_SIZE-1) {
		printf(" > Polled byte channel B...expected and got tx %d: PASSED\n",txavail);
	} else {
		printf("Byte channel B txavail not empty before send start ---> FAILED\n");
		goto bad;
	}

	total_sent = 0;
	while (1) {
		to_send = 16;
		status = ev_byte_channel_send(handle[1], &to_send, str);
		total_sent += to_send;

		if (status != 0)
			break;
	}

	if (total_sent == txavail)
		printf(" > Completed send: PASSED\n");
	else
		printf(" > Error: only sent %d bytes: FAILED\n", total_sent);


	status = ev_byte_channel_poll(handle[1], &rxavail, &txavail);

	if (txavail == 0)
		printf(" > Polling. Expected txavail = 0 & rc = EAGAIN, byte channel full: PASSED\n");
	else
		printf("Txavail = %d != 0 but rc = EAGAIN incorrect --->FAILED\n", txavail);

	debug("enabling byte channel B tx intr and byte channel A rx intr\n");
	if ((status = ev_int_set_mask(irq2[2], 0))) {/* enable byte channel B txintr */
		printf("ev_int_set_mask failed for byte channel B txint\n");
		disable_extint();
		goto bad;
	}
	debug("\n");
	if ((status = ev_int_set_mask(irq1[0], 0))) {/* enable byte channel A rxintr */
		printf("ev_int_set_mask failed for byte channel A rxint\n");
		disable_extint();
		goto bad;
	}

	while (rx_intr_state < 2);

	/* partial write unit test */

	/* mask all bc interrupts */
	status = ev_int_set_mask(irq1[0], 1);
	status += ev_int_set_mask(irq1[2], 1);
	status += ev_int_set_mask(irq2[0], 1);
	status += ev_int_set_mask(irq2[2], 1);
	if (status) {
		printf("ERROR: mask failed\n");
		goto bad;
	}

	status = ev_byte_channel_poll(handle[0], &rxavail, &txavail);
	if (rxavail) {
		printf("ERROR: byte-channel not empty \n");
		goto bad;
	}

	/* fill up byte-channel */
	to_send = 1;
	str = "byte-channel:-A!";	/* 16 chars*/
	while (ev_byte_channel_send(handle[0], &to_send, str) != EV_EAGAIN);

	/* test write of 16 bytes w/ 1 byte free */
	if (test_partial_write(handle[1], handle[0], 16, 1))
		goto bad;

	/* test write of 16 bytes w/ 15 bytes free */
	if (test_partial_write(handle[1], handle[0], 16, 15))
		goto bad;

	/* test write of 16 bytes w/ 16 bytes free */
	if (test_partial_write(handle[1], handle[0], 16, 16))
		goto bad;

	/* test write of 0 bytes */
	if (test_partial_write(handle[1], handle[0], 0, 0))
		goto bad;

	printf("Test Complete\n");

	return;

bad :
	printf("Fatal error, can't continue test failed status = %d\n", status);
}
