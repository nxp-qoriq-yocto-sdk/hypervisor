
/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
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


/*
 * shared tlb test program
 */

#include <libos/libos.h>
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trap_booke.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

#define PAGE_SHIFT		12
#define PRIV_INSTR_VA_BASE 	0x80000000

extern uint32_t priviledged_instr;

static __attribute__ ((noinline)) void do_priviledged_instr(void)
{
	asm volatile("mfspr %%r0, 1015;" : : : "r0");
}

static void create_mapping(void *vaddr, phys_addr_t paddr)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(0) | MAS0_ESEL(3));
	mtspr(SPR_MAS1, MAS1_VALID | (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT) |
	                (0 << MAS1_TID_SHIFT) | (0 << MAS1_TS_SHIFT));
	mtspr(SPR_MAS2, ((register_t)vaddr) | MAS2_M);
	mtspr(SPR_MAS3, (uint32_t)paddr | MAS3_SR | MAS3_SW | MAS3_SX);
	mtspr(SPR_MAS7, (uint32_t)(paddr >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

void *vaddr;
phys_addr_t paddr;
uint64_t misses;

static void tlb_miss(trapframe_t *frameptr)
{
	uintptr_t va;
	register_t miss_addr;

	miss_addr = frameptr->exc == EXC_ITLB ? frameptr->srr0 : frameptr->dear;
	va = (uintptr_t)vaddr & ~((1 << PAGE_SHIFT) - 1);

	misses++;

	if (miss_addr >= va && miss_addr < (va + 4096))
		create_mapping(vaddr, paddr);
	else
		printf("%s: %d miss at 0x%lx outside (0x%p, 0x%p)\n",
			__func__, frameptr->exc, miss_addr, vaddr, vaddr + 4096 - 1);
}

void dtlb_handler(trapframe_t *frameptr)
{
	tlb_miss(frameptr);
}

void itlb_handler(trapframe_t *frameptr)
{
	tlb_miss(frameptr);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	const char *label;
	int len;
	uint64_t loops = 0;
	void (* fn_ptr)(void) = NULL;

	init(devtree_ptr);

	printf("\n\nShared TLB test\n");

	label = fdt_getprop(fdt, 0, "label", &len);
	if (!label || !len) {
		printf("Error reading partition label\n");
		return;
	}

	asm volatile("tlbsx 0, %0" : : "r" (do_priviledged_instr));

	vaddr = (void *)((((uintptr_t)do_priviledged_instr) & ~((1 << PAGE_SHIFT) - 1)));
	vaddr += (PRIV_INSTR_VA_BASE - PHYSBASE);

	paddr = ((uint64_t)mfspr(SPR_MAS7) << 32) | (mfspr(SPR_MAS3) & MAS3_RPN);
	paddr += ((uintptr_t)vaddr - PRIV_INSTR_VA_BASE) & ~((1 << PAGE_SHIFT) - 1);

	fn_ptr = vaddr +
		(((uintptr_t)do_priviledged_instr) & ((1 << PAGE_SHIFT) - 1));

	printf("Original Priviledged instr. at 0x%p remapped at 0x%p\n",
	       &do_priviledged_instr, fn_ptr);

	if (!strcmp(label, "/part1")) {
		while (1) {
			((uint32_t *)vaddr)[42] = 0xdeadbeef;
			if (!((loops++) % 1000000))
				printf("part1: loop %lld - %lld misses\n", loops, misses);
		}
	} else {
		while (1) {
			fn_ptr();
			if (!((loops++) % 1000000))
				printf("part2: loop %lld - %lld misses\n", loops, misses);
		}
	}
}
