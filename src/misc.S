/** @file
 */
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

#include <libos/fsl-booke-tlb.h>
#include <libos/core-regs.h>

#if 1
/* 32-bit */
#define LONGBYTES 4
#define LOAD lwz
#define STORE stw
#else
/* 64-bit */
#define LONGBYTES 8
#define LOAD ld
#define STORE std
#endif

.global idle_loop
idle_loop:
	wait
	b	idle_loop

/** Branch to relocated hypervisor code
 * r3 = bigmap virtual address of new text base
 * r4 = new mas3
 * r5 = new mas7
 */
.global branch_to_reloc
branch_to_reloc:
	addis	%r3, %r3, (1f - PHYSBASE)@ha
	addi	%r3, %r3, (1f - PHYSBASE)@l
	mtctr	%r3
	btctr	%r3

1:	/* Now running out of bigmap, relocate boot mapping */
	lis	%r6, PHYSBASE@h
	ori	%r6, %r6, PHYSBASE@l
	tlbsx	0, %r6

	mtspr	SPR_MAS3, %r4
	mtspr	SPR_MAS7, %r5

	isync
	tlbwe
	isync
	msync

	/* Return into new text mapping */
	blr

#ifdef CONFIG_PM
/** Flush and disable L1 cache.  We assume 8-way PLRU.
 * r3 = address to use for displacement flush.
 *      This needs to have a valid mapping with load access
 *      for at least 13/8 the size of the cache.  It will not
 *      be overwritten and it does not matter if parts of it
 *      are already in the cache.
 *
 * r4 = timebase ticks for a timeout on invalidation
 */
.global flush_disable_l1
flush_disable_l1:
	mfspr	%r8, SPR_L1CFG0

	rlwinm	%r5, %r8, 9, 3	/* Extract cache block size */
	twlgti	%r5, 1		/* Only 32 and 64 byte cache blocks
	                         * are currently defined.
	                         */
	li	%r10, 32
	subfic	%r6, %r5, 2	/* %r6 = log2(1024/(block size * ways))
				 * r5 contains log2(block size) - 5
				 * log2(1024)-log2(block size)-log2(ways) =
				 * 10 - r5 - 5 - 3 =
				 * 2 - r5
				 */
	slw	%r5, %r10, %r5	/* %r5 = cache block size */

	rlwinm	%r7, %r8, 0, 0x7ff /* Extract number of KiB in the cache */
	mulli	%r7, %r7, 13	/* An 8-way cache will %require 13
	                         * loads per way.
	                         */
	slw	%r7, %r7, %r6	/* r7 = cache KiB*13*1024/block size/ways
				 *    = cache bytes * (13/8) / block size
				 *    = cache blocks to flush
				 */
	mr	%r9, %r3
	mtctr	%r7

1:	lwz	%r0, 0(%r9)	/* Load... */
	add	%r9, %r9, %r5
	bdnz	1b

	msync
	mr	%r9, %r3
	mtctr	%r7

1:	dcbf	0, %r9		/* ...and flush. */
	add	%r9, %r9, %r5
	bdnz	1b

	mfspr	%r10, SPR_L1CSR0	/* Invalidate and disable d-cache */
	li	%r5, 2
	rlwimi	%r10, %r5, 0, 3

	msync
	isync
	mtspr	SPR_L1CSR0, %r10
	isync

	mfspr	%r11, SPR_TBL
	li	%r3, -1

1:	mfspr	%r10, SPR_L1CSR0 /* Wait for the invalidate to finish */

	mfspr	%r12, SPR_TBL
	sub	%r12, %r12, %r11
	cmplw	%r12, %r4
	bgelr

	andi.	%r10, %r10, 2
	bne	1b

	rlwimi	%r10, %r8, 2, 3	/* Extract cache type */
	twlgti	%r10, 1		/* Only 0 (Harvard) and 1 (Unified)
	                         * are currently defined.
	                         */

	andi.	%r10, %r10, 1	/* If it's unified, we're done. */
	li	%r3, 0
	bnelr

	mfspr	%r10, SPR_L1CSR1	/* Otherwise, invalidate the i-cache */
	li	%r5, 2
	rlwimi	%r10, %r5, 0, 3

	msync
	isync
	mtspr	SPR_L1CSR1, %r10
	isync

	mfspr	%r11, SPR_TBL
	li	%r3, -1

1:	mfspr	%r10, SPR_L1CSR1 /* Wait for the invalidate to finish */

	mfspr	%r12, SPR_TBL
	sub	%r12, %r12, %r11
	cmplw	%r12, %r4
	bgelr

	andi.	%r10, %r10, 2
	bne	1b

	li	%r3, 0
	blr
#endif
