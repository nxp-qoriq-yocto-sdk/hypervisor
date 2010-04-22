
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


#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile int count = 0;

#define FP	38

void fit_handler(trapframe_t *frameptr)
{
	printf("got FIT interrupt %u\n", ++count);
	mtspr(SPR_TSR, TSR_FIS);
}

/* Delay for a time equivalent to the expected interval
 * for a fit of the given period.
 */
static void delay(unsigned int period)
{
	uint32_t start = mfspr(SPR_TBL);

	while (mfspr(SPR_TBL) - start < (2 << period))
		;
}

static void wait_for_interrupt(unsigned int c)
{
	unsigned int target = count + c;
	
	while (count < target) {
		fh_idle();

		/* Should not see more than one of these in a row,
		 * without an intervening interrupt message --
		 * eventually put this in check-results.
		 */
		printf("woke from idle\n");
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	// Wait for the HV to stop printing text.  This will reduce byte
	// channel switching.
	delay(24);

	init(devtree_ptr);

	printf("Fixed Interval Timer test\n");

	enable_extint();

	printf("Test 1: Keep FIE disabled, wait for FIS to get set\n");
	mtspr(SPR_TCR, TCR_INT_TO_FP(FP));
	delay(63 - FP);
	if (!(mfspr(SPR_TSR) & TSR_FIS))
		printf("FAILED: FIS is not set\n");
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 2: Wait for three timer interrupts\n");
	mtspr(SPR_TCR, TCR_FIE | TCR_INT_TO_FP(FP));
	wait_for_interrupt(3);
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 3: Disable FIE, wait for FIS to get set\n");
	// Disable FIE
	mtspr(SPR_TCR, TCR_INT_TO_FP(38));
	delay(63 - FP);
	if (!(mfspr(SPR_TSR) & TSR_FIS))
		printf("FAILED: FIS is not set\n");
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 4: Clear FIS, Enable FIE, disable EE, wait for FIS to get set\n");
	// Clear FIS and disable interrupts but re-enable FIE
	disable_extint();
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);
	mtspr(SPR_TCR, TCR_FIE | TCR_INT_TO_FP(FP));
	delay(63 - FP);
	if (!(mfspr(SPR_TSR) & TSR_FIS))
		printf("FAILED: FIS is not set\n");

	// Enable interrupts.  We should get an interrupt immediately
	printf("Test 5: Enable EE, wait for interrupt\n");
	enable_extint();
	
	// Stop everything
	mtspr(SPR_TCR, 0);
	disable_extint();

	printf("Test Complete\n");
}
