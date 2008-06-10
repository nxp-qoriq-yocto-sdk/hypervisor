/* @file
 * Trap handling
 */
/*
 * Copyright (C) 2007-2008 Freescale Semiconductor, Inc.
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


#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <libos/console.h>
#include <libos/core-regs.h>
#include <libos/mpic.h>

#include <hv.h>
#include <percpu.h>
#include <timers.h>
#include <guestmemio.h>
#include <vpic.h>
#include <vmpic.h>

/* Do not use this when entering via guest doorbell, since that saves
 * state in gsrr rather than srr, despite being directed to the
 * hypervisor.
 */

void reflect_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (__builtin_expect(!(regs->srr1 & MSR_GS), 0)) {
		crashing = 1;
		printf("unexpected trap in hypervisor\n");
		dump_regs(regs);
		stopsim();
	}

	assert(regs->exc >= 0 && regs->exc < sizeof(gcpu->ivor) / sizeof(int));

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);
	mtspr(SPR_GESR, mfspr(SPR_ESR));

	regs->srr0 = gcpu->ivpr | gcpu->ivor[regs->exc];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE;
}

void guest_doorbell(trapframe_t *regs)
{
	unsigned long gsrr1 = mfspr(SPR_GSRR1);
	gcpu_t *gcpu = get_gcpu();
	interrupt_t *irq;

	assert(gsrr1 & MSR_GS);
	assert(gsrr1 & MSR_EE);

	regs->srr0 = mfspr(SPR_GSRR0);
	regs->srr1 = gsrr1;

	/* check for pending virtual interrupts */
	if (gcpu->gdbell_pending & GCPU_PEND_VIRQ) {
		if (mpic_coreint) {
			irq = vpic_iack();
			if (irq) {
				vmpic_interrupt_t *vmirq = irq->priv;
				mtspr(SPR_GEPR, vmirq->handle);
			} else {
				goto no_virq;
			}
		}
		
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_EXT_INT];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		return;
	}

no_virq:
	/* Then, check for a FIT. */
	if (gcpu->gdbell_pending & GCPU_PEND_FIT) {
		run_deferred_fit();

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_FIT];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		return;
	}

	/* Then, check for a decrementer. */
	if (gcpu->gdbell_pending & GCPU_PEND_DECR) {
		run_deferred_decrementer();

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DECR];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		return;
	}

	if (gcpu->gdbell_pending & GCPU_PEND_TCR_FIE)
		enable_tcr_fie();
	if (gcpu->gdbell_pending & GCPU_PEND_TCR_DIE)
		enable_tcr_die();

	if (gcpu->gdbell_pending & GCPU_PEND_MSGSND) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_MSGSND);
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DOORBELL];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		return;
	}
}

/**
 * Reflect a guest critical doorbell
 *
 * This function is called whenever the hypervisor receives a critical
 * doorbell exception.  This happens when the hypervisor itself executes a
 * msgsnd instruction where the message type is G_DBELL_CRIT.
 *
 * Note that there is no regs->csrr0 or regs->csrr1.  The exception handler
 * prologue and epilogue (in exception.S) uses regs->srr0 and regs->srr1 to
 * store the values of CSSR0 and CSSR1, respectively.
 */
void guest_critical_doorbell(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (gcpu->gdbell_pending & GCPU_PEND_MSGSNDC) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_MSGSNDC);

		/*
		 * We save the values of CSSR0 and CSSR1 into gcpu for emu_mfspr() and
		 * emu_mtspr().  Since there are no GCSRR0 and GCSRR1 registers, the
		 * guest can't access CSRR0 or CSRR1 directly when the hypervisor is
		 * running -- they need to be emulated.
		 */
		gcpu->csrr0 = regs->srr0;
		gcpu->csrr1 = regs->srr1;

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DOORBELLC];
		regs->srr1 &= (MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		return;
	}
}

void reflect_mcheck(trapframe_t *regs, register_t mcsr, uint64_t mcar)
{
	gcpu_t *gcpu = get_gcpu();

	gcpu->mcsr = mcsr;
	gcpu->mcar = mcar;

	gcpu->mcsrr0 = regs->srr0;
	gcpu->mcsrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_MCHECK];
	regs->srr1 &= MSR_GS | MSR_UCLE;
}

typedef struct {
	unsigned long addr, handler;
} extable;

extern extable extable_begin, extable_end;

static void abort_guest_access(trapframe_t *regs, int stat)
{
	regs->gpregs[3] = stat;

	for (extable *ex = &extable_begin; ex < &extable_end; ex++) {
		if (ex->addr == regs->srr0) {
			regs->srr0 = ex->handler;
			return;
		}
	}

	reflect_trap(regs);
}

void data_storage(trapframe_t *regs)
{
	/* If it's from the guest, then it was a virtualization
	  fault.  Currently, we only use that for bad mappings. */
	if (regs->srr1 & MSR_GS)
		reflect_mcheck(regs, MCSR_MAV | MCSR_MEA, mfspr(SPR_DEAR));
	else if (mfspr(SPR_ESR) & ESR_EPID)
		abort_guest_access(regs, GUESTMEM_TLBERR);
	else
		reflect_trap(regs);
}

void dtlb_miss(trapframe_t *regs)
{
	assert(!(regs->srr1 & MSR_GS));

	if (mfspr(SPR_ESR) & ESR_EPID)
		abort_guest_access(regs, GUESTMEM_TLBMISS);
	else
		reflect_trap(regs);
}
