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
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

static int failed, exception;

#define TLB0 0
#define TLB1 1

void dsi_handler(trapframe_t *frameptr)
{
	if (mfspr(SPR_ESR) & ESR_ST) {
		asm volatile("tlbsx 0, %0" : : "r" (mfspr(SPR_DEAR)) : "memory");
		if (!(mfspr(SPR_MAS1) & MAS1_VALID)) {
			printf("TLBSX failed for address %x\n", (uint32_t)mfspr(SPR_DEAR));
			failed = 1;
			goto out;
		} else {
		#ifdef DEBUG
			printf("DSI exception while writing at address %x, tlb %d \n",
					(uint32_t)mfspr(SPR_DEAR),
					(uint32_t)((mfspr(SPR_MAS0) >> 28) & 0xf));
		#endif
			mtspr(SPR_MAS1, mfspr(SPR_MAS1) & ~MAS1_VALID);
			asm volatile("tlbwe" : : : "memory");
		}
	} else {
		printf("invalid ESR bit set %x\n", (uint32_t)mfspr(SPR_ESR));
		failed = 1;
	}
out:
	exception = 1;
	frameptr->srr0 += 4;
}

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa, int tsize)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | MAS2_M);
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW | MAS3_SX);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

static void check_result(int tlb)
{
	if (exception) {
		if (!failed) {
			printf(" > DSI exception for %s -- PASSED\n",
					tlb ? "TLB1" : "TLB0");
		} else {
			failed = 0;
			printf("FAILED");
		}
		exception = 0;
	} else {
		printf("No DSI exception for %s -- FAILED\n",
				tlb ? "TLB1" : "TLB0");
	}
}

static void gpma_test(void)
{
	void *arr;

	arr = valloc(4 * 1024, 4 * 1024);
	if (!arr) {
		printf("valloc failed \n");
		return;
	}

	create_mapping(TLB0, 0, arr, (phys_addr_t) 0x26000000, TLB_TSIZE_4K);
	*(char *)arr = 1;
	check_result(TLB0);

	create_mapping(TLB1, 1, arr, (phys_addr_t) 0x26000000, TLB_TSIZE_4K);
	*(char *)arr = 1;
	check_result(TLB1);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("read-only-gpma test\n");
	gpma_test();

	printf("Test Complete\n");
}
