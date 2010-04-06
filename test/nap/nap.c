/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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
#include <libos/hcalls.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>
#include <libos/percpu.h>

#include <hvtest.h>
#include <libfdt.h>

static int fail;

#define NUM_CORES 4

static int core_state[NUM_CORES];
volatile static int should_be_awake;

static void sync_cores(int secondary)
{
	sync();
	isync();

	if (!secondary) {
		static int next_state;
		int i;

		next_state++;

		for (i = 1; i < NUM_CORES; i++)
			while (core_state[i] != next_state)
				sync();

		core_state[0] = next_state;
	} else {
		int pir = mfspr(SPR_PIR);
		core_state[pir]++;

		while (core_state[0] != core_state[pir])
			sync();
	}

	sync();
	isync();
}

static void secondary_entry(void)
{
	int ret;

	sync_cores(1); /* start */
	sync_cores(1); /* ready to nap */

	if (mfspr(SPR_PIR) & 1) {
		ret = fh_enter_nap(-1, cpu->coreid);
		if (ret) {
			printf("fh_enter_nap: FAILED: "
			       "vcpu %lu failed %d\n", mfspr(SPR_PIR), ret);
			fail = 1;
		}

		if (!should_be_awake) {
			printf("fh_enter_nap: FAILED: "
			       "vcpu %lu awake too soon\n", mfspr(SPR_PIR));
			fail = 1;
		}
	}

	sync_cores(1); /* tells the primary we're all awake */

	for (;;);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	const char *label;
	const uint32_t *prop;
	int node, part2;
	int i, len, ret;
	uint32_t time;

	init(devtree_ptr);

	printf("nap test\n");
	printf("========\n\n");

	label = fdt_getprop(fdt, 0, "label", &len);
	if (!label) {
		printf("no label property: BROKEN %d\n", len);
		goto out;
	}

	/* If we're not the primary partition, just spin. */
	if (strcmp(label, "nap-test")) {
		for (;;)
			;
	}

	node = fdt_path_offset(fdt, "/hypervisor/handles/part2");
	if (node < 0) {
		printf("no /hypervisor/handles/part2: BROKEN %d\n", node);
		goto out;
	}

	prop = fdt_getprop(fdt, node, "reg", &len);
	if (!prop) {
		printf("no part2 reg: BROKEN %d\n", len);
		goto out;
	}

	part2 = *prop;

	secondary_startp = secondary_entry;
	release_secondary_cores();

	sync_cores(0); /* start */

	printf("Getting sleep state while running...\n");
	for (i = 0; i < NUM_CORES + 1; i++) {
		unsigned int state;
		ret = fh_get_core_state(-1, i, &state);
		if (ret) {
			/* Expect a fail on a non-existent core. */
			if (i >= NUM_CORES)
				break;

			printf("fh_get_core_state: FAILED: "
			       "vcpu %d status %u\n", i, ret);
			fail = 1;
			continue;
		}

		if (i >= NUM_CORES) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d didn't fail, state %u\n", i, state);
			fail = 1;
			continue;
		}

		if (state != FH_VCPU_RUN) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d unexpected state %u\n", i, state);
			fail = 1;
		}
	}

	printf("Trying to set nap on other cores/partitions...\n");

	for (i = 1; i < NUM_CORES + 1; i++) {
		unsigned int state;
		ret = fh_enter_nap(-1, i);
		if (ret)
			continue;

		printf("fh_enter_nap: FAILED: "
		       "vcpu %d from boot core didn't fail\n", i);
	}

	ret = fh_enter_nap(part2, 0);
	if (ret == 0) {
		printf("fh_enter_nap: FAILED: "
		       "part2 from boot core didn't fail\n");
	}

	printf("Odd numbered cores putting themselves to nap...\n");
	sync_cores(0); /* tell secondaries to nap */

	/* Make sure this doesn't hang waiting for the napping cores.
	 * Try at it a bunch of times to try to reveal races.
	 */
	for (int i = 0; i < 100; i++) {
		asm volatile("tlbivax 0, %0" : :
		             "r" (TLBIVAX_TLB0 | TLBIVAX_INV_ALL) :
		             "memory");
	}

	/* Wait an arbitrary amount of time for the secondaries to nap...
	 * we can't use sync_cores, because they'll be napping.
	 *
	 * The delay should be large enough that DEC/FIT/watchdog
	 * will have expired.
	 */
	time = mfspr(SPR_TBL);
	while (mfspr(SPR_TBL) - time < 25000000)
		;

	asm volatile("tlbivax 0, %0" : :
	             "r" (TLBIVAX_TLB0 | TLBIVAX_INV_ALL) :
	             "memory");

	time = mfspr(SPR_TBL);
	while (mfspr(SPR_TBL) - time < 25000000)
		;

	printf("Checking nap status...\n");
	sync();

	/* Now every odd-numbered core should be napping. */
	for (i = 0; i < NUM_CORES; i++) {
		unsigned int state;
		ret = fh_get_core_state(-1, i, &state);
		if (ret) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d status %u\n", i, ret);
			fail = 1;
			continue;
		}

		if (state != (i & 1 ? FH_VCPU_NAP : FH_VCPU_RUN)) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d unexpected state %u\n", i, ret);
			fail = 1;
		}
	}

	printf("Waking cores...\n");
	should_be_awake = 1;

	for (i = 0; i < NUM_CORES; i++) {
		unsigned int state;
		ret = fh_exit_nap(-1, i);
		if (ret) {
			printf("fh_exit_nap: FAILED: "
			       "vcpu %d from boot core failed %d\n", i, ret);
		}
	}

	/* After this all should be awake; if one doesn't return we'll
	 * get stuck here.
	 */
	sync_cores(0);

	for (i = 0; i < NUM_CORES; i++) {
		unsigned int state;
		ret = fh_get_core_state(-1, i, &state);
		if (ret) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d status %u\n", i, ret);
			fail = 1;
			continue;
		}

		if (state != FH_VCPU_RUN) {
			printf("fh_get_core_state: FAILED: "
			       "vcpu %d unexpected state %u\n", i, ret);
			fail = 1;
		}
	}

	if (fail)
		printf("FAILED\n");
	else
		printf("PASSED\n");

	printf("Test Complete\n");
out:
	fh_partition_stop(-1);
	BUG();
}
