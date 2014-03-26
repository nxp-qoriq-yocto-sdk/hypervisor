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
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile int data_exception, inst_exception;

void mcheck_interrupt(trapframe_t *frameptr)
{

#ifdef DEBUG
	printf("machine check interrupt\n");
#endif
	if (mfspr(SPR_MCSR) & MCSR_ST) {
		data_exception++;
		mtspr(SPR_MCSR, mfspr(SPR_MCSR));
		frameptr->srr0 += 4;
	}

	if (mfspr(SPR_MCSR) & MCSR_IF) {
		inst_exception++;
		mtspr(SPR_MCSR, mfspr(SPR_MCSR));
		frameptr->srr0 = frameptr->lr;
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	char *vaddr;

	init(devtree_ptr);

	enable_extint();

	printf("MCHECK test\n");


	vaddr = valloc(4 * 1024, 4 * 1024);
	if (!vaddr) {
		printf("valloc failed \n");
		return;
	}

	/* create tlb entry for an invalid guest physical */
	tlb1_set_entry(2, (unsigned long)vaddr, 0x20000000, TLB_TSIZE_4K,
	               MAS1_IPROT, TLB_MAS2_IO, TLB_MAS3_KERN, 0, 0);
	*vaddr = 'a';

	/* make sure that the data_exception is seen after the access */
	sync();

	if (data_exception)
		printf("Machine check exception generated for illegal data address -- PASSED\n");
	else
		printf("Machine check - data test --- FAILED\n");

	/* Make the function call in asm, to avoid the C function descriptors */
	__asm__ __volatile__ ("mtctr %0; bctrl;"
		: : "r" (vaddr)
		: EV_HCALL_CLOBBERS1);

	if (inst_exception)
		printf("Machine check exception generated for illegal instruction address-- PASSED\n");
	else
		printf("Machine check - illegal instruction test -- FAILED\n");

	printf("Test Complete\n");
}
