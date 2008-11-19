/** @file
 */
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

/** Wait until a guest-event critical interrupt is delivered.
 *
 * Don't call this directly; only return to it with waiting_for_gevent
 * set, the stack reset, and critical interrupts enabled.
 */
.global wait_for_gevent_loop
wait_for_gevent_loop:
	wait
	b	wait_for_gevent_loop

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
