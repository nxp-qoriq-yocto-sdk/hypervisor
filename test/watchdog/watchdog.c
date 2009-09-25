
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


/*
 * watchdog test program
 */

#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <hvtest.h>

static volatile unsigned int watchdog;

#define TIMEOUT		48

void watchdog_handler(trapframe_t *frameptr)
{
	watchdog = 1;
}

static void delay(void) {
	// Wait a little extra to give the interrupt handler a chance to run
	while (!(mfspr(SPR_TBL) & (1 << 5)));
	while (mfspr(SPR_TBL) & (1 << 5));
}

static void wait_for_timeout(unsigned int wp)
{
	register_t mask = 1 << (31 - (wp & 31));

	if (wp > 31) {
		while (mfspr(SPR_TBL) & mask);
		while (!(mfspr(SPR_TBL) & mask));
	} else {
		while (mfspr(SPR_TBU) & mask);
		while (!(mfspr(SPR_TBU) & mask));
	}
}

static int test1(void)
{
	printf("> set watchdog, wait twice, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	delay();

	return watchdog != 0;
}

static int test2(void)
{
	printf("> disable critints, set watchdog, wait twice, enable critints, check for interrupt: ");
	watchdog = 0;
	disable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	delay();
	if (watchdog)
		return 0;

	enable_critint();

	delay();
	return watchdog != 0;
}

static int test3(void)
{
	printf("> set watchdog, wait, ping, wait twice, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	wait_for_timeout(TIMEOUT);
	delay();
	if (watchdog)
		return 0;

	wait_for_timeout(TIMEOUT);

	delay();
	return watchdog != 0;
}

static int test4(void)
{
	printf("> disable WIE, set watchdog, wait twice, enable WIE, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT));
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	delay();
	if (watchdog)
		return 0;

	mtspr(SPR_TCR, mfspr(SPR_TCR) | TCR_WIE);

	delay();
	return watchdog != 0;
}

static int test5(void)
{
	printf("> set watchdog, wait thrice, check for no reboot: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT));
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);

	delay();

	// TSR[WRS] should be 0
	return (mfspr(SPR_TSR) & TSR_WRS) == 0;
}

static int test6(void)
{
	printf("> set timeout reset, set watchdog, wait thrice, check for reboot.");

	mtspr(SPR_TCR, TCR_WRC_RESET);

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(TIMEOUT));
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);

	delay();
	return 0;
}

static void secondary_entry(void)
{

	// If the WRS bits are set, then it means that we just rebooted from
	// a watchdog timeout.  If we re-run test6(), then we'll just reboot
	// again.  So instead, we stop.
	if (!(mfspr(SPR_TSR) & TSR_WRS)) {
		unsigned int cpu_index = mfspr(SPR_PIR);

		printf("CPU%u TSR[WRS] = %lu\n", cpu_index, (mfspr(SPR_TSR) & TSR_WRS) >> 28);

		test6();
		// If we get here, then we didn't reboot.  That's a failure.
		printf("test6 FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_WP_SET(0));
	} else {
		printf("test6 PASSED\nTest Complete\n");
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	unsigned int cpu_index = mfspr(SPR_PIR);

	init(devtree_ptr);

	printf("\n\nWatchdog test\n");

	printf("CPU%u TSR[WRS] = %lu\n", cpu_index, (mfspr(SPR_TSR) & TSR_WRS) >> 28);

	mtspr(SPR_TCR, TCR_WRC_NOP);

	if (test1())
		printf("PASSED\n");
	else
		printf("FAILED\n");
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(0));

	if (test2())
		printf("PASSED\n");
	else
		printf("FAILED\n");
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(0));

	if (test3())
		printf("PASSED\n");
	else
		printf("FAILED\n");
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(0));

	if (test4())
		printf("PASSED\n");
	else
		printf("FAILED\n");
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(0));

	if (test5())
		printf("PASSED\n");
	else
		printf("FAILED\n");
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_WP_SET(0));

	// Finish the test on the second core
	secondary_startp = secondary_entry;
	release_secondary_cores();
}
