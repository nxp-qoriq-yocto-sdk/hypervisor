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


#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <libos/console.h>
#include <libos/core-regs.h>
#include <libos/mpic.h>
#include <libos/list.h>

#include <hv.h>
#include <percpu.h>
#include <timers.h>
#include <guestmemio.h>
#include <vpic.h>
#include <vmpic.h>
#include <greg.h>
#include <paging.h>
#include <doorbell.h>

void program_trap(trapframe_t *regs)
{
#ifdef CONFIG_DEBUG_STUB
	gcpu_t *gcpu = get_gcpu();
	if (mfspr(SPR_ESR) == ESR_PTR && (regs->srr1 & MSR_GS)
	    && gcpu->guest->stub_ops && gcpu->guest->stub_ops->debug_interrupt
	    && !gcpu->guest->stub_ops->debug_interrupt(regs))
		return;
#endif

	reflect_trap(regs);
}

/* Do not use this when entering via guest doorbell, since that saves
 * state in gsrr rather than srr, despite being directed to the
 * hypervisor.
 */

void reflect_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (__builtin_expect(!(regs->srr1 & MSR_GS), 0)) {
		set_crashing();
		printf("unexpected trap in hypervisor\n");
		dump_regs(regs);
		stopsim();
	}

	assert(regs->exc >= 0 && regs->exc < sizeof(gcpu->ivor) / sizeof(int));

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);
	mtspr(SPR_GESR, mfspr(SPR_ESR));
	mtspr(SPR_GDEAR, mfspr(SPR_DEAR));

	regs->srr0 = gcpu->ivpr | gcpu->ivor[regs->exc];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE;
}

void debug_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	if (!gcpu->guest->guest_debug_mode) {
#ifdef CONFIG_DEBUG_STUB
	if (mfspr(SPR_DBSR) && (regs->srr1 & MSR_GS)
 	    && gcpu->guest->stub_ops && gcpu->guest->stub_ops->debug_interrupt
 	    && !gcpu->guest->stub_ops->debug_interrupt(regs))
		return;
#endif
		set_crashing();
		printf("unexpected debug exception\n");
		dump_regs(regs);
		stopsim();
	}
 
	gcpu->dsrr0 = regs->srr0;
	gcpu->dsrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DEBUG];
	regs->srr1 &= MSR_GS | MSR_UCLE;
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
		goto check_flags;
	}
no_virq:

	/* Then, check for a FIT. */
	if (gcpu->gdbell_pending & GCPU_PEND_FIT) {
		run_deferred_fit();

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_FIT];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		goto check_flags;
	}

	/* Then, check for a decrementer. */
	if (gcpu->gdbell_pending & GCPU_PEND_DECR) {
		run_deferred_decrementer();

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DECR];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		goto check_flags;
	}

	if (gcpu->gdbell_pending & GCPU_PEND_MSGSND) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_MSGSND);
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DOORBELL];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
		goto check_flags;
	}

	if (gcpu->gdbell_pending & GCPU_PEND_TCR_FIE)
		enable_tcr_fie();

	if (gcpu->gdbell_pending & GCPU_PEND_TCR_DIE)
		enable_tcr_die();

	return;

check_flags:
	/* Are there still any pending doorbells?  If so, then we need to
	 * issue another guest doorbell so that we return to this function to
	 * process them.
	 */
	if (gcpu->gdbell_pending)
		send_local_guest_doorbell();
}

/**
 * Reflect a guest critical doorbell
 *
 * This function is called whenever the hypervisor receives a critical
 * doorbell exception.  This happens when the hypervisor itself executes a
 * msgsnd instruction where the message type is G_DBELL_CRIT, which is what
 * send_local_crit_guest_doorbell() does.
 *
 * This function isn't called right away, though.  It is called when both
 * MSR[GS] and MSR[CE] become set.  This is how we can detect when the guest
 * enables MSR[CE].
 *
 * Note that there is no regs->csrr0 or regs->csrr1.  The exception handler
 * prologue and epilogue (in exception.S) uses regs->srr0 and regs->srr1 to
 * store the values of CSSR0 and CSSR1, respectively.
 */
void guest_critical_doorbell(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (gcpu->crit_gdbell_pending & GCPU_PEND_WATCHDOG) {
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_WATCHDOG);

		reflect_watchdog(gcpu, regs);
		goto check_flags;
	}

	if (gcpu->crit_gdbell_pending & GCPU_PEND_MSGSNDC) {
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_MSGSNDC);

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
		goto check_flags;
	}

	return;

check_flags:
	/* Are there still any pending doorbells?  If so, then we need to
	 * issue another guest doorbell so that we return to this function to
	 * process them.
	 */
	if (gcpu->crit_gdbell_pending)
		send_local_crit_guest_doorbell();
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
	for (extable *ex = &extable_begin; ex < &extable_end; ex++) {
		if (ex->addr == regs->srr0) {
			regs->gpregs[3] = stat;
			regs->srr0 = ex->handler;
			return;
		}
	}

	reflect_trap(regs);
}

void data_storage(trapframe_t *regs)
{
	/* If it's from the guest, then it was a virtualization
	 * fault.  Currently, we only use that for bad mappings.
	 * This includes emulated devices that do not have page table entries.
	 */

	if (regs->srr1 & MSR_GS) {
#ifdef CONFIG_DEVICE_VIRT
		guest_t *guest = get_gcpu()->guest;
		unsigned long vaddr = mfspr(SPR_DEAR);
		phys_addr_t paddr;
		vf_range_t *vf;

		// Get the guest physical address from the TLB
		asm volatile("tlbsx 0, %0" : : "r" (vaddr));
		paddr = (mfspr(SPR_MAS3) & MAS3_RPN) | (vaddr & ~MAS3_RPN);

		// Scan our listed of virtualized device ranges
		list_for_each(&guest->vf_list, list) {
			vf = to_container(list, vf_range_t, list);

			if ((paddr >= vf->start) && (paddr <= vf->end)) {
				vf->callback(vf, regs, paddr);
				return;
			}
		}
#endif
		// If we get here, then it's a bad mapping

		int store = mfspr(SPR_ESR) & ESR_ST;
		reflect_mcheck(regs, MCSR_MAV | MCSR_MEA | (store ? MCSR_ST : MCSR_LD),
		               mfspr(SPR_DEAR));
	} else if (mfspr(SPR_ESR) & ESR_EPID){ 
		abort_guest_access(regs, GUESTMEM_TLBERR);
	} else {
		reflect_trap(regs);
	}
}

void inst_storage(trapframe_t *regs)
{
	if (regs->srr1 & MSR_GS) {
		int space = (regs->srr1 & MSR_IS) >> 5;
		int ret = guest_tlb_isi(regs->srr0, space, mfspr(SPR_PID));

		if (unlikely(ret == TLB_MISS_MCHECK)) {
			reflect_mcheck(regs, MCSR_MAV | MCSR_MEA | MCSR_IF, regs->srr0);
			return;
		}
	}

	reflect_trap(regs);
}

void tlb_miss(trapframe_t *regs)
{
	int itlb = regs->exc == EXC_ITLB;
	int guest = likely(regs->srr1 & MSR_GS);
	int epid = !itlb && (mfspr(SPR_ESR) & ESR_EPID);
	register_t vaddr = itlb ? regs->srr0 : mfspr(SPR_DEAR);

#ifdef CONFIG_TLB_CACHE
	if (guest || epid) {
		register_t mas1, mas2;
		int pid = mfspr(SPR_PID);
		int space = itlb ? (regs->srr1 & MSR_IS) >> 5 :
		                   (regs->srr1 & MSR_DS) >> 4;
		int ret = guest_tlb1_miss(vaddr, space, pid);

		if (likely(ret == TLB_MISS_HANDLED))
			return;

		if (ret == TLB_MISS_MCHECK && guest) {
			int store = mfspr(SPR_ESR) & ESR_ST;
			reflect_mcheck(regs, MCSR_MAV | MCSR_MEA |
			                     (itlb ? MCSR_IF : (store ? MCSR_ST : MCSR_LD)), vaddr);
			return;
		};

		if (guest) {
			assert(ret == TLB_MISS_REFLECT);

			inc_stat(stat_tlb_miss_reflect);

			mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | space);
			asm volatile("tlbsx 0, %0" : : "r" (vaddr));
		
			mas1 = mfspr(SPR_MAS1);
			assert(!(mas1 & MAS1_VALID));
			mtspr(SPR_MAS1, mas1 | MAS1_VALID);
		
			mas2 = mfspr(SPR_MAS2);
			mas2 &= MAS2_EPN;
			mas2 |= vaddr & MAS2_EPN;
			mtspr(SPR_MAS2, mas2);

			mtspr(SPR_GESR, mfspr(SPR_ESR)); 

			if (!itlb)
				mtspr(SPR_DEAR, vaddr); /* note: reflect_trap() moves this into GDEAR */
		}
	}
#else
	assert(!(regs->srr1 & MSR_GS));
#endif

	if (unlikely(!guest)) {
		if (epid) {
			abort_guest_access(regs, GUESTMEM_TLBMISS);
			return;
		}
		
		if (handle_hv_tlb_miss(regs, vaddr))
			return;
	}

	reflect_trap(regs);
}
