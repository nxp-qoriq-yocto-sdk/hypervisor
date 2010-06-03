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

#include <libos/alloc.h>
#include <libos/epapr_hcalls.h>
#include <libos/fsl_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/platform_error.h>
#include <libfdt.h>
#include <hvtest.h>

#define DMA_CHAN_BUSY 0x00000004
#define DMA_END_OF_SEGMENT 0x00000002
#define PAGE_SIZE 4096

void init(unsigned long devtree_ptr);

static phys_addr_t dma_phys;
static uint32_t *dma_virt;
static uint32_t liodn;
static int window_test, sub_window_test;
static volatile int mcheck_int, crit_int;
static hv_error_t mcheck_err, crit_err[2];

void crit_int_handler(trapframe_t *regs)
{
	hv_error_t *ptr;
	uint32_t bufsize = sizeof(hv_error_t);

	if (!crit_int)
		ptr = &crit_err[0];
	else
		ptr = &crit_err[1];

	int ret = fh_err_get_info(global_error_queue, &bufsize, 0, virt_to_phys(ptr), 0);
	if (!ret)
		crit_int++;
}

void mcheck_interrupt(trapframe_t *regs)
{
	uint32_t bufsize = sizeof(hv_error_t);

	if (!(mfspr(SPR_MCSR) & MCSR_MCP))
		return;

	int ret = fh_err_get_info(guest_error_queue, &bufsize, 0, virt_to_phys(&mcheck_err), 0);
	if (!ret)
		mcheck_int++;

	mtspr(SPR_MCSR, mfspr(SPR_MCSR));
}

static int dma_init(void)
{
	int node, parent, len;
	const uint32_t *liodnp;
	int rc;

	node = fdt_node_offset_by_compatible(fdt, 0, "window");
	if (node >= 0)
		window_test = 1;

	node = fdt_node_offset_by_compatible(fdt, 0, "subwindow");
	if (node >= 0)
		sub_window_test = 1;

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

	rc = fh_dma_enable(liodn);
	if (rc) {
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
	uint32_t stat;

	gva_src = (unsigned char *)(uintptr_t)(gpa_src + PHYSBASE);
	gva_dst = (unsigned char *)(uintptr_t)(gpa_dst + PHYSBASE);

	if (do_memset) {
		for (int i = 0; i < count; i++)
			gva_src[i] = i;
		memset(gva_dst, 0xeb, count);
	}

	/* basic DMA direct mode test */
	out32(&dma_virt[1], in32(&dma_virt[1])); /* clear old status */
	out32(&dma_virt[0], 0x00000644);
	out32(&dma_virt[8], count); /* count */
	out32(&dma_virt[4], 0x00050000); /* read,snoop */
	out32(&dma_virt[6], 0x00050000); /* write,snoop */
	out32(&dma_virt[5], gpa_src);
	out32(&dma_virt[7], gpa_dst);

	while (1) {
		stat = in32(&dma_virt[1]);
		stat &= ~DMA_CHAN_BUSY;

		if (stat & ~DMA_END_OF_SEGMENT) {
			printf("stat %x\n", stat);
			return 0;
		}
		if (stat & DMA_END_OF_SEGMENT)
			break;
	}

	return (!memcmp(gva_src, gva_dst, count));
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret, len, i, node;

	init(devtree_ptr);

	if (init_error_queues() < 0)
		return;

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
	if (!test_dma_memcpy(0, 0x0e000000, 0))
		printf("DMA access violation test#1 : PASSED\n");
	else
		printf("DMA access violation test#1 : FAILED\n");

	while (!mcheck_int);
	printf("domain: %s, error: %s, path: %s\n", mcheck_err.domain, mcheck_err.error,
		 mcheck_err.hdev_tree_path);
	printf("PAMU access violation avs1 = %x, avs2 = %x, av_addr = %llx\n",
		 mcheck_err.pamu.avs1, mcheck_err.pamu.avs2, mcheck_err.pamu.access_violation_addr);
	printf("PAMU access violation lpid = %x, handle = %x\n",
		mcheck_err.pamu.lpid, mcheck_err.pamu.liodn_handle);
	printf("\n");
	printf("Access violation test : PASSED\n");
	printf("\n");

	const char *label = fdt_getprop(fdt, 0, "label", &len);
	if(!strcmp("/part1", label)) {
		while (crit_int < 2 );
		for(i = 0 ; i < 2 ; i++) {
			printf("domain: %s, error: %s, path: %s\n", crit_err[i].domain, crit_err[i].error,
				crit_err[i].hdev_tree_path);
			printf("PAMU access violation avs1 = %x, avs2 = %x, av_addr = %llx\n",
				crit_err[i].pamu.avs1, crit_err[i].pamu.avs2, crit_err[i].pamu.access_violation_addr);
			printf("PAMU access violation lpid = %x, handle = %x\n",
				crit_err[i].pamu.lpid, crit_err[i].pamu.liodn_handle);
		}
		printf("\n");
		printf("Error Manager test : PASSED\n");
		printf("\n");

		/* PPAACE entry correponding to access violation LIODN is marked
		 * invalid by the hypervisor. We need to explicitly mark the entry
		 * as valid using the hcall.
		 */
		ret = fh_dma_enable(liodn);
		if (ret) {
			printf("fh_dma_enable: failed %d\n", liodn);
			return;
		}
		/*
		 * create a hard-coded TLB1 entry for PAMU address-translation
		 * verification test
		 */

		tlb1_set_entry(3, (unsigned long)(0x20000000 + PHYSBASE),
			(phys_addr_t)0x20000000, TLB_TSIZE_4K, 0,
			TLB_MAS3_KERN, 0, 0, 0);
		if (window_test) {
			if (test_dma_memcpy(0x04000115, 0x05000127, 1))
				printf("DMA access test : PASSED\n");
			else
				printf("DMA access test : FAILED\n");
		}

		if (sub_window_test) {
			if (test_dma_memcpy(0x04000115, 0x0c00012e, 1) &&
				test_dma_memcpy(0x04000130, 0x20000111, 1))
				printf("DMA access test : PASSED\n");
			else
				printf("DMA access test : FAILED\n");
		}
	}
	printf("Test Complete\n");
}
