/* $FreeBSD: src/sys/powerpc/powerpc/trap_subr.S,v 1.14.2.1 2005/08/08 07:02:12 grehan Exp $ */
/* $NetBSD: trap_subr.S,v 1.20 2002/04/22 23:20:08 kleink Exp $	*/

/*-
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
 * Copyright (C) 2006 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * Copyright (C) 2006 Juniper Networks, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spr.h"
#include "assym.s"
#include "trap_booke.h"

/* TODO
 *    -watchdog should be critical
 *    -uncomment IVOR init in head.S
 *    -don't need any TLB stuff in VCPU
 *    -debug int needs to use different SRR0/1
 *    -need separate exit for guest only exceptions, if it didn't come
 *     guest it's fatal
 */

/*
 * SPRG usage notes
 *
 * SPRG0 - VCPU pointer
 * SPRG1 - scratch - all interrupts except TLB miss, critical, machine check
 * SPRG2 - scratch - critical level
 * SPRG3 - ?
 * SPRG4 - for guest OS use
 * SPRG5 - for guest OS use
 * SPRG6 - for guest OS use
 * SPRG6 - for guest OS use
 * SPRG8 - scratch - machine check level
 * SPRG9 - scratch - debug
 *
 */

/* Get the per-CPU data structure */
#define GET_CPUINFO(r) mfsprg0 r

/*
 * Standard interrupt prolog
 *
 * sprg_sp - SPRG{1-3} reg used to temporarily store the SP
 * savearea - temp save area (pc_{tempsave, disisave, critsave, mchksave})
 * isrr0-1 - save restore registers with CPU state at interrupt time (may be
 *           SRR0-1, CSRR0-1, MCSRR0-1
 *
 * 1. saves in the given savearea:
 *   - R30-31
 *   - DEAR, ESR
 *   - xSRR0-1
 *
 * 2. saves CR -> R30
 *
 * 3. switches to kstack if needed
 *
 * 4. notes:
 *   - R31 can be used as scratch register until a new frame is layed on
 *     the stack with FRAME_SETUP
 *
 *   - potential TLB miss: NO. Saveareas are always acessible via TLB1 
 *     permanent entries, and within this prolog we do not dereference any
 *     locations potentially not in the TLB
 */

/*
 * TODO: switch to uv stack if int taken in _guest_ mode 
 *
 */

#define STANDARD_PROLOG(sprg_sp, savearea, isrr0, isrr1)		\
	mtspr	sprg_sp, %r1;		/* Save SP */			\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	stw	%r30, (savearea+CPUSAVE_R30)(%r1);			\
	stw	%r31, (savearea+CPUSAVE_R31)(%r1); 			\
	mfdear	%r30;		 					\
	mfesr	%r31;							\
	stw	%r30, (savearea+CPUSAVE_DEAR)(%r1); 			\
	stw	%r31, (savearea+CPUSAVE_ESR)(%r1) ; 			\
	mfspr	%r30, isrr0;						\
	mfspr	%r31, isrr1;	 	/* MSR at interrupt time */	\
	stw	%r30, (savearea+CPUSAVE_SRR0)(%r1);			\
	stw	%r31, (savearea+CPUSAVE_SRR1)(%r1);			\
	isync;			 					\
	mfspr	%r1, sprg_sp;	 	/* Restore SP */		\
	mfcr	%r30;		 	/* Save CR */			\
	/* switch to per-thread kstack if intr taken in user mode */	\
	mtcr	%r31;			/* MSR at interrupt time  */	\
	bf	17, 1f;	 /* TODO: needs to test MSR[GS]	*/					\
	GET_CPUINFO(%r1);		/* Per-cpu structure */		\
	lwz	%r1, UVSTACK(%r1); 	/* Per-thread kernel stack */	\
1:	

/*
 * Note: the debug prolog is different because they take
 * extra step to preserve SRR0/1 incase we are debugging
 * exception handlers-- don't understand this completely...
 *
 */
#define	STANDARD_DEBUG_PROLOG(sprg_sp, savearea, isrr0, isrr1)		\
	mtspr	sprg_sp, %r1;           /* Save SP */                   \
	GET_CPUINFO(%r1);               /* Per-cpu structure */         \
	stw	%r30, (savearea+CPUSAVE_R30)(%r1);                      \
	stw	%r31, (savearea+CPUSAVE_R31)(%r1);                      \
	mfdear	%r30;                                                   \
	mfesr	%r31;                                                   \
	stw	%r30, (savearea+CPUSAVE_DEAR)(%r1);                     \
	stw	%r31, (savearea+CPUSAVE_ESR)(%r1) ;                     \
	mfspr	%r30, isrr0;                                            \
	mfspr	%r31, isrr1;            /* MSR at interrupt time */     \
	stw	%r30, (savearea+CPUSAVE_SRR0)(%r1);                     \
	stw	%r31, (savearea+CPUSAVE_SRR1)(%r1);                     \
	mfspr	%r30, SPR_SRR0;                                         \
	mfspr	%r31, SPR_SRR1;         /* MSR at interrupt time */     \
	stw	%r30, (savearea+CPUSAVE_SRR0+8)(%r1);                   \
	stw	%r31, (savearea+CPUSAVE_SRR1+8)(%r1);                   \
	isync;                                                          \
	mfspr	%r1, sprg_sp;           /* Restore SP */                \
	mfcr	%r30;                   /* Save CR */                   \
	/* switch to per-thread kstack if intr taken in user mode */    \
	mtcr	%r31;                   /* MSR at interrupt time  */    \
	bf	17, 1f;                                                 \
	GET_CPUINFO(%r1);               /* Per-cpu structure */         \
	lwz	%r1, UVSTACK(%r1);    /* Per-thread kernel stack */   \
1:

/*
 * FRAME_SETUP assumes:
 *	SPRG{1-3}	SP at the time interrupt occured
 *	savearea	r30-r31, DEAR, ESR, xSRR0-1
 *	r30		CR
 *	r31		scratch	
 *	r1		kernel stack
 *
 * sprg_sp - SPRG reg containing SP at the time interrupt occured
 * savearea - temp save
 * exc - exception number (EXC_xxx)
 *
 * 1. sets a new frame
 * 2. saves in the frame:
 *   - R0, R1 (SP at the time of interrupt), R2, LR, CR
 *   - R3-31 (R30-31 first restored from savearea)
 *   - XER, CTR, DEAR, ESR (from savearea), xSRR0-1
 *
 */
#define	FRAME_SETUP(sprg_sp, savearea, exc)				\
	mfspr	%r31, sprg_sp;		/* get saved SP */		\
	/* establish a new stack frame and put everything on it */	\
	stwu	%r31, -FRAMELEN(%r1);					\
	stw	%r0, FRAME_0(%r1);	/* save r0 in the trapframe */	\
	stw	%r31, FRAME_1(%r1);	/* save SP   "      "       */	\
	stw	%r2, FRAME_2(%r1);	/* save r2   "      "       */	\
	mflr	%r31;		 					\
	stw	%r31, FRAME_LR(%r1);	/* save LR   "      "       */	\
	stw	%r30, FRAME_CR(%r1);	/* save CR   "      "       */	\
	GET_CPUINFO(%r2);						\
	lwz	%r30, (savearea+CPUSAVE_R30)(%r2); /* get saved r30 */	\
	lwz	%r31, (savearea+CPUSAVE_R31)(%r2); /* get saved r31 */	\
	/* save R3-31 */						\
	stmw	%r3,  FRAME_3(%r1) ;					\
	/* save DEAR, ESR */						\
	lwz	%r28, (savearea+CPUSAVE_DEAR)(%r2);			\
	lwz	%r29, (savearea+CPUSAVE_ESR)(%r2);			\
	stw	%r28, FRAME_DEAR(1);					\
	stw	%r29, FRAME_ESR(1);					\
	/* save XER, CTR, exc number */					\
	mfxer	%r3;							\
	mfctr	%r4;							\
	stw	%r3, FRAME_XER(1);					\
	stw	%r4, FRAME_CTR(1);					\
	li	%r5, exc;						\
	stw	%r5, FRAME_EXC(1);					\
	/* save xSSR0-1 */						\
	lwz	%r30, (savearea+CPUSAVE_SRR0)(%r2);			\
	lwz	%r31, (savearea+CPUSAVE_SRR1)(%r2);			\
	stw	%r30, FRAME_SRR0(1);					\
	stw	%r31, FRAME_SRR1(1)


/*
 *
 * isrr0-1 - save restore registers to restore CPU state to (may be
 *           SRR0-1, CSRR0-1, MCSRR0-1
 *
 * Notes:
 *  - potential TLB miss: YES. The deref'd kstack may be not covered
 */
#define	FRAME_LEAVE(isrr0, isrr1)					\
	/* restore CTR, XER, LR, CR */					\
	lwz	%r4, FRAME_CTR(%r1);					\
	lwz	%r5, FRAME_XER(%r1);					\
	lwz	%r6, FRAME_LR(%r1);					\
	lwz	%r7, FRAME_CR(%r1);					\
	mtctr	%r4;							\
	mtxer	%r5;							\
	mtlr	%r6;							\
	mtcr	%r7;							\
	/* restore xSRR0-1 */						\
	lwz	%r30, FRAME_SRR0(%r1);				\
	lwz	%r31, FRAME_SRR1(%r1);				\
	mtspr	isrr0, %r30;						\
	mtspr	isrr1, %r31;						\
	/* restore R2-31, SP */						\
	lmw	%r2, FRAME_2(%r1) ;					\
	lwz	%r0, FRAME_0(%r1);					\
	lwz	%r1, FRAME_1(%r1);					\
	isync


#define INTERRUPT(label)						\
	.globl	label;							\
	.align	5;							\
	label:

/*
 * Interrupt handling routines in BookE can be flexibly placed and do not have
 * to live in pre-defined vectors location. Note they need to be TLB-mapped at
 * all times in order to be able to handle exceptions. We thus arrange for
 * them to be part of kernel text which is always TLB-accessible.
 *
 * The interrupt handling routines have to be 16 bytes aligned: we align them
 * to 32 bytes (cache line length) which supposedly performs better.
 *
 */
	.text
	.globl interrupt_vector_base
	.align 5
interrupt_vector_base:

/****************************************
 * Critical input interrupt - critical
 ****************************************/
INTERRUPT(int_critical_input)
	STANDARD_PROLOG(SPR_SPRG2, VCPU_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, VCPU_CRITSAVE, EXC_CRIT)
	bl	critical_interrupt
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci


/****************************************
 * Machine check interrupt - machine check
 ****************************************/
INTERRUPT(int_machine_check)
	STANDARD_PROLOG(SPR_SPRG8, VCPU_MACHKSAVE, SPR_MCSRR0, SPR_MCSRR1)
	FRAME_SETUP(SPR_SPRG8, VCPU_MACHKSAVE, EXC_MCHK)
	bl	powerpc_mchk_interrupt
	FRAME_LEAVE(SPR_MCSRR0, SPR_MCSRR1)
	rfmci


/****************************************
 * Data storage interrupt - normal, guest only
 ****************************************/
INTERRUPT(int_data_storage)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_DSI)
	b	trap_common


/****************************************
 * Instruction storage interrupt - normal, guest only
 ****************************************/
INTERRUPT(int_instr_storage)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_ISI)
	b	trap_common


/****************************************
 * External input interrupt - normal, guest only
 ****************************************/
INTERRUPT(int_external_input)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_EXI)
	b	trap_common


/****************************************
 * Alignment - normal
 ****************************************/
INTERRUPT(int_alignment)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_ALI)
	b	trap_common


/****************************************
 * Program - normal
 ****************************************/
INTERRUPT(int_program)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_PGM)
	b	trap_common


/****************************************
 * System call - normal - should never occur
 ****************************************/
INTERRUPT(int_syscall)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_SC)
	b	trap_common


/****************************************
 * Decrementer interrupt - normal
 ****************************************/
INTERRUPT(int_decrementer)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_DECR)
	b	trap_common


/****************************************
 * Fixed interval timer - normal
 ****************************************/
INTERRUPT(int_fixed_interval_timer)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_FIT)
	b	trap_common


/****************************************
 * Watchdog interrupt - critical
 ****************************************/
INTERRUPT(int_watchdog)
	STANDARD_PROLOG(SPR_SPRG2, VCPU_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, VCPU_CRITSAVE, EXC_WDOG)
	bl	critical_interrupt
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci

/****************************************
 * Data TLB error interrupt - normal, guest only
 ****************************************/
INTERRUPT(int_data_tlb_error)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_DTLB)
	b	trap_common

/****************************************
 * Inst TLB error interrupt - normal, guest only
 ****************************************/
INTERRUPT(int_inst_tlb_error)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_ITLB)
	b	trap_common

/****************************************
 * Perf monitor interrupt - normal
 ****************************************/
INTERRUPT(int_perf_mon)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_PERF)
	b	trap_common

/****************************************
 * doorbell - normal
 ****************************************/
INTERRUPT(int_doorbell)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_DOORBELL)
	b	trap_common

/****************************************
 * doorbell critical - critical
 ****************************************/
INTERRUPT(int_doorbell_critical)
	STANDARD_PROLOG(SPR_SPRG2, VCPU_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, VCPU_CRITSAVE, EXC_DOORBELLC)
	bl	critical_interrupt
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci

/****************************************
 * guest doorbell - normal
 ****************************************/
INTERRUPT(int_guest_doorbell)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_GDOORBELL)
	b	trap_common

/****************************************
 * guest doorbell critical - normal
 ****************************************/
INTERRUPT(int_guest_doorbell_critical)
	STANDARD_PROLOG(SPR_SPRG2, VCPU_CRITSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG2, VCPU_CRITSAVE, EXC_GDOORBELLC)
	bl	critical_interrupt
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci

/****************************************
 * hypercall - normal
 ****************************************/
INTERRUPT(int_hypercall)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_HCALL)
	b	trap_common

/****************************************
 * embedded hypervisor priv - normal
 ****************************************/
INTERRUPT(int_ehpriv)
	STANDARD_PROLOG(SPR_SPRG1, VCPU_NORMSAVE, SPR_SRR0, SPR_SRR1)
	FRAME_SETUP(SPR_SPRG1, VCPU_NORMSAVE, EXC_EHPRIV)
	b	trap_common

	.globl	interrupt_vector_top
interrupt_vector_top:

/****************************************
 * Debug interrupt
 ****************************************/
INTERRUPT(int_debug)
	STANDARD_DEBUG_PROLOG(SPR_SPRG9, VCPU_DBGSAVE, SPR_CSRR0, SPR_CSRR1)
	FRAME_SETUP(SPR_SPRG9, VCPU_DBGSAVE, EXC_DEBUG)
	lwz     %r3, (VCPU_DBGSAVE+CPUSAVE_SRR0)(%r2);
	lis	%r4, interrupt_vector_base@ha
	addi	%r4, %r4, interrupt_vector_base@l
	cmplw	cr0, %r3, %r4
	blt	1f
	lis	%r4, interrupt_vector_top@ha
	addi	%r4, %r4, interrupt_vector_top@l
	cmplw	cr0, %r3, %r4
	bge	1f
	/* Disable single-stepping for the interrupt handlers. */
	lwz	%r3, FRAME_SRR1(%r1);
	rlwinm  %r3,%r3,0,23,21
	stw	%r3, FRAME_SRR1(%r1);
	/* Restore srr0 and srr1 as they could have been clobbered. */
	lwz     %r3, (VCPU_DBGSAVE+CPUSAVE_SRR0+8)(%r2);
	mtspr   SPR_SRR0, %r3
	lwz     %r4, (VCPU_DBGSAVE+CPUSAVE_SRR1+8)(%r2);
	mtspr   SPR_SRR1, %r4
	b	9f
1:
	bl      trap
	b	trapexit
9:
	FRAME_LEAVE(SPR_CSRR0, SPR_CSRR1)
	rfci


/********************************
 * Common trap code
 ********************************/
trap_common:
	/* Call C trap dispatcher */
	mr	%r3, %r1
	bl	trap

	.globl	trapexit		/* exported for db_backtrace use */
trapexit:
	FRAME_LEAVE(SPR_SRR0, SPR_SRR1)
	rfi



