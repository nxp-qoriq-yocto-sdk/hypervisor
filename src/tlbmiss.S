/** @file
 * TLB miss fastpath, filling from TLB0 cache
 */
/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <libos/core-regs.h>
#include <libos/fsl-booke-tlb.h>

#ifndef CONFIG_LIBOS_64BIT
/* 32-bit */
#define LONGBYTES 4
#define LOAD lwz
#define STORE stw
#define COMPARE cmpw
#define SHIFTR srw
#define SHIFTL slw
#define LOADX lwzx
#else
/* 64-bit */
#define LONGBYTES 8
#define LOAD ld
#define STORE std
#define COMPARE cmpd
#define SHIFTR srd
#define SHIFTL sld
#define LOADX ldx
#endif

	.macro tlb_miss_fast name asbit addr itlb
	.global \name
	.balign 64
\name:
	mtspr	SPR_SPRG1, %r2
	mfspr	%r2, SPR_SPRG0

	/* We have a full cache line dedicated to TLB miss scratch,
	 * so allocate a cache line without touching the bus.
	 */
	dcba	0, %r2

#ifdef CONFIG_LIBOS_64BIT
	addi	%r2, %r2, 64
	dcba	0, %r2
	addi	%r2, %r2, -64
#endif

	STORE	%r3, LONGBYTES*0(%r2)

	mfspr	%r3, SPR_SPRG3
	STORE	%r4, LONGBYTES*1(%r2)
	STORE	%r5, LONGBYTES*2(%r2)
	STORE	%r6, LONGBYTES*3(%r2)
	li	%r5, MSR_ME | MSR_RI
#ifdef CONFIG_LIBOS_64BIT
	oris	%r5, %r5, MSR_CM@h
#endif
	STORE	%r7, LONGBYTES*4(%r2)
	li	%r6, 1

	mtmsr	%r5			// disable ints

	mfcr	%r4
	rlwinm	%r5, %r3, 0, 31		// r5 = index bits from SPRG3
	STORE	%r8, LONGBYTES*5(%r2)
	addi	%r7, %r5, 12		// r7 = index bits + PAGE_SHIFT
	STORE	%r9, LONGBYTES*6(%r2)
	slw	%r5, %r6, %r5		// r6 = 1 << index bits
	STORE	%r10, LONGBYTES*7(%r2)
	cmpwi	%cr7, %r6, \itlb

	mfspr	%r8, \addr
	addi	%r5, %r5, -1
	STORE	%r4, LONGBYTES*15(%r2)
#ifndef CONFIG_LIBOS_64BIT
	rlwinm	%r3, %r3, 0, 0xfffff000	// r3 = cache address from SPRG3
#else
	clrrdi	%r3, %r3, 12
#endif
	STORE	%r11, LONGBYTES*8(%r2)
	rlwinm	%r5, %r5, 6, 0, 25	// r5 *= sizeof(tlbcset_t)
	STORE	%r12, LONGBYTES*9(%r2)

	mfspr	%r6, SPR_SRR1
	rlwinm	%r4, %r8, 26, 6, 25	// r4 = set index into array
	and	%r4, %r4, %r5
	SHIFTR	%r7, %r8, %r7		// r7 = address
	li	%r9, 22
	SHIFTL	%r7, %r7, %r9		// r7 = tag bits from address

	.if	\itlb == 0
	andis.	%r10, %r6, MSR_GS@h
	mfspr	%r11, SPR_ESR
	beq	hv_dtlb_miss
	andi.	%r10, %r11, ESR_EPID
	bne	epid_guest
	rlwimi	%r7, %r6, \asbit - 43, 11, 11 // Insert AS into tag
	mfspr	%r12, SPR_PID
ret_epid:
	.else
	andis.	%r10, %r6, MSR_GS@h
	rlwimi	%r7, %r6, \asbit - 43, 11, 11 // Insert AS into tag
	beq	tlb_miss_slow
	mfspr	%r12, SPR_PID
	.endif

	b	tlb_miss_fast_common
	.endm

/* If the hv uses epid to access guest space, treat it like a guest epid
 * access.  Otherwise, take the slow path like normal hv tlb misses.
 *
 * The only valid HV epid scenarios are:
 *   ELPID = LPID and EGS = 1
 *   ELPID = 0 and EGS = 0
 * ...so we don't test ELPID here.
 */
epid_hv:
	andis.	%r10, %r11, ESR_ST@h
	bne	1f
	mfspr	%r10, SPR_EPLC
	b	2f
1:	mfspr	%r10, SPR_EPSC
2:	andis.	%r12, %r10, EPC_EGS@h
	rlwimi	%r7, %r10, 32 + EPCBIT_EAS - 43, 11, 11 // Insert AS into tag
	beq	tlb_miss_slow
	rlwinm	%r12, %r10, 0, EPC_EPID
	b	ret_epid

epid_guest:
	andis.	%r10, %r11, ESR_ST@h
	bne	1f
	mfspr	%r10, SPR_EPLC
	b	2f
1:	mfspr	%r10, SPR_EPSC
2:	rlwimi	%r7, %r10, 32 + EPCBIT_EAS - 43, 11, 11 // Insert AS into tag
	rlwinm	%r12, %r10, 0, EPC_EPID
	b	ret_epid

	tlb_miss_fast itlb_miss_fast 58 SPR_SRR0 1
	tlb_miss_fast dtlb_miss_fast 59 SPR_DEAR 0

	.balign	64
tlb_miss_fast_common:
#ifdef CONFIG_STATISTICS
	LOAD	%r9, CLIENT_GCPU(%r2)
	LOAD	%r10, TLB_MISS_COUNT(%r9)
	addi	%r10, %r10, 1
	STORE	%r10, TLB_MISS_COUNT(%r9)
#endif
	add	%r3, %r3, %r4		// r3 = tlbset_t address
	LOAD	%r9, LONGBYTES*0(%r3)	// Load the tag words
	mfspr	%r5, SPR_LPIDR
	LOAD	%r10, LONGBYTES*1(%r3)
	LOAD	%r11, LONGBYTES*2(%r3)

	rlwimi	%r7, %r5, 14, 12, 17	// Insert LPID into tag
	li	%r4, -1
	oris	%r5, %r7, 0x20		// Set valid bit in tag
	oris	%r7, %r7, 0x20		// r7 will be used as PID zero tag
	rlwimi	%r5, %r12, 0, 18, 31	// Insert PID into tag

	LOAD	%r12, LONGBYTES*3(%r3)

	COMPARE	%cr0, %r5, %r9		// Compare tags
	COMPARE	%cr1, %r7, %r9		// Compare tags with PID zero
	COMPARE	%cr2, %r5, %r10
	cror	0*4+2, 0*4+2, 1*4+2
	COMPARE	%cr3, %r7, %r10
	COMPARE	%cr4, %r5, %r11
	COMPARE	%cr5, %r7, %r11
	cror	1*4+2, 2*4+2, 3*4+2
	COMPARE	%cr6, %r5, %r12
	cror	2*4+2, 4*4+2, 5*4+2
	COMPARE	%cr4, %r7, %r12
	cror	3*4+2, 6*4+2, 4*4+2

	/* Select a matching tag.  If there are multiple matches,
	 * the highest numbered way will be used, but the hypervisor
	 * should prevent such duplicates from being added in the
	 * first place.
	 */
	li	%r7, 0
	isel	%r4, %r7, %r4, 2
	li	%r6, 8
	isel	%r4, %r6, %r4, 6
	li	%r7, 16
	isel	%r4, %r7, %r4, 10
	li	%r6, 24
	isel	%r4, %r6, %r4, 14

	cmpwi	%r4, 0
	blt-	tlb_miss_slow

	mfspr	%r7, SPR_MAS0
	add	%r6, %r3, %r4		   // r6 = "entry" offset
	lwz	%r11, LONGBYTES*4 + 0(%r6) // r11 = mas3
	lwz	%r12, LONGBYTES*4 + 4(%r6) // r12 = mas2/mas7/etc. union
#ifndef CONFIG_LIBOS_64BIT
	rlwinm	%r4, %r4, 31, 15	   // r4 = tag offset
#endif
	LOADX	%r5, %r3, %r4		   // r5 = tag from TLB cache

	mfspr	%r6, SPR_MAS1
	stw	%r7, LONGBYTES*11(%r2)
	mfspr	%r10, SPR_MAS2
	stw	%r6, LONGBYTES*11+4(%r2)
	mfspr	%r9, SPR_MAS3
	STORE	%r10, LONGBYTES*10(%r2)
	mfspr	%r10, SPR_MAS7
	stw	%r9, LONGBYTES*11+8(%r2)
	stw	%r10, LONGBYTES*11+12(%r2)

	/* Acquire a next-victim hint.  MAS5 is assumed to have SGS/SLPID
	 * set properly.
	 */
	mfspr	%r9, SPR_MAS6
	rlwinm	%r4, %r5, 12, 31, 31	// Extract MAS6[SAS] from tag.space
	rlwimi	%r4, %r5, 16, 2, 16	// Extract MAS[SPID] from tag.pid
	mtspr	SPR_MAS6, %r4
	isync
	tlbsx	0, %r8
	mtspr	SPR_MAS6, %r9

	mfspr	%r4, SPR_MAS0
	mfspr	%r7, SPR_MAS1
		
	rlwimi	%r8, %r12, 8, 24, 31	// r8 = MAS2
	rlwinm	%r6, %r12, 12, 28, 31	// r6 = MAS7
	oris	%r7, %r7, 0x8000	// Set MAS1[V]
	rlwinm	%r4, %r4, 0, 4, 1	// Clear MAS0[TLBSEL]
	rlwinm	%r9, %r5, 18, 26, 31	// r9 = MAS8[TLPID]
	rlwimi	%r9, %r12, 16, 0, 1	// r9 |= MAS8[TGS, VF]

	mtspr	SPR_MAS3, %r11
	mtspr	SPR_MAS0, %r4
	mtspr	SPR_MAS1, %r7
	mtspr	SPR_MAS2, %r8
	mtspr	SPR_MAS7, %r6
	mtspr	SPR_MAS8, %r9
	isync
	tlbwe

	lwz	%r5, LONGBYTES*11(%r2)
	lwz	%r6, LONGBYTES*11+4(%r2)
	LOAD	%r7, LONGBYTES*10(%r2)
	lwz	%r9, LONGBYTES*11+8(%r2)
	lwz	%r10, LONGBYTES*11+12(%r2)
	mtspr	SPR_MAS0, %r5
	mtspr	SPR_MAS1, %r6
	mtspr	SPR_MAS2, %r7
	mtspr	SPR_MAS3, %r9
	mtspr	SPR_MAS7, %r10
	isync

	LOAD	%r12, LONGBYTES*15(%r2)
	LOAD	%r3, LONGBYTES*0(%r2)
	LOAD	%r4, LONGBYTES*1(%r2)
	LOAD	%r5, LONGBYTES*2(%r2)
	LOAD	%r6, LONGBYTES*3(%r2)
	LOAD	%r7, LONGBYTES*4(%r2)
	LOAD	%r8, LONGBYTES*5(%r2)
	LOAD	%r9, LONGBYTES*6(%r2)
	LOAD	%r10, LONGBYTES*7(%r2)
	mtcr	%r12
	LOAD	%r11, LONGBYTES*8(%r2)
	LOAD	%r12, LONGBYTES*9(%r2)
	dcbi	0, %r2
#ifdef CONFIG_LIBOS_64BIT
	addi	%r2, %r2, 64
	dcbi	0, %r2
#endif
	mfspr	%r2, SPR_SPRG1
	rfi

hv_dtlb_miss:
	andi.	%r10, %r11, ESR_EPID
	bne	epid_hv
tlb_miss_slow:
	LOAD	%r12, LONGBYTES*15(%r2)
	LOAD	%r3, LONGBYTES*0(%r2)
	LOAD	%r4, LONGBYTES*1(%r2)
	LOAD	%r5, LONGBYTES*2(%r2)
	LOAD	%r6, LONGBYTES*3(%r2)
	LOAD	%r7, LONGBYTES*4(%r2)
	LOAD	%r8, LONGBYTES*5(%r2)
	LOAD	%r9, LONGBYTES*6(%r2)
	LOAD	%r10, LONGBYTES*7(%r2)
	LOAD	%r11, LONGBYTES*8(%r2)
	
	beq	%cr7, itlb_miss_slow

	mtcr	%r12
	LOAD	%r12, LONGBYTES*9(%r2)
	dcbi	0, %r2
#ifdef CONFIG_LIBOS_64BIT
	addi	%r2, %r2, 64
	dcbi	0, %r2
#endif
	mfspr	%r2, SPR_SPRG1
	b	int_data_tlb_error

itlb_miss_slow:
	mtcr	%r12
	LOAD	%r12, LONGBYTES*9(%r2)
	dcbi	0, %r2
#ifdef CONFIG_LIBOS_64BIT
	addi	%r2, %r2, 64
	dcbi	0, %r2
#endif
	mfspr	%r2, SPR_SPRG1
	b	int_inst_tlb_error
