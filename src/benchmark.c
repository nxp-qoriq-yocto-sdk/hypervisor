/** @file
 * Microbenchmarks.
 */
/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc.
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
#include <libos/queue.h>
#include <libos/bitops.h>
#include <libos/alloc.h>

#include <benchmark.h>
#include <percpu.h>
#include <devtree.h>

#include <limits.h>

const char *benchmark_names[num_benchmarks] = {
	"Other",
	/* emulated instructions start here */
	"tlbre",
	"tlbilx",
	"tlbsx",
	"tlbsync",
	"msgsnd",
	"msgclr",
	"spr",
	"pmr",
	"tlbwe(tlb0)",
	"tlbwe(tlb1)",
	"tlbivax(all tlb0)",
	"tlbivax(tlb0)",
	"tlbivax(all tlb1)",
	"tlbivax(tlb1)",
	"tlbivax",
	"emulated inst - other",
	/* hcalls start here */
	"vmpic eoi",
	"hcall - other",
	/* other tracked exceptions go here */
	"machine check",
	"critical int",
	"dsi",
	"isi",
	"program exception",
	"decrementer int",
	"fit interrupt",
	"guest doorbell",
	"guest doorbell crit",
	"doorbell",
	"debug interrupt",
	"watchdog int",
	"alignment",
	"floating pt. unavail",
	"tlb miss count",
	"tlb miss reflected",
	"tlb miss",
	"altivec unavail",
	"altivec assist",
	"lrat miss",
	/* microbenchmarks go here */
	"tlbinv by PID",
	"tlbcache inv all",
	"tlb1 inv",
	"tlb write",
};

void statistics_stop(uint32_t start, int bmnum)
{
	uint32_t end = mfspr(SPR_TBL);
	benchmark_t *bm = &get_gcpu()->benchmarks[bmnum];
	uint32_t diff = end - start;

	bm->accum += diff;
	bm->num++;

	if (bm->min > diff || !bm->min)
		bm->min = diff;

	if (bm->max < diff)
		bm->max = diff;
}
