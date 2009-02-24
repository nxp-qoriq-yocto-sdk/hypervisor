
/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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

void init(unsigned long devtree_ptr);

extern void *fdt;

#define UART 1
#define DMA 2

volatile int exception;

void mcheck_interrupt(trapframe_t *frameptr)
{

#ifdef DEBUG
	printf("machine check exception\n");
#endif
	exception++;
	frameptr->srr0 += 4;
}

void check_dma1_node(int node)
{
	const uint32_t *prop;
	int len;

	prop = fdt_getprop(fdt, node, "fsl,liodn", &len);
	printf("delete-prop --- %s\n",
		 (prop == NULL)? "PASSED" : "FAILED");
	node = fdt_subnode_offset(fdt, node, "dma-channel");
	printf("delete-subnodes --- %s\n",
		(node < 0)? "PASSED" : "FAILED");
	/* checking reg resources are not assigned to the guest */
	in32((uint32_t *)(CCSRBAR_VA + 0x110304));
	printf("delete-property reg --- %s\n",
		(exception)? "PASSED" : "FAILED");
}

void start(unsigned long devtree_ptr)
{
	int node, ret, compat_found = 0;
	int last_node = 0;

	init(devtree_ptr);

	printf("node-update test\n");

	node = fdt_path_offset(fdt, "/dma0");
	printf("delete-node --- %s\n",
			(node < 0)? "PASSED" : "FAILED");

	node = -1;

	while (1) {
		ret = fdt_node_offset_by_compatible(fdt, node, "addr-space");
		if (ret < 0) {
			if (compat_found != 2) {
				printf("prepend-list --- FAILED\n");
				if (last_node != DMA) {
					node = fdt_path_offset(fdt, "/dma1");
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

	printf("Test Complete\n");
}
