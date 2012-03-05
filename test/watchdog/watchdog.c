
/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile unsigned int watchdog;
static volatile unsigned int state_change_int;
static volatile unsigned int wd_notify_int;

static int num_parts = 0;
static int parts[8];
static int wd_dbells[8];
static int state_dbells[8];

#define TIMEOUT		46

void watchdog_handler(trapframe_t *frameptr)
{
	watchdog = 1;
}

void ext_int_handler(trapframe_t *frameptr)
{
	unsigned int irq;
	int i, found = 0;

	if (coreint)
		irq = mfspr(SPR_EPR);
	else
		ev_int_iack(0, &irq);

	for (i = 0; i < num_parts; i++) {
		if (irq == wd_dbells[i]) {
			found = 1;
			wd_notify_int++;
			// Stopping partition so that it does not flood the 
			// manager partition with watchdog expired notifications
			fh_partition_stop(parts[i]);

			break;
		}
		if (irq == state_dbells[i]) {
			found = 1;
			state_change_int++;

			break;
		}
	}
	if (!found) {
		printf("Unknown external irq %d\n", irq);
	}

	ev_int_eoi(irq);
}


static unsigned long period_to_ticks(unsigned int period)
{
	return 2UL << period;
}

/* Delay for a time equivalent to the expected interval
 * for a fit of the given period.
 */
static void delay_period(unsigned int period)
{
	return delay_timebase(period_to_ticks(period));
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

static const uint32_t *get_handle(int off, const char *compatible_type,
                                  const char *prop)
{
	int ret;
	int len;

	ret = fdt_node_offset_by_compatible(fdt, off, compatible_type);
	if (ret < 0)
		return NULL;

	off = ret;

	return fdt_getprop(fdt, off, prop, &len);
}

static int test1(void)
{
	printf("> set watchdog, wait twice, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);

	return watchdog != 0;
}

static int test2(void)
{
	printf("> disable critints, set watchdog, wait twice, enable critints, check for interrupt: ");
	watchdog = 0;
	disable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	delay_timebase(1000);
	if (watchdog)
		return 0;

	enable_critint();

	return watchdog != 0;
}

static int test3(void)
{
	printf("> set watchdog, wait, ping, wait twice, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT) | TCR_WIE);
	wait_for_timeout(TIMEOUT);
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	wait_for_timeout(TIMEOUT);
	if (watchdog)
		return 0;
	wait_for_timeout(TIMEOUT);

	return !!watchdog;
}

static int test4(void)
{
	printf("> disable WIE, set watchdog, wait twice, enable WIE, check for interrupt: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT));
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	delay_timebase(1000);
	if (watchdog)
		return 0;

	mtspr(SPR_TCR, mfspr(SPR_TCR) | TCR_WIE);

	return !!watchdog;
}

static int test5(void)
{
	printf("> set watchdog, wait thrice, check for no reboot: ");

	watchdog = 0;
	enable_critint();
	mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
	mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT));
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);
	wait_for_timeout(TIMEOUT);

	delay_timebase(100000);

	// TSR[WRS] should be 0
	return (mfspr(SPR_TSR) & TSR_WRS) == 0;
}

static int get_dbell_handles(void)
{
	int node, len;
	const uint32_t *phandle;

	/* Iterate managed partitions and store partition handles, 
	 * 'state change doorbell' irqs and 'watchdog expiration doorbell' irqs
	 */
	node = fdt_path_offset(fdt, "/hypervisor/handles");
	if (node < 0) {
		printf("'hypervisior/handles' node not found\n");
		return 0;
	}
	node = fdt_node_offset_by_compatible(fdt, node, "fsl,hv-partition-handle");
	while (node >= 0) {
		// Get partition handle
		phandle = fdt_getprop(fdt, node, "hv-handle", &len);
		if (!phandle || len != 4) {
			printf("\nError reading slave partition's 'reg' property\n");
			return 0;
		}
		parts[num_parts] = *phandle;

		// Get 'state change doorbell' irq
		phandle = get_handle(node, "fsl,hv-state-change-doorbell", "interrupts");
		if (!phandle) {
			printf("Error retrieving 'fsl,hv-state-change-doorbell'\n");
			return 0;
		}
		state_dbells[num_parts] = *phandle;

		// Get 'watchdog expiration doorbell' irq
		phandle = get_handle(node, "fsl,hv-watchdog-expiration-doorbell", "interrupts");
		if (!phandle) {
			printf("Error retrieving 'fsl,hv-watchdog-expiration-doorbell'\n");
			return 0;
		}
		wd_dbells[num_parts] = *phandle;

		printf("\tpartition #%d: reg = %d state-change-irq = %d watchdog-irq = %d\n",
		       num_parts, parts[num_parts], state_dbells[num_parts],wd_dbells[num_parts]);

		num_parts++;

		node = fdt_node_offset_by_compatible(fdt, node, "fsl,hv-partition-handle");
	}

	return -1;
}

static int config_dbell_ints(void)
{
	int ret, i;

	for (i = 0; i < num_parts; i++) {
		ret = ev_int_set_config(state_dbells[i], 1, 15, 0);
		if (ret) {
			printf("ev_int_set_config failed %d\n", ret);
			return 0;
		}
		ret = ev_int_set_mask(state_dbells[i], 0);
		if (ret) {
			printf("ev_int_set_config failed %d\n", ret);
			return 0;
		}

		ret = ev_int_set_config(wd_dbells[i], 1, 15, 0);
		if (ret) {
			printf("ev_int_set_config failed %d\n", ret);
			return 0;
		}
		ret = ev_int_set_mask(wd_dbells[i], 0);
		if (ret) {
			printf("ev_int_set_config failed %d\n", ret);
			return 0;
		}
	}

	return -1;
}

static int test6_master(void)
{
	int wd_restart = 0, wd_notify = 0, wd_stop = 0;
	int i, ret;
	const char *bootargs;

	bootargs = get_bootargs();
	printf("> [master partition] scenario '%s'\n", bootargs);

	if (!get_dbell_handles())
		return 0;

	if (!config_dbell_ints())
		return 0;

	printf("> [master partition] start slave partitions, wait for watchdog events:\n");

	// Start partitions one by one and wait for watchdog events
	enable_extint();
	for (i = 0; i < num_parts; i++) {
		unsigned int status;

		ret = fh_partition_start(parts[i], 0, 0);
		if (ret) {
			printf("Error starting slave partition %d\n", parts[i]);
			return 0;
		}

		// Wait for partition to start
		do {
			ret = fh_partition_get_status(parts[i], &status);
			assert(!ret);
		} while (status != FH_PARTITION_RUNNING);

		// Wait for watchdog event
		state_change_int = wd_notify_int = 0;
		wait_for_timeout(TIMEOUT);
		wait_for_timeout(TIMEOUT);
		wait_for_timeout(TIMEOUT);
		wait_for_timeout(TIMEOUT);

		// Wait for HV to finish performing the watchdog action.
		// For the 'restart' case this takes a significant
		// amount of time because the guest image is reloaded.
		while (!state_change_int && !wd_notify_int) ;

		// Find out what type of watchdog action happened
		if (wd_notify_int) {
			// Received a watchdog expiration event
			wd_notify++;
			printf("\tpartition %d (reg=%d) watchdog expired notification\n", i, parts[i]);
		} else if (state_change_int) {
			ret = fh_partition_get_status(parts[i], &status);
			assert(!ret);
			// Check for partition reset (state change from RUNNING to STARTING or RUNNING)
			if (status == FH_PARTITION_RUNNING || status == FH_PARTITION_STARTING) {
				printf("\tpartition %d (reg=%d) watchdog restart\n", i, parts[i]);
				wd_restart++;
			}
			// Check for partition stop (state change from RUNNING to STOPPED)
			if (status == FH_PARTITION_STOPPED) {
				printf("\tpartition #%d: (reg=%d) watchdog stop\n", i, parts[i]);
				wd_stop++;
			}
		}
	}
	disable_extint();
	printf("> [master partition] test6: ");

	ret = 0;
	if (wd_restart == 1 && !strcmp(bootargs, "partition-reset"))
		ret++;
	if (wd_notify == 1 && !strcmp(bootargs, "manager-notify"))
		ret++;
	if (wd_stop == 1 && !strcmp(bootargs, "partition-stop"))
		ret++;

	// Test is passed if one and only one watchdog event type happened
	return ret == 1;
}

static int test6_slave(void)
{
	const char *bootargs;

	bootargs = get_bootargs();

	if (bootargs && !strcmp(bootargs, "watchdog-autostart")) {
		printf("> [slave partition] wait for autostarted watchdog to trigger");
		while (1)
			;
	}

	if (!(mfspr(SPR_TSR) & TSR_WRS)) {
		printf("> [slave partition] set timeout reset, set watchdog, wait thrice.");

		mtspr(SPR_TCR, TCR_WRC_RESET);

		watchdog = 0;
		enable_critint();
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(TIMEOUT));
		wait_for_timeout(TIMEOUT);
		wait_for_timeout(TIMEOUT);
		wait_for_timeout(TIMEOUT);

		delay_timebase(100000);
	} else {
		printf(" reset done.\n");
	}

	return 0;
}

void libos_client_entry(unsigned long devtree_ptr)
{
	unsigned int cpu_index = mfspr(SPR_PIR);
	const char *label;
	int len;

	init(devtree_ptr);

	printf("\n\nWatchdog test\n");

	printf("CPU%u TSR[WRS] = %lu\n", cpu_index, (mfspr(SPR_TSR) & TSR_WRS) >> 28);

	label = fdt_getprop(fdt, 0, "label", &len);
	if (!label || !len) {
		printf("Error reading partition label\n");
		return;
	}

	if (!strcmp(label, "/manager-part")) {
		mtspr(SPR_TCR, TCR_WRC_NOP);

		if (test1())
			printf("PASSED\n");
		else
			printf("FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(0));

		if (test2())
			printf("PASSED\n");
		else
			printf("FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(0));

		if (test3())
			printf("PASSED\n");
		else
			printf("FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(0));

		if (test4())
			printf("PASSED\n");
		else
			printf("FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(0));

		if (test5())
			printf("PASSED\n");
		else
			printf("FAILED\n");
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		mtspr(SPR_TCR, TCR_INT_TO_WP(0));

		if (test6_master())
			printf("PASSED\n");
		else
			printf("FAILED\n");
	} else {
		test6_slave();
	}

	printf("Test Complete\n");
}
