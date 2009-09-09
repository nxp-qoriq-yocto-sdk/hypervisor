/** @file
 * Microbenchmarks.
 */
/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#include <libos/bitops.h>

#include <benchmark.h>
#include <shell.h>
#include <devtree.h>

#include <limits.h>

typedef struct benchmark {
	const char *name;
	uint64_t accum; /* Accumulated time */
	unsigned long min, max; /* Record fast/slow */
	unsigned long num; /* number of instances */
	uint32_t lock;
} benchmark_t;

static benchmark_t benchmarks[num_benchmarks] = {
	[bm_tlb0_inv_pid] = {
		.name = "TLB0 cache invalidate by PID",
		.min = ULONG_MAX
	},
	[bm_tlb0_inv_all] = {
		.name = "TLB0 cache invalidate all",
		.min = ULONG_MAX
	},
	[bm_tlb1_inv] = {
		.name = "TLB1 invalidate",
		.min = ULONG_MAX
	},
	[bm_tlbwe] = {
		.name = "TLB write",
		.min = ULONG_MAX
	},
};

void bench_stop(unsigned long start, int bmnum)
{
	unsigned long end = mfspr(SPR_TBL);
	benchmark_t *bm = &benchmarks[bmnum];
	unsigned long diff = end - start;
	register_t saved;

	saved = spin_lock_intsave(&bm->lock);

	bm->accum += diff;
	bm->num++;

	if (bm->min > diff)
		bm->min = diff;

	if (bm->max < diff)
		bm->max = diff;

	spin_unlock_intsave(&bm->lock, saved);
}

static unsigned long tb_to_nsec(uint64_t freq, unsigned long ticks)
{
	return ticks * 1000000000ULL / freq;
}

static void benchmark_fn(shell_t *shell, char *args)
{
	uint64_t freq = dt_get_timebase_freq();

	if (num_benchmarks == 0) {
		printf("No benchmarks defined.\n");
		return;
	}

	for (benchmark_num_t i = 0; i < num_benchmarks; i++) {
		benchmark_t *bm = &benchmarks[i];

		spin_lock_int(&bm->lock);

		if (bm->num == 0)
			printf("%s: no data\n", bm->name);
		else
			printf("%s: %lu ns avg, %lu ns min, %lu ns max, %lu instances\n",
			       bm->name,
			       tb_to_nsec(freq, bm->accum / bm->num),
			       tb_to_nsec(freq, bm->min),
			       tb_to_nsec(freq, bm->max), bm->num);

		spin_unlock_int(&bm->lock);
	}
}

static command_t benchmark = {
	.name = "benchmark",
	.action = benchmark_fn,
	.shorthelp = "Print microbenchmark information",
};
shell_cmd(benchmark);
