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

#include <libos/alloc.h>
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

#define PAGE_SIZE 4096

#define UART 1
#define DMA 2

static volatile int exception;

void mcheck_interrupt(trapframe_t *frameptr)
{

#ifdef DEBUG
	printf("machine check exception\n");
#endif
	exception++;
	frameptr->srr0 += 4;
}

static int uart_node;

static void check_dma1_node(int node)
{
	const uint32_t *prop;
	const uint32_t *reg;
	phys_addr_t uart_phys;
	int len;

	uint32_t *dma_virt;
	phys_addr_t dma_phys;

	prop = fdt_getprop(fdt, node, "fsl,liodn", &len);
	printf("delete-prop --- %s\n",
		 (prop == NULL)? "PASSED" : "FAILED");
	node = fdt_subnode_offset(fdt, node, "dma-channel");
	printf("delete-subnodes --- %s\n",
		(node < 0)? "PASSED" : "FAILED");

	/* checking reg resources are not assigned to the guest */
	reg = fdt_getprop(fdt, node, "reg", &len);
	printf("delete-property reg (property) --- %s\n",
	       reg ? "FAILED" : "PASSED");

	if (dt_get_reg(fdt, uart_node, 0, &uart_phys, NULL) < 0) {
		printf("Can't get uart reg -- FAILED\n");
		return;
	}

	dma_phys = (uart_phys & ~0xffffffULL) + 0x101300;

	dma_virt = valloc(4096, 4096);
	if (!dma_virt) {
		printf("valloc failed --- BROKEN\n");
		return;
	}
	
	dma_virt = (void *)dma_virt + (dma_phys & (PAGE_SIZE - 1));

	tlb1_set_entry(4, (uintptr_t)dma_virt, dma_phys,
	               TLB_TSIZE_4K, TLB_MAS2_IO,
	               TLB_MAS3_KERN, 0, 0, 0, 0);
	
	in32(dma_virt);
	printf("delete-property reg (access) --- %s\n",
		(exception)? "PASSED" : "FAILED");
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int node, ret, compat_found = 0;
	int dma1, bar;
	uint32_t dma1ph, serialph, barph, memph;
	const uint32_t *prop;
	int last_node = 0;

	init(devtree_ptr);

	printf("node-update test\n");

	uart_node = fdt_node_offset_by_compatible(fdt, -1, "ns16550");
	if (uart_node < 0) {
		printf("no serial node --- FAILED\n");
		goto out;
	}

	node = fdt_path_offset(fdt, "/devices/dma0");
	printf("delete-node --- %s\n",
			(node < 0)? "PASSED" : "FAILED");

	dma1 = fdt_path_offset(fdt, "/devices/dma1");
	if (dma1 < 0) {
		printf("dma1 does not exist --- FAILED\n");
		goto out;
	}

	dma1ph = fdt_get_phandle(fdt, dma1);
	if (!dma1ph)
		printf("dma1 no phandle --- FAILED\n");

	node = -1;

	while (1) {
		ret = fdt_node_offset_by_compatible(fdt, node, "addr-space");
		if (ret < 0) {
			if (compat_found != 2) {
				printf("prepend-list --- FAILED\n");
				if (last_node != DMA) {
					node = fdt_path_offset(fdt, "/devices/dma1");
					check_dma1_node(node);
				}
			} else {
				printf("prepend-list --- PASSED\n");
			}

			break;
		} else {
			compat_found++;
			node = ret;
			ret = fdt_node_check_compatible(fdt, node, "ns16550");
			if (ret == 0) {
				last_node = UART;
			} else {
				ret = fdt_node_check_compatible(fdt, node, "fsl,p4080-dma");
				if (ret == 0) {
					last_node = DMA;
					check_dma1_node(node);
				}
			}
		}
	}

	serialph = fdt_get_phandle(fdt, uart_node);
	if (!serialph)
		printf("serial no phandle --- FAILED\n");

	prop = fdt_getprop(fdt, uart_node, "fooph", &ret);
	if (!prop || ret != 4) {
		printf("no fooph --- FAILED\n");
		goto out;
	}

	if (*prop != serialph)
		printf("serialph %x serial/fooph %x --- FAILED\n", serialph, *prop);

	prop = fdt_getprop(fdt, uart_node, "barph", &ret);
	if (!prop || ret != 4) {
		printf("no barph --- FAILED\n");
		goto out;
	}

	bar = fdt_subnode_offset(fdt, uart_node, "bar");
	if (bar < 0) {
		printf("no bar --- FAILED\n");
		goto out;
	}

	barph = fdt_get_phandle(fdt, bar);
	if (*prop != barph)
		printf("bar phandle %x serial/barph %x --- FAILED\n", barph, *prop);

	prop = fdt_getprop(fdt, uart_node, "dmaph", &ret);
	if (!prop || ret != 4) {
		printf("no barph --- FAILED\n");
		goto out;
	}

	if (*prop != dma1ph)
		printf("dma1 phandle %x serial/dma1ph %x --- FAILED\n", dma1ph, *prop);

	prop = fdt_getprop(fdt, bar, "blah", &ret);
	if (!prop || ret != 5 || strcmp((const char *)prop, "test"))
		printf("serial/bar, blah has bad value --- FAILED\n");

	prop = fdt_getprop(fdt, 0, "ph", &ret);
	if (!prop || ret != 12) {
		printf("bad/missing ph --- FAILED\n");
		goto out;
	}

	if (prop[0] != dma1ph)
		printf("dma1 phandle %x ph[0] %x --- FAILED\n", dma1ph, prop[0]);
	if (prop[1] !=  barph)
		printf("bar phandle %x ph[0] %x --- FAILED\n", serialph, prop[1]);

	node = fdt_subnode_offset(fdt, 0, "memory");
	if (node < 0) {
		printf("no memory --- FAILED\n");
		goto out;
	}

	memph = fdt_get_phandle(fdt, node);
	if (prop[2] != memph)
		printf("gpma phandle %x ph[0] %x --- FAILED\n", memph, prop[2]);

	prop = fdt_getprop(fdt, dma1, "serialph", &ret);
	if (!prop || ret != 4) {
		printf("no dma1/serialph --- FAILED\n");
		goto out;
	}
	
	if (*prop !=  serialph)
		printf("serial phandle %x dma1/serialph %x --- FAILED\n",
		       serialph, *prop);

	printf("node-update-phandle --- PASSED\n");

out:
	printf("Test Complete\n");
}
