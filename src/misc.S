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

#ifndef CONFIG_PM
.global idle_loop
idle_loop:
	wait
	b	idle_loop
#endif

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

/** Flush and disable L1 cache.  We assume 8-way PLRU.
 * r3 = address to use for displacement flush.
 *      This needs to have a valid mapping with load access for at least
 *      the size of the cache.  It will be overwritten.  It does not
 *      matter if parts of it are already in the cache (e.g. from
 *      speculative loads), as we will invalidate the range. It must not
 *      be accessed from any other core or device while the invalidation
 *      is in progress (use a separate area per core).
 *
 * r4 = timebase ticks for a timeout on invalidation
 */
.global flush_disable_l1_cache
flush_disable_l1_cache:
	mfspr	%r8, SPR_L1CFG0

	rlwinm	%r5, %r8, 9, 3	/* Extract cache block size */
	twlgti	%r5, 1		/* Only 32 and 64 byte cache blocks
	                         * are currently defined.
	                         */
	li	%r10, 32
	subfic	%r6, %r5, 5	/* %r6 = log2(1024/block size)
				 * r5 contains log2(block size) - 5
				 * log2(1024) - log2(block size) =
				 * 10 - r5 - 5 =
				 * 5 - r5
				 */
	slw	%r5, %r10, %r5	/* %r5 = cache block size */

	rlwinm	%r7, %r8, 0, 0x7ff /* Extract number of KiB in the cache */
	slw	%r7, %r7, %r6	/* r7 = cache KiB * 1024 / block size
				 *    = cache bytes / block size
				 *    = cache blocks to flush
				 */
	mr	%r9, %r3

#ifdef CONFIG_P4080_ERRATUM_CPU20
	/* Stashing must be disabled while doing a flash d-cache lock clear. */
	mfspr	%r10, SPR_L1CSR2
	rlwinm	%r10, %r10, 0, ~L1CSR2_DCSTASHID
	msync
	isync
	mtspr	SPR_L1CSR2, %r10
	isync
	msync
#endif

	mfspr	%r10, SPR_L1CSR0 /* Clear d-cache locks, so they don't
				  * escape the displacement flush.
				  */
	ori	%r10, %r10, L1CSR0_DCLFC

	msync
	isync
	mtspr	SPR_L1CSR0, %r10
	isync

	li	%r3, -1
	mfspr	%r11, SPR_TBL
1:	mfspr	%r10, SPR_L1CSR0 /* Wait for the lock clear to finish */

	mfspr	%r12, SPR_TBL
	sub	%r12, %r12, %r11
	cmplw	%r12, %r4
	bgelr

	andi.	%r10, %r10, L1CSR0_DCLFC
	bne	1b

	mr	%r3, %r9
	mtctr	%r7

1:	dcbi	0, %r9		/* Make sure the displacement area
				 * isn't in the cache.
				 */
	add	%r9, %r9, %r5
	bdnz	1b

	mfspr	%r10, SPR_HID0	/* Enable data cache flush assist,
				 * reducing the size of the displacement
				 * area needed from 12 per set to 8.
				 */
	ori	%r10, %r10, HID0_DCFA
	msync
	isync
	mtspr	SPR_HID0, %r10
	isync

	mr	%r9, %r3
	mtctr	%r7

1:	dcbz	0, %r9		/* Force junk data into the cache line,
 				 * displacing real data.
 				 */
	add	%r9, %r9, %r5
	bdnz	1b

	mfspr	%r10, SPR_L1CSR0 /* Invalidate and disable d-cache */
	li	%r5, L1CSR0_DCFI
	rlwimi	%r10, %r5, 0, L1CSR0_DCFI | L1CSR0_DCE

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

	andi.	%r10, %r10, L1CSR0_DCFI
	bne	1b

	mfspr	%r10, SPR_HID0	/* Tun data cache flush assist back off */
	rlwinm	%r10, %r10, 0, ~HID0_DCFA
	msync
	isync
	mtspr	SPR_HID0, %r10
	isync

	rlwinm	%r10, %r8, 2, 3	/* Extract cache type */
	twlgti	%r10, 1		/* Only 0 (Harvard) and 1 (Unified)
	                         * are currently defined.
	                         */

	andi.	%r10, %r10, 1	/* If it's unified, we're done. */
	li	%r3, 0
	bnelr

	mfspr	%r10, SPR_L1CSR1 /* Otherwise, invalidate the i-cache */
	li	%r5, L1CSR1_ICFI
	rlwimi	%r10, %r5, 0, L1CSR1_ICFI | L1CSR1_ICE

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

	andi.	%r10, %r10, L1CSR1_ICFI
	bne	1b

	li	%r3, 0
	blr
