
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

#define DMA_CHAN_BSY 0x00000004

void init(unsigned long devtree_ptr);

extern void *fdt;

void dump_dev_tree(void)
{
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

int test_dma_memcpy(unsigned int gpa_src, unsigned int gpa_dst, int do_memset)
{
	int count = 0x100;
	unsigned char *gva_src, *gva_dst;

	gva_src = (unsigned char *)(gpa_src + PHYSBASE);
	if (do_memset)
		memset(gva_src, 0x5A, count);

	/* basic DMA direct mode test */
	out32((uint32_t *)(CCSRBAR_VA+0x100100), 0x00000404);
	out32((uint32_t *)(CCSRBAR_VA+0x100120), count); /* count */
	out32((uint32_t *)(CCSRBAR_VA+0x100110), 0x00050000); /* read,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100118), 0x00050000); /* write,snoop */
	out32((uint32_t *)(CCSRBAR_VA+0x100114), gpa_src);
	out32((uint32_t *)(CCSRBAR_VA+0x10011c), gpa_dst);

	while (in32((uint32_t *)(CCSRBAR_VA+0x100104)) & DMA_CHAN_BSY);

	gva_dst = (unsigned char *)(gpa_dst + PHYSBASE);

	return (!memcmp(gva_src, gva_dst, count));
}


void start(unsigned long devtree_ptr)
{
	int node = -1;
	int ret;
	int len;
	unsigned long *liodnp;

	init(devtree_ptr);

//	dump_dev_tree();

//	enable_extint();

	printf("DMA test code for guest memcpy\n");

	ret = fdt_node_offset_by_compatible(fdt, node, "fsl,p4080-dma");
	if (ret == -FDT_ERR_NOTFOUND) {
		printf("fdt_node_offset failed, NOT FOUND\n");
		return;
	}
	if (ret < 0) {
		printf("fdt_node_offset failed = %d\n", ret);
		return;
	}
	node = ret;

	liodnp = fdt_getprop_w(fdt, node, "fsl,hv-dma-handle", &len);
	if (!liodnp) {
		printf("fsl,hv-dma-handle property not found\n");
		return;
	}

	fh_dma_enable(*liodnp);

	liodnp = fdt_getprop_w(fdt, node, "fsl,liodn", &len);
	printf("actual liodn = %ld\n", *liodnp);

	/* test access violation failure and pass cases */
	if (!test_dma_memcpy(0, 0x0e000000, 0))
		printf("DMA access violation test#1 : PASSED\n");
	else
		printf("DMA access violation test#1 : FAILED\n");

	if (!test_dma_memcpy(0x00000100, 0x0c000000, 0))
		printf("DMA access violation test#2 : PASSED\n");
	else
		printf("DMA access violation test#2 : FAILED\n");

	if (test_dma_memcpy(0x04000100, 0x0c000100, 1) ||
		test_dma_memcpy(0x04000100, 0x05000100, 1))
		printf("DMA access test : PASSED\n");
	else
		printf("DMA access test : FAILED\n");

	printf("Test Complete\n");
}
