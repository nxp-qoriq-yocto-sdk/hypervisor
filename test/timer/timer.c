
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
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile int fit_count, dec_count;
static volatile int dont_clear;

#define FP	38

void fit_handler(trapframe_t *frameptr)
{
	printf("got FIT interrupt %u\n", ++fit_count);

	if (!dont_clear) {
		mtspr(SPR_TSR, TSR_FIS);
	} else {
		dont_clear = 0;
	}
}

void dec_handler(trapframe_t *frameptr)
{
	printf("got DEC interrupt %u\n", ++dec_count);

	if (!dont_clear) {
		mtspr(SPR_TSR, TSR_DIS);
	} else {
		dont_clear = 0;
	}
}

static unsigned long period_to_ticks(unsigned int period)
{
	return 2UL << period;
}

static void delay(unsigned long ticks)
{
	unsigned long start = mfspr(SPR_TBL);

	while (mfspr(SPR_TBL) - start < ticks)
		;
}

/* Delay for a time equivalent to the expected interval
 * for a fit of the given period.
 */
static void delay_period(unsigned int period)
{
	return delay(period_to_ticks(period));
}

static void wait_for_interrupt(volatile int *count, unsigned int c)
{
	int target = *count + c;

	while (*count < target)
		;
}

static int wait_for_tsr(uint32_t tsr_mask, unsigned long timeout)
{
	unsigned long start = mfspr(SPR_TBL);

	while (!(mfspr(SPR_TSR) & tsr_mask)) {
		if (mfspr(SPR_TBL) - start >= timeout)
			break;
	}

	return mfspr(SPR_TSR) & tsr_mask;
}

static void fit_wait(unsigned int period)
{
	if (wait_for_tsr(TSR_FIS, period_to_ticks(period))) {
		if (!(mfspr(SPR_TBL) & (1UL << period)))
			printf("FAILED: FIS set with timebase bit clear\n");
	} else {
		printf("FAILED: FIS is not set\n");
	}
}

static void dec_wait(unsigned long ticks)
{
	if (wait_for_tsr(TSR_DIS, ticks)) {
		if (mfspr(SPR_DEC) != 0)
			printf("FAILED: DIS set with unexpired decrementer\n");
	} else {
		printf("FAILED: DIS is not set\n");
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int prev;

	// Wait for the HV to stop printing text.  This will reduce byte
	// channel switching.
	delay_period(24);

	init(devtree_ptr);

	printf("Fixed Interval Timer test\n");

	enable_extint();

	printf("Test 1: Keep FIE disabled, wait for FIS to get set\n");
	mtspr(SPR_TCR, TCR_INT_TO_FP(FP));
	fit_wait(63 - FP);
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 2: Wait for three timer interrupts\n");
	mtspr(SPR_TCR, TCR_FIE | TCR_INT_TO_FP(FP));
	wait_for_interrupt(&fit_count, 3);
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 3: Disable FIE, wait for FIS to get set\n");
	// Disable FIE
	mtspr(SPR_TCR, TCR_INT_TO_FP(FP));
	fit_wait(63 - FP);
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);

	printf("Test 4: Clear FIS, Enable FIE, disable EE, wait for FIS to get set\n");
	// Clear FIS and disable interrupts but re-enable FIE
	disable_extint();
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_FIS);
	mtspr(SPR_TCR, TCR_FIE | TCR_INT_TO_FP(FP));
	fit_wait(63 - FP);

	// Enable interrupts.  We should get an interrupt immediately
	// Don't clear FIS on the first interrupt, to test level-triggered
	// delivery.
	printf("Test 5: Enable EE, wait for interrupt and reassertion\n");
	prev = fit_count;
	dont_clear = 1;
	enable_extint();
	if (fit_count != prev + 2) {
		printf("FAILED: got %d interrupt(s) instead of two\n",
		       fit_count - prev);
	}

	printf("Test 6: Wait for one more interrupt and reassertion\n");
	prev = fit_count;
	dont_clear = 1;
	delay_period(63 - FP);
	if (fit_count != prev + 2) {
		printf("FAILED: got %d interrupt(s) instead of two\n",
		       fit_count - prev);
	}

	// Test decrementer
	printf("Test 7: Decrementer and reassertion\n");
	prev = dec_count;
	dont_clear = 1;
	mtspr(SPR_DEC, 100000);
	mtspr(SPR_TCR, TCR_DIE);
	delay(100000);
	if (dec_count != prev + 2) {
		printf("FAILED: got %d interrupt(s) instead of two\n",
		       dec_count - prev);
	}

	// Test decrementer auto-reload
	printf("Test 8: Test decrementer auto-reload\n");
	prev = dec_count;
	mtspr(SPR_DECAR, 100000);
	mtspr(SPR_DEC, 1000);
	mtspr(SPR_TCR, TCR_DIE | TCR_ARE);
	delay(301000);
	if (dec_count != prev + 4) {
		printf("FAILED: got %d interrupt(s) instead of four\n",
		       dec_count - prev);
	}

	wait_for_interrupt(&dec_count, 3);

	printf("Test 9: Disable DIE, wait for DIS to get set\n");
	mtspr(SPR_TCR, 0);
	mtspr(SPR_DEC, 100000);
	dec_wait(100000);

	printf("Test 10: Clear DIS, Enable DIE, disable EE, wait for DIS to get set\n");
	disable_extint();
	mtspr(SPR_TCR, TCR_DIE);
	mtspr(SPR_TSR, TSR_DIS);
	mtspr(SPR_DEC, 100000);
	dec_wait(100000);

	// Enable interrupts.  We should get an interrupt immediately
	// Don't clear DIS on the first interrupt, to test level-triggered
	// delivery.
	printf("Test 11: Enable EE, wait for interrupt and reassertion\n");
	prev = dec_count;
	dont_clear = 1;
	enable_extint();
	if (dec_count != prev + 2) {
		printf("FAILED: got %d interrupt(s) instead of two\n",
		       dec_count - prev);
	}

	// Stop everything
	mtspr(SPR_TCR, 0);
	disable_extint();

	printf("Test Complete\n");
}
