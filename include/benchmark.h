/** @file
 * Microbenchmarks.
 */
/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>

/* If you add a benchmark here, you must update the table in benchmark.c */
typedef enum benchmark_num {
	bm_stat_other,  /**< untracked overhead */
	/* emulated instructions start here */
	bm_stat_tlbre, /**< Overhead of tlbre instructions */
	bm_stat_tlbilx, /**< Overhead of tlbilx instructions */
	bm_stat_tlbsx, /**< Overhead of tlbsx instructions */
	bm_stat_tlbsync, /**< Overhead of tlbsync instructions */
	bm_stat_msgsnd, /**< Overhead of msgsnd instructions */
	bm_stat_msgclr, /**< Overhead of msgclr instructions */
	bm_stat_spr,   /**< Overhead of SPR accesses */
	bm_stat_pmr,   /**< Overhead of PMR accesses */
	bm_stat_tlbwe_tlb0, /**< Overhead of tlbwe instructions for tlb0 */
	bm_stat_tlbwe_tlb1, /**< Overhead of tlbwe instructions for tlb1 */
	bm_stat_tlbivax_tlb0_all, /**< Overhead of tlbivax all instructions for tlb0 */
	bm_stat_tlbivax_tlb0, /**< Overhead of tlbivax instructions for tlb0 */
	bm_stat_tlbivax_tlb1_all, /**< Overhead of tlbivax all instructions for tlb1 */
	bm_stat_tlbivax_tlb1, /**< Overhead of tlbivax instructions for tlb1 */
	bm_stat_emulated_other, /**< emulated instruction overhead -- other */
	/* hcalls start here */
	bm_stat_vmpic_eoi, /**< vmpic eoi hcall */
	bm_stat_hcall, /**< other hcalls not explicitly tracked */
	/* other tracked exceptions go here */
	bm_stat_mcheck, /**< Machine check */
	bm_stat_critint, /**< Critical interrupt */
	bm_stat_dsi, /**< data storage */
	bm_stat_isi, /**< instruction storage */
	bm_stat_program, /**< program exception */
	bm_stat_decr, /**< Decrementer interrupt */
	bm_stat_fit, /**< FIT interrupt */
	bm_stat_gdbell, /**< guest doorbell */
	bm_stat_gdbell_crit, /**< guest critical doorbell */
	bm_stat_dbell, /**< doorbell */
	bm_stat_debug, /**< debug interrupt */
	bm_stat_watchdog, /**< watchdog interrupt */
	bm_stat_align, /**< alignment exception */
	bm_stat_fpunavail, /**< floating point unavailable */
	bm_stat_tlb_miss_count, /**< TLB miss exceptions */
	bm_stat_tlb_miss_reflect, /**< TLB misses reflected to guest in case of TLBCache */
	bm_stat_tlb_miss, /**< TLB miss exceptions */
	/* microbenchmarks go here */
	bm_tlb0_inv_pid, /**< microbenchmarks */
	bm_tlb0_inv_all,
	bm_tlb1_inv,
	bm_tlbwe,
	num_benchmarks
} benchmark_num_t;

typedef struct benchmark {
	uint64_t accum; /* Accumulated time */
	unsigned long min, max; /* Record fast/slow */
	unsigned long num; /* number of instances */
} benchmark_t;

#ifdef CONFIG_STATISTICS
static inline void set_stat(int stat, struct trapframe *regs)
{
	regs->current_event = stat;

}

void statistics_stop(unsigned long start, int bmnum);
#else
static inline void set_stat(int stat, struct trapframe *regs)
{
}

static inline void statistics_stop(unsigned long start, int bmnum)
{
}
#endif

#ifdef CONFIG_BENCHMARKS
static inline register_t bench_start(void)
{
	return mfspr(SPR_TBL);
}

static inline void bench_stop(register_t start, int bm)
{
	statistics_stop(start, bm);
}
#else
static inline register_t bench_start(void)
{
	return 0;
}

static inline void bench_stop(register_t start, int bm)
{
}
#endif

#endif /* BENCHMARK_H */
