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

#include "error-mgr-common.c"
#include "error-mgr-tests.c"

static void execute_tests_A(register_t pir)
{
	puts("Test suite A");

	test_stat[pir] += test1_errors_on_mgr_stopped(pir);

	test_stat[pir] += test2_orig_interrupt(pir);

	test_barrier(0, 4); /* wait for results to be written in stat */
	if (!was_restarted) {
		memset(shared_memory, 0, 2 * sizeof(long));
		fh_partition_restart(-1);
	}

	end_test_and_print(pir);
}

static void execute_tests_B(register_t pir) {
	puts("Test suite B");

	/* /part3 should only do test4 */
	if (strstr(label, "/part3-") == NULL) {
		/* Want this primarily for its start of the /part2 */
		test_stat[pir] += test1_errors_on_mgr_stopped(pir);
	
		test_stat[pir] += test3_claim_its_own(pir);
	}

	test_stat[pir] += test4_claim_error_manager(pir);

	end_test_and_print(pir);
}

void define_test_case(const char *test_selection)
{
	if (test_selection) {
		if (strcmp(test_selection, "-A") == 0)
			execute_tests = execute_tests_A;
		else if (strcmp(test_selection, "-B") == 0)
			execute_tests = execute_tests_B;
	}
}
