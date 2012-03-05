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

static int test1_errors_on_mgr_stopped(register_t pir);
static int test2_orig_interrupt(register_t pir);
static int test3_claim_its_own(register_t pir);
static int test4_claim_error_manager(register_t pir);

/* Test that errors are posted into global queue, while the error manager is
 * not running.
 * Initial state:
 * - The slave partition /part1 is alive, and all resources are initialized:
 *   shared memory, DMA handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - The slave guest (with DMA), with no error manager active, does two illegal
 *   DMAs.
 * - The slave guest (with DMA) starts the /part2, the error manager. One of its
 *   cores will succeed, the other should fail.
 * - The manager guest, currently owning the error manager, should receive two
 *   interrupts as soon as it enables crit int.
 * - All 4 cores (2 from each guest) synchronize at a barrier.
 * Results validation:
 * - The core owning the error manager must receive 2 critical interrupts.
 * Not tested (outside of test scope):
 * - The boot core of the slave guest having DMA must receive 2 machine checks.
 */
static int test1_errors_on_mgr_stopped(register_t pir)
{
	int ret; //, expected_crit_int = was_restarted ? 2 : 1;
	/* is this CPU the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	/* This is a slave partition which generates two errors into the
	 * global queue while the error manager is stopped. */
	test_common_do_dma(pir, is_err_owner);
	test_common_do_dma(pir, is_err_owner);

	if (dma_virt) {
		/* Only one of the two guests of the test runs this */
		ret = fh_partition_start(slave_part_handle[0], 0, 0);
		if (!ret) {
			printf("[%ld] partition %d started\n", pir,
			       slave_part_handle[0]);
		}
	}
	
	/* Expect two crit int from DMA accesses of one guest */
	if ((is_err_owner && crit_cnt[pir] == old_crit_cnt[pir] + 2) ||
	    (!is_err_owner && crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 1: PASSED\n", pir);
		ret = TEST_PASSED;
	} else {
		printf("[%ld] Test 1: FAILED\n", pir);
		ret = TEST_FAILED;
	}

	crit_cnt[pir] = old_crit_cnt[pir] = 0;

	/* 4 cores active, from both guests */
	test_barrier(0, 4);
	return ret;
}

/* Test that we receive critical interrupt for the initial error manager.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - Start the slave guest which owns the DMA (DMA is owned by one single core).
 *   Only one core of the master partition will succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and one core has DMA.
 * - Once the test is executed, all guests restart themselves to verify that the
 *   error manager is reassigned when alive (see execute_tests()). The first
 *   test executes again.
 * Results validation:
 * - The core owning the error manager must receive 1 critical interrupt.
 * Not tested (outside of test scope):
 * - The boot core of the slave guest having DMA must receive 1 machine check.
 */
static int test2_orig_interrupt(register_t pir)
{
	int ret;
	/* is this CPU the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	test_common_do_dma(pir, is_err_owner);

	/* Expect one crit int because only one guest has DMA */
	if ((is_err_owner && crit_cnt[pir] == old_crit_cnt[pir] + 1) ||
			(!is_err_owner && crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 2: PASSED\n", pir);
		ret = TEST_PASSED;
	} else {
		printf("[%ld] Test 2: FAILED\n", pir);
		ret = TEST_FAILED;
	}

	crit_cnt[pir] = old_crit_cnt[pir] = 0;

	test_barrier(1, 4);
	return ret;
}

/* Test that a guest cannot claim the error manager that already owns and others
 * don't since the owner is not stopped. Check that critical interrupts still
 * arrive where they should.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - The slave /part1 is alive, and all resources are mapped/initialized:
 *   shared memory, DMA handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - All 4 cores claim the error manager. No core should succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and one core has DMA.
 * Results validation:
 * - All cores should receive an invalid state error code for hcall.
 * - The core owning the error manager must receive 1 critical interrupt.
 * Not tested (outside of test scope):
 * - The boot core of the slave guest having DMA must receive 1 machine check.
 */
static int test3_claim_its_own(register_t pir)
{
	int ret;
	/* is this CPU the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	/* Might have been nice to allow a current owner to claim its own
	 * error manager, but it could be just an useless corner case; so all
	 * return codes are EV_INVALID_STATE.
	 */
	ret = fh_claim_device(error_manager_handle);

	test_common_do_dma(pir, is_err_owner);

	/* Expect one crit int because only one guest has DMA */
	if ((is_err_owner && ret == EV_INVALID_STATE &&
				crit_cnt[pir] == old_crit_cnt[pir] + 1) ||
				(!is_err_owner && ret == EV_INVALID_STATE &&
				crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 3: PASSED\n", pir);
		ret = TEST_PASSED;
	} else {
		if (ret == EV_UNIMPLEMENTED)
			/* Abandon remaining tests as there is no claim device */
			end_test_and_print(pir);

		/* If it failed with EV_EINVAL, check if the error manager is
		 * non claimable. This means that it is expected to pass a test
		 * in which the error-manager is non-claimable to this guest.
		 * Else, there must be other problem with the configuration.
		 */
		if (!fdt_getprop(fdt, error_manager_node, "fsl,hv-claimable",
				 NULL) && ret == EV_EINVAL) {
			printf("[%ld] Test 3: PASSED\n", pir);
			ret = TEST_PASSED;
		} else {
			printf("[%ld] Test 3: FAILED with return code %d\n", pir,
			       ret);
			ret = TEST_FAILED;
		}
	}

	crit_cnt[pir] = old_crit_cnt[pir] = 0;

	test_barrier(1, 4);
	return ret;
}

/* Test that a guest can claim the error manager once the previous owner is
 * stopped.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - The slave /part1 is alive, and all resources are mapped/initialized:
 *   shared memory, DMA handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - The manager guest (without DMA) starts the second slave /part3. One of its
 *   core will succeed, the other should fail.
 * - The manager guest, currently owning the error manager, stop itself.
 * - All 4 cores (2 from each slave guest) synchronize at a barrier.
 * - Subtest 4_1 tests that after an error manager stops itself, the critical
 *   interrupts are queued but not delivered. One DMA illegal access will be
 *   queued and it will be retrieved in subtest 4_2 by the future error manager.
 * - Subtest 4_2 tests that a core can claim the error manager. All 4 cores
 *   claim the error manager. One should succeed. All perform a small stress
 *   test claiming the error manager for 6 times in a row. Only once the hcall
 *   should succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and two cores have DMA channels.
 * Results validation:
 * - The core owning the error manager must receive 3 critical interrupts since
 *   there are two cores doing illegal DMA accesses, plus the one queued when
 *   there was no error manager active. It is noted in the comments
 *   that precise allignment of these accesses may lead to one exception being
 *   lost in the interrupt controller, and a delay was added to one guest.
 * Not tested (outside of test scope):
 * - The boot core of each slave guest having DMA must receive 1 machine check.
 */
static int test4_claim_error_manager(register_t pir)
{
	int ret, result = 0;
	/* is this the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	if (!dma_virt) {
		/* This partition does not have DMA, must be the master */
		assert(slave_part_handle[0] >= 0 && slave_part_handle[1] >= 0);

		/* Whichever core is first */
		ret = fh_partition_start(slave_part_handle[1], 0, 0);
		if (!ret) {/* This colision between cores is expected */
			printf("[%ld] Started partition %d\n", pir,
			       slave_part_handle[1]);
		}
	}

	/* CPUs of the current error manager stop themselves */
	if (is_error_manager) {
		disable_critint(); /* stop any global event */
		test_barrier(0, 6); /* 6 concurrent CPUs from 3 guests */
		end_test_and_print(pir);
	}

	test_barrier(0, 6); /* 6 concurrent CPUs from 3 guests */

	/* At this point, there should not be any error owner, as the previous
	 * error manager partition is stopped.
	 */

	if (slave_part_handle[0] >= 0)
		/* One of the two guests will do one illegal access while there
		 * is no error manager active.
		 */
		test_common_do_dma(pir, is_err_owner);

	/* One guest doing illegal DMA, but no critical interrupts */
	if (!is_err_owner && crit_cnt[pir] == old_crit_cnt[pir]) {
		printf("[%ld] Test 4_1: PASSED\n", pir);
		result += TEST_PASSED;
	} else {
		printf("[%ld] Test 4_1: FAILED\n", pir);
		result += TEST_FAILED;
	}

	/* Never mind the function that parses the FDT, it is out of sync */
	is_error_manager = 0;
	is_err_owner = 0;
	test_barrier(1, 4); /* 4 concurrent CPUs from 2 guests */
	crit_cnt[pir] = old_crit_cnt[pir] = 0;

	/* 4 CPUs to claim the error manager */
	for (int i = 0; i < 6; i++) {
		ret = fh_claim_device(error_manager_handle);
		if (!ret) {
			is_error_manager = 1; /* Guest global var */
			printf("[%ld] Claimed error manager\n", pir);
		}
	}

	/* Do illegal DMA to check that the new error manager is functional */
	if (is_error_manager) {
		if (error_manager_cpu == pir) {
			is_err_owner = 1;
			printf("[%ld] Is error manager CPU\n", pir);
		}

		/* TODO: investigate if the real problem is the loss of
		 * PAMU illegal interrupts in case of simultaneous causes.
		 * Until then, just desynchronize the two guests by a delay.
		 */
		delay_ms(500);
	}
	test_common_do_dma(pir, is_err_owner);

	/* Two guests, each doing illegal DMA, plus one crit int in the queue */
	if ((is_err_owner && crit_cnt[pir] == old_crit_cnt[pir] + 3) ||
	    (!is_err_owner && crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 4_2: PASSED\n", pir);
		result += TEST_PASSED;
	} else {
		printf("[%ld] Test 4_2: FAILED\n", pir);
		result += TEST_FAILED;
	}

	crit_cnt[pir] = old_crit_cnt[pir] = 0;

	return result;
}
