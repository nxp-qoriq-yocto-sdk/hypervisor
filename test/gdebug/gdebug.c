
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>

void init(unsigned long devtree_ptr);
extern void *fdt;
int saved_trap_eip, saved_icmp_eip;

void guest_debug_handler(trapframe_t *frameptr)
{
	switch (mfspr(SPR_DBSR)) {
		case DBCR0_TRAP:
			saved_trap_eip = frameptr->srr0;
			frameptr->srr0 += 4;
			break;
		case DBCR0_ICMP:
			saved_icmp_eip = frameptr->srr0;
			/* disable single-stepping */
			mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_ICMP);
	}
}

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("Guest Debug test:\n");

	/* Test notes :
	 * Enable both TRAP & ICMP debug events, as the NIA is at a s/w trap
	 * instruction, hence TRAP event will take precedence over
	 * single-step, note the faulted eip at that event. After rdfi, the
	 * next event should be ICMP, compare saved eip to our computed
	 * value and print test results.
	 */

	mtmsr(mfmsr() & ~MSR_DE);
	mtspr(SPR_DBCR0, DBCR0_IDM | DBCR0_TRAP | DBCR0_ICMP);

	printf("Testing Trap Debug event : ");

	mtmsr(mfmsr() | MSR_DE);
	
	asm volatile ("twge %r2, %r2");

	printf("Passed\n");

	printf("Testing ICMP Debug event : ");

	if (saved_trap_eip + 8 == saved_icmp_eip )
		printf("Passed\n");
	
	printf("Test Complete\n");
}
