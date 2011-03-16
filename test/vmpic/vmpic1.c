
/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/mpic.h>
#include <libos/io.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/interrupts.h>
#include <libos/alloc.h>
#include <libfdt.h>
#include <hvtest.h>

static const uint32_t *handle_p;

static volatile int extint_cnt = 0;

static phys_addr_t addr;

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int vector;
	int rc;
	unsigned int mask;

	rc = ev_int_iack(0, &vector);

	if (coreint) {
		if (rc != EV_INVALID_STATE)
			printf("iack had bad rc: FAILED\n");

		vector = mfspr(SPR_EPR);
	} else {
		if (rc != 0)
			printf("iack had bad rc: FAILED\n");
	}

	if (vector == *handle_p) {
		extint_cnt++;
		ev_int_set_mask(*handle_p, 1);
		/* Make sure we are masked now */
		ev_int_get_mask(*handle_p, &mask);
		if (!mask)
			printf("Unexpected behavior : Interrupt not masked\n");
	} else {
		printf("Unexpected extint %u\n", vector);
	}

	out32(((uint32_t *)((uintptr_t)addr + MPIC_EOI)), 0);
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
		printf("   node = %s\n", s);
	}
	printf("------\n");
#endif
}

extern uint8_t *uart_virt;

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret;
	int node;
	const char *path;
	int len;

	init(devtree_ptr);

	printf("End Of Interrupt with no hcall test ");
	if (coreint)
		printf("- coreint mode\n");
	else
		printf("- legacy mode\n");

	dump_dev_tree();

	node = fdt_path_offset(fdt, "/devices/uart2");
	if (node < 0) {
		printf("no /devices/uart2: BROKEN\n");
		return;
	}

	if (!test_init_uart(node)) {
		printf("Can't init /devices/uart2: BROKEN\n");
		return;
	}

	handle_p = fdt_getprop(fdt, node, "interrupts", &len);

	if (handle_p[1] & IRQ_TYPE_MPIC_DIRECT) {
		int off = -1;

		node = fdt_subnode_offset(fdt, 0, "devices");
		if (node < 0) {
			printf("no devices node: FAILED\n");
			return;
		}

		node = fdt_subnode_offset(fdt, node, "vmpic");
		if (node < 0) {
			printf("no vmpic node: FAILED\n");
			return;
		}

		ret = node;
		ret = fdt_node_check_compatible(fdt, ret,
						"fsl,hv-mpic-per-cpu");
		if (ret < 0) {
			printf("vmpic node is not mpic-per-cpu compatible:"
				       "FAILED\n");
			return;
		}

		off = node;
		const uint32_t *reg = fdt_getprop(fdt, off, "reg", &len);
		if (!reg) {
			printf("no reg property in vmpic node: FAILED\n");
			return;
		}

		/* assuming address_cells = 2 */
		addr |= ((uint64_t) *reg) << 32;
		reg++;
		addr |= *reg;
		tlb1_set_entry(1, (unsigned long)addr, (phys_addr_t)addr,
				TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KERN,
				0, 0, 0);
	}

	ev_int_set_mask(*handle_p, 0);

	/* VMPIC config */
	ev_int_set_config(*handle_p, 0x3 , 15, 0);

	/* enable TX interrupts at the UART */
	out8(&uart_virt[1], 0x2);

	/* enable interrupts at the CPU */
	enable_extint();

	
	/* wait for the first interrupt and then unmask the interrupts to
	   receive the second one */
	while (extint_cnt <= 0)
		;

	ev_int_set_mask(*handle_p, 0);
	out8(&uart_virt[1], 0x2);

	while (extint_cnt <= 1)
		;

	printf(" > got external interrupts: PASSED\n");

	printf("Test Complete\n");

}



