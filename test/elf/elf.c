
/*
 * Copyright (C) 2009, 2010 Freescale Semiconductor, Inc.
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
#include <libos/epapr_hcalls.h>
#include <libos/fsl_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

extern uint32_t text_start;
int failed;

static inline uintptr_t get_pc(void)
{
	register uintptr_t retval asm("r3") = 0;

	asm ("mflr %%r0;"
	     "bl 1f;"
	     "1: mflr %0;"
	     "mtlr %%r0;" : "=r" (retval) : :"r0","memory" );

	return retval;
}

static void far_call(void) __attribute__ ((section (".faraway")));
static void far_call(void)
{
	printf("call @ %#lx\n", get_pc());
}

static void very_far_call(void) __attribute__ ((section (".veryfaraway")));
static void very_far_call(void)
{
	printf("call @ %#lx\n", get_pc());
}

static void call_function(void (*f)(void), int positive)
{
	uintptr_t pfunc;

#ifndef CONFIG_LIBOS_64BIT
	pfunc = (uintptr_t)f;
#else
	pfunc = *(uint64_t *)f;
#endif

	/* Use tlbsx to locate the TLB entry that maps the function w/ AS=0. */
	mtspr(SPR_MAS6, (mfspr(SPR_PID) << MAS6_SPID_SHIFT) | 0);
	isync();
	asm volatile("tlbsx 0, %0" : : "r" (pfunc));

	if (mfspr(SPR_MAS1) & MAS1_VALID) {
		f();
		return;
	}

	if (positive) {
		printf("FAILED to find a proper TLB entry\n");
		failed = 1;
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("elf non zero load address test\n");

	call_function(far_call, 1);
	call_function(very_far_call, 0);	/* should fail to load, being outside memory */

	if (!failed && get_pc() > (uintptr_t)&text_start)
		printf("PASSED\n");
	else
		printf("FAILED\n");

	printf("Test Complete\n");
}
