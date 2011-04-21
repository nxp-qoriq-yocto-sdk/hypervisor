
/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
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
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>
#include <hvtest.h>
#include <libfdt.h>

#define PME_PROCESSOR_CYCLES 1
#define PME_INSTRUCTIONS_COMPLETED 2

static void pm_start(void);
static uint32_t pm_get_control_a(uint8_t counter);
static void pm_set_counter(uint8_t counter, uint32_t val);

volatile uint32_t int_trigger = 0;

#define INTERRUPT_TRIGGER_PERIOD 0x100

void perfmon_handler(trapframe_t *frameptr)
{
	uint32_t period = 0x80000000 - INTERRUPT_TRIGGER_PERIOD; 

	pm_set_counter(0, period);

	int_trigger ++;

	/* restart the counting */
	pm_start();

}

/*
 * Sets counter control register A
 */
static void pm_set_control_a(uint8_t counter, uint32_t val)
{

	switch (counter) {
	case 0:
		mtpmr(PMR_PMLCA0, val);
		break;
	case 1:
		mtpmr(PMR_PMLCA1, val);
		break;
	case 2:
		mtpmr(PMR_PMLCA2, val);
		break;
	case 3:
		mtpmr(PMR_PMLCA3, val);
		break;
	}
}

/*
 * Sets performance monitor global control.
 */
static void pm_set_global_control(uint32_t val)
{
	mtpmr(PMR_PMGC0, val);
}
/*
 * Reads a counter value
 */
static uint32_t pm_get_counter(uint8_t counter)
{
	uint32_t ret = 0;

	switch (counter) {
	case 0:
		ret = mfpmr(PMR_PMC0);
		break;
	case 1:
		ret = mfpmr(PMR_PMC1);
		break;
	case 2:
		ret = mfpmr(PMR_PMC2);
		break;
	case 3:
		ret = mfpmr(PMR_PMC3);
		break;
	}
	return ret;
}

/*
 * Sets a counter value
 */
static void pm_set_counter(uint8_t counter, uint32_t val)
{

	switch (counter) {
	case 0:
		mtpmr(PMR_PMC0, val);
		break;
	case 1:
		mtpmr(PMR_PMC1, val);
		break;
	case 2:
		mtpmr(PMR_PMC2, val);
		break;
	case 3:
		mtpmr(PMR_PMC3, val);
		break;
	}
}

/*
 * Configures a specified counter to count the occurrences of
 * the specified event
 */
static void pm_set_event(uint8_t counter, uint8_t event, uint32_t value)
{
	pm_set_control_a(counter, (pm_get_control_a(counter) & ~PMLCA_EVENT) | (uint32_t)(event << PMLCA_EVENT_SHIFT));
	pm_set_counter(counter, value);
}

/*
 * Starts all counters
 */
static void pm_start(void)
{
	mtpmr(PMR_PMGC0, mfpmr(PMR_PMGC0) & ~PMGC0_FAC);
}

/*
 * Freezes all counters
 */
static void pm_stop(void)
{
	mtpmr(PMR_PMGC0, mfpmr(PMR_PMGC0) | PMGC0_FAC);

}
static uint32_t pm_get_control_a(uint8_t counter)
{
	uint32_t ret = 0;

	switch (counter) {
	case 0:
		ret = mfpmr(PMR_PMLCA0);
		break;
	case 1:
		ret = mfpmr(PMR_PMLCA1);
		break;
	case 2:
		ret = mfpmr(PMR_PMLCA2);
		break;
	case 3:
		ret = mfpmr(PMR_PMLCA3);
		break;
	}
	return ret;
}


/*
 * Performs hardware init - currently it only stops counters so they can be
 * configured
 */
static void pm_init(void)
{
	int i;
	/* set general control counter - freeze all counters, enable perfmon interrupt */
    mtpmr(PMR_PMGC0, PMGC0_FAC | PMGC0_PMIE | PMGC0_FCECE);

	/* reset counters */
	for(i = 0; i < 4; i++) {
		pm_set_event(i, 0, 0);
		pm_set_control_a(i, PMLCA_FCM0 | PMLCA_CE);
	}

}


static void test_perfmon(void)
{
	int i;
	uint32_t cnt_val;
	uint32_t period = 0;
	int loopcnt = 0;
	int test_events = DBCR0_DAC2W;

	enable_extint();

	pm_init();

	/* activate PMM in MSR */
	mtmsr(mfmsr() | MSR_PMM);

	period = 0x80000000 - INTERRUPT_TRIGGER_PERIOD;
	pm_set_event(0, PME_INSTRUCTIONS_COMPLETED, period);

	pm_set_event(1, PME_PROCESSOR_CYCLES, 0);

	pm_start();

	for(i = 0; i < INTERRUPT_TRIGGER_PERIOD * 100; i++)
		asm(" nop");

	pm_stop();

	cnt_val = pm_get_counter(0);
	if (cnt_val == 0)
		printf("FAILED\n");

	cnt_val = pm_get_counter(1);
	printf("%s: %d\n", "PME_PROCESSOR_CYCLES", cnt_val);
	if (cnt_val == 0)
		printf("FAILED\n");

	printf("Interrupt was triggered %d times \n", int_trigger);

	if (int_trigger < 2) 
		printf("FAILED\n");

	printf("Test Complete\n");

}
void libos_client_entry(unsigned long devtree_ptr)
{
	printf("Perfmon test:\n");

	init(devtree_ptr);

	test_perfmon();
}

