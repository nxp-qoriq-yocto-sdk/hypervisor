/*
 * Exception vectors
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

#include <spr.h> 
#include <percpu.h>
#include "assym.s"

/* TODO
 *    -watchdog should be critical
 *    -uncomment IVOR init in head.S
 *    -don't need any TLB stuff in HCPU
 *    -need separate exit for guest only exceptions, if it didn't come
 *     guest it's fatal
 *    -hypercall exception doesn't need to save registers
 *     (but if it doesn't, it should zero out volatiles to avoid
 *      information leakage).
 */

/* SPRG allocation:
 * SPRG0: per-CPU data pointer
 * SPRG1: normal exception scratch
 * SPRG2: critical exception scratch
 * SPRG3: unused
 * SPRG4-7: reserved for guest
 * SPRG8: machine check scratch
 * SPRG9: debug exception scratch
 */

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

	.macro	exception, handler, exc, name, scratch, \
	        srr0, srr1, uvstack, savearea, ret
	.global \name
	.align	4
\name:
	mtspr	\scratch, %r2
	mfspr	%r2, SPR_SPRG0

	STORE	%r1, \savearea(%r2)
	\uvstack
	addi	%r1, %r1, UVSTACK_SIZE - FRAMELEN

	STORE	%r0, LONGBYTES*0 + FRAME_GPREGS(%r1)

	/* fixme for 64-bit */
	stmw	%r3, LONGBYTES*3 + FRAME_GPREGS(%r1)

	LOAD	%r3, \savearea(%r2)
	mfspr	%r4, \scratch
	mflr	%r5
	mfctr	%r6
	mfcr	%r7
	mfxer	%r8
	mfspr	%r9, \srr0
	mfspr	%r10, \srr1
	mfspr	%r11, SPR_DEAR
	mfspr	%r12, SPR_ESR
	li	%r13, \exc

	STORE	%r3, LONGBYTES*1 + FRAME_GPREGS(%r1)
	STORE	%r4, LONGBYTES*2 + FRAME_GPREGS(%r1)
	STORE	%r5, FRAME_LR(%r1)
	STORE	%r6, FRAME_CTR(%r1)
	stw	%r7, FRAME_CR(%r1)
	stw	%r8, FRAME_XER(%r1)
	STORE	%r9, FRAME_SRR0(%r1)
	STORE	%r10, FRAME_SRR1(%r1)
	STORE	%r11, FRAME_DEAR(%r1)
	STORE	%r12, FRAME_ESR(%r1)
	stw	%r13, FRAME_EXC(%r1)
	
	andis.	%r0, %r10, MSR_GS@h
	beq	1f
	
	mfspr	%r3, SPR_EPLC
	mfspr	%r4, SPR_EPSC
	mfspr	%r5, SPR_PID0
	mfspr	%r6, SPR_LPID
	
	stw	%r3, FRAME_EPLC(%r1)
	stw	%r4, FRAME_EPSC(%r1)
	
	/* Set the PID, LPID, guest/hypervisor, and user/kernel state for
	 * external PID access.  We don't set AS0/AS1 in EPLC, as it
	 * depends on whether it's a data or instruction access which MSR
	 * bit we copy from.  However, we shouldn't be storing to
	 * instruction addresses, so EPSC assumes data.
	 */
	
	rlwimi	%r3, %r5, EPCBIT_EPID_SHIFT, EPC_EPID
	rlwimi	%r4, %r5, EPCBIT_EPID_SHIFT, EPC_EPID
	rlwimi	%r3, %r6, EPCBIT_ELPID_SHIFT, EPC_ELPID
	rlwimi	%r4, %r6, EPCBIT_ELPID_SHIFT, EPC_ELPID

	rlwimi	%r3, %r10, MSRBIT_PR - EPCBIT_EPR, EPC_EPR
	rlwimi	%r4, %r10, MSRBIT_PR - EPCBIT_EPR, EPC_EPR

	rlwimi	%r4, %r10, MSRBIT_DS - EPCBIT_EAS, EPC_EAS
	
	/* Always guest access */
	oris	%r3, %r3, EPC_EGS@h
	oris	%r4, %r4, EPC_EGS@h
	
	mtspr	SPR_EPLC, %r3
	mtspr	SPR_EPSC, %r4

1:	mr	%r3, %r1
	bl	\handler

	LOAD	%r5, FRAME_LR(%r1)
	LOAD	%r6, FRAME_CTR(%r1)
	lwz	%r7, FRAME_CR(%r1)
	lwz	%r8, FRAME_XER(%r1)
	LOAD	%r9, FRAME_SRR0(%r1)
	LOAD	%r10, FRAME_SRR1(%r1)

	mtlr	%r5
	mtctr	%r6
	mtcr	%r7
	mtxer	%r8
	mtspr	\srr0, %r9
	mtspr	\srr1, %r10

	andis.	%r0, %r10, MSR_GS@h
	beq	2f

	lwz	%r3, FRAME_EPLC(%r1)
	lwz	%r4, FRAME_EPSC(%r1)
	mtspr	SPR_EPLC, %r3
	mtspr	SPR_EPSC, %r4

2:	/* fixme for 64-bit */
	lmw	%r2, LONGBYTES*2 + FRAME_GPREGS(%r1)

	LOAD	%r0, LONGBYTES*0 + FRAME_GPREGS(%r1)
	LOAD	%r1, LONGBYTES*1 + FRAME_GPREGS(%r1)

	\ret
	.endm

	.macro	baseuvstack
	LOAD	%r1, HCPU_GCPU(%r2)
#if GCPU_UVSTACK != 0
	addi	%r1, %r1, GCPU_UVSTACK
#endif
	.endm

	.macro	baseexception, exc, name, handler
	exception \handler \exc \name SPR_SPRG1 SPR_SRR0 SPR_SRR1 \
	          baseuvstack HCPU_NORMSAVE rfi
	.endm

	.macro	crituvstack
	addi	%r1, %r2, HCPU_CRITSTACK
	.endm

	.macro	critexception, exc, name, handler
	exception \handler \exc \name SPR_SPRG2 SPR_CSRR0 SPR_CSRR1 \
	          crituvstack HCPU_CRITSAVE rfci
	.endm

	.macro	mcheckuvstack
	addi	%r1, %r2, HCPU_MCHECKSTACK
	.endm

	.macro	machinecheck, exc, name, handler
	exception \handler \exc \name SPR_SPRG8 SPR_MCSRR0 SPR_MCSRR1 \
	          mcheckuvstack HCPU_MACHKSAVE rfmci
	.endm

	.macro	debuguvstack
	addi	%r1, %r2, HCPU_DEBUGSTACK
	.endm

	.macro	debugexception, exc, name, handler
	exception \handler \exc \name SPR_SPRG9 SPR_DSRR0 SPR_DSRR1 \
	          debuguvstack HCPU_DBGSAVE rfdi
	.endm

	.global interrupt_vector_base
	.align 5
interrupt_vector_base:
	critexception 0 int_critical_input unknown_exception
	machinecheck 1 int_machine_check unknown_exception
	baseexception 2 int_data_storage unknown_exception
	baseexception 3 int_instr_storage unknown_exception
	baseexception 4 int_external_input unknown_exception
	baseexception 5 int_alignment unknown_exception
	baseexception 6 int_program unknown_exception
	baseexception 8 int_syscall unknown_exception
	baseexception 10 int_decrementer unknown_exception
	baseexception 11 int_fixed_interval_timer unknown_exception
	critexception 12 int_watchdog unknown_exception
	baseexception 13 int_data_tlb_error unknown_exception
	baseexception 14 int_inst_tlb_error unknown_exception
	debugexception 15 int_debug unknown_exception
	baseexception 35 int_perf_mon unknown_exception
	baseexception 36 int_doorbell unknown_exception
	critexception 37 int_doorbell_critical unknown_exception
	baseexception 38 int_guest_doorbell unknown_exception
	critexception 39 int_guest_doorbell_critical unknown_exception
	baseexception 40 int_hypercall unknown_exception
	baseexception 41 int_ehpriv hvpriv

