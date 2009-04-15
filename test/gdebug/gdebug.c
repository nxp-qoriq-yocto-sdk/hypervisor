
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
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>
#include <hvtest.h>

static int saved_trap_eip, saved_icmp_eip;
static volatile int loopcnt, loopcnt_monitor;
static uint32_t dac2_write_hit_pc, data_watchpoint_cmp;

void debug_handler(trapframe_t *frameptr)
{
	register_t val = mfspr(SPR_DBSR);

	switch (val) {
		case DBCR0_TRAP:
			saved_trap_eip = frameptr->srr0;
			frameptr->srr0 += 4;
			break;
		case DBCR0_ICMP:
			saved_icmp_eip = frameptr->srr0;
			/* disable single-stepping */
			mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_ICMP);
			break;
		case DBCR0_IAC1:
			loopcnt_monitor = 1;
			mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_IAC1);
			break;
		case DBCR0_DAC2W:
			dac2_write_hit_pc = frameptr->srr0;
			mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_DAC2W);
			break;
		case DBCR0_BRT:
			frameptr->srr0 += 4;
	}

	mtspr(SPR_DBSR, val);
}

static inline uint32_t get_pc(void)
{
	register uint32_t retval asm("r3") = 0;

	asm ("mflr %%r0;"
	     "bl dummy;"
	     "dummy: ;"
	     "mflr %0;"
	     "mtlr %%r0;" : "=r" (retval) : :"r0","memory" );

	return retval;
}

static inline int infinite_loop(void)
{
	asm volatile ("1: b 1b;" : : );
	return 0;
}

static void hwbreaktestfunc(void)
{
	if (loopcnt_monitor) {
		data_watchpoint_cmp = get_pc();
		loopcnt = 0;
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int test_events = DBCR0_TRAP | DBCR0_ICMP | DBCR0_IAC1 | DBCR0_DAC2W;

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
	mtspr(SPR_DBCR0, DBCR0_IDM | test_events);

	printf("Testing Trap Debug event : ");

	mtmsr(mfmsr() | MSR_DE);
	
	asm volatile ("twge %r2, %r2");

	printf("PASSED\n");

	printf("Testing ICMP Debug event : ");

	if (saved_trap_eip + 8 == saved_icmp_eip )
		printf("PASSED\n");

	printf("Testing IAC1/IAC2 Debug event : ");
	loopcnt = 1;
	mtspr(SPR_DBCR1, 0);
	mtspr(SPR_DBCR2, 0);
	mtspr(SPR_IAC1, (unsigned long) &hwbreaktestfunc);
	mtspr(SPR_DAC2, (unsigned long) &loopcnt);

	while (loopcnt)
		hwbreaktestfunc();

	printf("PASSED\n");

	printf("Testing DAC1/DAC2 Debug event : ");
	if (dac2_write_hit_pc == 8 + data_watchpoint_cmp)
		printf("PASSED\n");

	printf("Testing Branch taken (BRT) Debug event : ");

	mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) | DBCR0_BRT);

	if (!infinite_loop()) {
		mtspr(SPR_DBCR0, mfspr(SPR_DBCR0) & ~DBCR0_BRT);
		printf("PASSED\n");
	}

	printf("Test Complete\n");
}
