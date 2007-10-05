/*
 *
 *
 *
 */

#include "spr.h"
#include "tlb.h"
#include "asm.h"
#include "assym.s"

.text

.globl _start
_start:

	/* Issue INV_ALL Invalidate on TLB0 */
	li      %r16, 0x04
	tlbivax	0, %r16
	isync
	msync
	nop

/*
 * Use tblsx to locate the TLB1 entry that maps kernel code
 */
	bl	1f			/* Current address */
1:	mflr	%r15

	/* Find entry that maps current address */
	mfspr	%r17, SPR_PID0
	slwi	%r17, %r17, MAS6_SPID0_SHIFT
	mtspr	SPR_MAS6, %r17
	isync
	tlbsx	0, %r15

	/* Copy entry number to r10 */
	mfspr	%r17, SPR_MAS0
	rlwinm	%r10, %r17, 16, 28, 31

	/* Invalidate TLB1, skipping our entry. */

	mfspr	%r17, SPR_TLB1CFG	/* Get number of entries */
	andi.	%r17, %r17, TLBCFG_NENTRY_MASK@l
	li	%r16, 0			/* Start from Entry 0 */

2:	lis	%r15, MAS0_TLBSEL1@h	/* Select TLB1 */
	rlwimi	%r15, %r16, 16, 12, 15
	mtspr	SPR_MAS0, %r15
	isync
	tlbre
	mfspr	%r15, SPR_MAS1
	cmpw	%r16, %r10
	beq	3f
	/* Clear VALID and IPROT bits for other entries */
	rlwinm	%r15, %r15, 0, 2, 31
	mtspr	SPR_MAS1, %r15
	isync
	tlbwe
	isync
	msync
3:	addi	%r16, %r16, 1           /* increment to next index */
	cmpw	%r16, %r17		/* Check if this is the last entry */
	bne	2b

/*
 * Create temporary mapping in the other Address Space
 */
	lis	%r17, MAS0_TLBSEL1@h	/* Select TLB1 */
	rlwimi	%r17, %r10, 16, 12, 15	/* Select our entry */
	mtspr	SPR_MAS0, %r17
	isync
	tlbre				/* Read it in */

	/* Prepare and write temp entry */
	lis	%r17, MAS0_TLBSEL1@h	/* Select TLB1 */
	addi	%r11, %r10, 0x1		/* Use next entry. */
	rlwimi	%r17, %r11, 16, 12, 15	/* Select temp entry */
	mtspr	SPR_MAS0, %r17
	isync

	mfspr	%r16, SPR_MAS1
	li	%r15, 1			/* AS 1 */
	rlwimi	%r16, %r15, 12, 19, 19
	mtspr	SPR_MAS1, %r16
	li	%r17, 0
	rlwimi	%r16, %r17, 0, 8, 15	/* Global mapping, TID=0 */
	isync

	tlbwe
	isync
	msync

	mfmsr	%r16
	ori	%r16, %r16, 0x30	/* Switch to AS 1. */

	bl	4f			/* Find current execution address */
4:	mflr	%r15
	addi	%r15, %r15, 20		/* Increment to instruction after rfi */
	mtspr	SPR_SRR0, %r15
	mtspr	SPR_SRR1, %r16
	rfi				/* Switch context */

/*
 * Invalidate initial entry
 */
	mr	%r22, %r10
	bl	tlb1_inval_entry

/*
 * Setup final mapping in TLB1[1] and switch to it
 */
	/* Final kernel mapping, map in 16 MB of RAM */
	lis	%r16, MAS0_TLBSEL1@h	/* Select TLB1 */
	li	%r17, 1			/* Entry 1 */
	rlwimi	%r16, %r17, 16, 12, 15
	mtspr	SPR_MAS0, %r16
	isync

	li	%r16, (TLB_SIZE_16M << MAS1_TSIZE_SHIFT)@l
	oris	%r16, %r16, (MAS1_VALID | MAS1_IPROT)@h
	mtspr	SPR_MAS1, %r16
	isync

	lis	%r19, HVBASE@h
	ori	%r19, %r19, HVBASE@l
	mtspr	SPR_MAS2, %r19		/* Set final EPN, clear WIMG */
	isync

	bl	5f
5:	mflr	%r16			/* Use current address */
	lis	%r18, 0xff00		/* 16MB alignment mask */
	and	%r16, %r16, %r18
	mr	%r25, %r16		/* Copy kernel load address */
	ori	%r16, %r16, (MAS3_SX | MAS3_SW | MAS3_SR)@l
	mtspr	SPR_MAS3, %r16		/* Set RPN and protection */
	isync
	tlbwe
	isync
	msync

	/* Switch to the above TLB1[1] mapping */
	lis	%r18, 0x00ff		/* 16MB offset mask */
	ori	%r18, %r18, 0xffff
	bl	6f
6:	mflr	%r20			/* Use current address */
	and	%r20, %r20, %r18	/* Offset from kernel load address */
	add	%r20, %r20, %r19	/* Move to kernel virtual address */
	addi	%r20, %r20, 32		/* Increment to instr. after rfi  */
	li	%r21, 0x200
	mtspr   SPR_SRR0, %r20
	mtspr   SPR_SRR1, %r21
	rfi

#if 0
	/* Save kernel load address for later use */
	lis	%r24, kernload@ha
	addi	%r24, %r24, kernload@l
	stw	%r25, 0(%r24)
#endif

/*
 * Invalidate temp mapping
 */
	mr	%r22, %r11
	bl	tlb1_inval_entry

/*
 * set up a stack
 * -TODO: eventually this needs to be allocated per core--
 *        need to programmatically compute this
 *        from some base
 */
	lis	%r1, uv_stack_top@ha
	addi	%r1, %r1, uv_stack_top@l
	addi	%r1, %r1, -4

/* 
 * set SPRG0 to point to the pcpu struct
 * (for the exception handlers)
 * -TODO: this will need to be per-core eventually
 */
	lis	%r21, pcpu@ha
	addi	%r21, %r21, pcpu@l
	mtsprg0	%r21

/*
 * Intialise exception vector offsets
 */
	bl	ivor_setup

	.long   0

_busy_loop:
	b	_busy_loop


tlb1_inval_entry:
	lis	%r17, MAS0_TLBSEL1@h	/* Select TLB1 */
	rlwimi	%r17, %r22, 16, 12, 15	/* Select our entry */
	mtspr	SPR_MAS0, %r17
	isync
	tlbre				/* Read it in */

	li	%r16, 0
	mtspr	SPR_MAS1, %r16
	isync
	tlbwe
	isync
	msync
	blr

ivor_setup:
	/* Set base address of interrupt handler routines */
	lis	%r21, interrupt_vector_base@h
	mtspr	SPR_IVPR, %r21

	/* Assign interrupt handler routines offsets */
	li	%r21, int_critical_input@l
	mtspr	SPR_IVOR0, %r21
	li	%r21, int_machine_check@l
	mtspr	SPR_IVOR1, %r21
	li	%r21, int_data_storage@l
	mtspr	SPR_IVOR2, %r21
	li	%r21, int_instr_storage@l
	mtspr	SPR_IVOR3, %r21
	li	%r21, int_external_input@l
	mtspr	SPR_IVOR4, %r21
	li	%r21, int_alignment@l
	mtspr	SPR_IVOR5, %r21
	li	%r21, int_program@l
	mtspr	SPR_IVOR6, %r21
	li	%r21, int_syscall@l
	mtspr	SPR_IVOR8, %r21
	li	%r21, int_decrementer@l
	mtspr	SPR_IVOR10, %r21
	li	%r21, int_fixed_interval_timer@l
	mtspr	SPR_IVOR11, %r21
	li	%r21, int_watchdog@l
	mtspr	SPR_IVOR12, %r21
	li	%r21, int_data_tlb_error@l
	mtspr	SPR_IVOR13, %r21
	li	%r21, int_inst_tlb_error@l
	mtspr	SPR_IVOR14, %r21
	li	%r21, int_debug@l
	mtspr	SPR_IVOR15, %r21
	li	%r21, int_perf_mon@l
	mtspr	SPR_IVOR35, %r21
	li	%r21, int_doorbell@l
	mtspr	SPR_IVOR36, %r21
	li	%r21, int_doorbell_critical@l
	mtspr	SPR_IVOR37, %r21
	li	%r21, int_guest_doorbell@l
	mtspr	SPR_IVOR38, %r21
	li	%r21, int_guest_doorbell_critical@l
	mtspr	SPR_IVOR39, %r21
	li	%r21, int_hypercall@l
	mtspr	SPR_IVOR40, %r21
	li	%r21, int_ehpriv@l
	mtspr	SPR_IVOR41, %r21

	blr


	.data
	.align 12 /* page align */
pcpu:
	.space PCPU_SIZE

	/* For now allocate 4 pages for the 4 exception stacks
	 * normal, critical, machine check, debug.
	 * Eventually, these stacks need to be in a per hypervisor
         * data area.
         */

	.section .bss
	.align 12  /* page align */
uv_stack_bottom:
	.space 4096
	.globl uv_stack_top
uv_stack_top:
except_stack_bottom:
	.space 4096
	.globl except_stack_top
except_stack_top:
crit_except_stack_bottom:
	.space 4096
	.globl crit_except_stack_top
crit_except_stack_top:
machchk_except_stack_bottom:
	.space 4096
	.globl machchk_except_stack_top
machchk_except_stack_top:
dbg_except_stack_bottom:
	.space 4096
	.globl dbg_except_stack_top
dbg_except_stack_top:
