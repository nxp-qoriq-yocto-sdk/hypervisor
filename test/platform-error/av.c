/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
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
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

#define DMA_CHAN_BSY 0x00000004
#define PAGE_SIZE 4096

void init(unsigned long devtree_ptr);

static phys_addr_t dma_phys;
static uint32_t *dma_virt;
volatile int mcheck_int, crit_int;
uint32_t mcheck_err[8], crit_err[16];

void crit_int_handler(trapframe_t *regs)
{
	uint32_t *ptr;

	if (!crit_int)
		ptr = crit_err;
	else
		ptr = &crit_err[8];

	int ret = fh_err_get_info(1, ptr);
	if (!ret)
		crit_int++;
}

void mcheck_interrupt(trapframe_t *regs)
{
	if (!(mfspr(SPR_MCSR) & MCSR_MCP))
		return;

	int ret = fh_err_get_info(0, mcheck_err);
	if (!ret)
		mcheck_int++;

	mtspr(SPR_MCSR, mfspr(SPR_MCSR));
}

static int dma_init(void)
{
	int node, parent, len;
	uint32_t liodn;
	const uint32_t *liodnp;

	node = fdt_node_offset_by_compatible(fdt, 0, "fsl,eloplus-dma-channel");
	if (node < 0)
		return -1;

	if (dt_get_reg(fdt, node, 0, &dma_phys, NULL) < 0)
		return -2;

	parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		return -3;

	dma_virt = valloc(4096, 4096);
	if (!dma_virt)
		return -4;

	dma_virt = (void *)dma_virt + (dma_phys & (PAGE_SIZE - 1));

	tlb1_set_entry(4, (uintptr_t)dma_virt, dma_phys,
	               TLB_TSIZE_4K, TLB_MAS2_IO,
	               TLB_MAS3_KERN, 0, 0, 0);

	/* FIXME: reset all channels of DMA block before enabling liodn */

	liodnp = fdt_getprop(fdt, parent, "fsl,hv-dma-handle", &len);
	if (!liodnp) {
		printf("fsl,hv-dma-handle property not found %d\n", len);
		return -5;
	}

	liodn = *liodnp;
	printf("actual liodn = %d\n", liodn);

	fh_dma_enable(liodn);
	if (liodn != 0) {
		printf("fh_dma_enable: failed %d\n", liodn);
		return -6;
	}

	return 0;
}

static int test_dma_memcpy(phys_addr_t gpa_src, phys_addr_t gpa_dst,
                           int do_memset)
{
	int count = 0x100;
	unsigned char *gva_src, *gva_dst;

	gva_src = (unsigned char *)(uintptr_t)(gpa_src + PHYSBASE);
	if (do_memset)
		memset(gva_src, 0x5A, count);

	/* basic DMA direct mode test */
	out32(&dma_virt[0], 0x00000404);
	out32(&dma_virt[8], count); /* count */
	out32(&dma_virt[4], 0x00050000); /* read,snoop */
	out32(&dma_virt[6], 0x00050000); /* write,snoop */
	out32(&dma_virt[5], gpa_src);
	out32(&dma_virt[7], gpa_dst);

	while (in32(&dma_virt[1]) & DMA_CHAN_BSY);

	gva_dst = (unsigned char *)(uintptr_t)(gpa_dst + PHYSBASE);

	return (!memcmp(gva_src, gva_dst, count));
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret, i, len;

	init(devtree_ptr);

	enable_extint();
	enable_critint();
	enable_mcheck();

	printf("DMA test code for guest memcpy\n");

	ret = dma_init();
	if (ret < 0) {
		printf("dma_init failed = %d\n", ret);
		return;
	}

	/* test access violation failure and pass cases */
	if (!test_dma_memcpy(0, 0x0e000000, 0));

	const char *label = fdt_getprop(fdt, 0, "label", &len);

	while (!mcheck_int);
	for(i = 0; i < 8; i++)
		printf("%x, ", mcheck_err[i]);
	printf("\n");

	printf("Access violation test : PASSED\n");

	if(!strcmp("/part1", label)) {
		while (crit_int < 2 );
		for(i = 0 ; i < 16 ; i++)
			printf("%x, ", crit_err[i]);
		printf("\n");
		printf("Error Manager test : PASSED\n");
	}

	printf("Test Complete\n");
}
