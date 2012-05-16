/* @file
 * Trap handling
 */
/*
 * Copyright (C) 2007-2011 Freescale Semiconductor, Inc.
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
#include <libos/io.h>

#include <hv.h>
#include <percpu.h>
#include <timers.h>
#include <guestmemio.h>
#include <vpic.h>
#include <vmpic.h>
#include <greg.h>
#include <paging.h>
#include <doorbell.h>
#include <debug-stub.h>
#include <events.h>
#include <benchmark.h>

void program_trap(trapframe_t *regs)
{
	set_stat(bm_stat_program, regs);
#ifdef CONFIG_DEBUG_STUB_PROGRAM_INTERRUPT
	gcpu_t *gcpu = get_gcpu();
	if (mfspr(SPR_ESR) == ESR_PTR && (regs->srr1 & MSR_GS)
	    && gcpu->guest->stub_ops && gcpu->guest->stub_ops->debug_interrupt
	    && !gcpu->guest->stub_ops->debug_interrupt(regs))
		return;
#endif

	reflect_trap(regs);
}

void align_trap(trapframe_t *regs)
{
	set_stat(bm_stat_align, regs);
	reflect_trap(regs);
}

void fpunavail(trapframe_t *regs)
{
	set_stat(bm_stat_fpunavail, regs);
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
		set_crashing(1);
		printlog(LOGTYPE_IRQ, LOGLEVEL_ALWAYS,
		         "unexpected trap in hypervisor\n");
		dump_regs(regs);
		set_crashing(0);
		stopsim();
	}

	assert(!is_idle());
	assert(regs->exc < sizeof(gcpu->ivor) / sizeof(int));

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);
	mtspr(SPR_GESR, mfspr(SPR_ESR));
	mtspr(SPR_GDEAR, mfspr(SPR_DEAR));

	regs->srr0 = gcpu->ivpr | gcpu->ivor[regs->exc];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI;
#ifdef CONFIG_LIBOS_64BIT
	if (mfspr(SPR_EPCR) & EPCR_GICM)
		regs->srr1 |= MSR_CM;
#endif
}

void debug_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	set_stat(bm_stat_debug, regs);

	if (!gcpu->guest->guest_debug_mode) {
#ifdef CONFIG_DEBUG_STUB
		if (mfspr(SPR_DBSR) && (regs->srr1 & MSR_GS)
	 	    && gcpu->guest->stub_ops && gcpu->guest->stub_ops->debug_interrupt
	 	    && !gcpu->guest->stub_ops->debug_interrupt(regs))
			return;
#endif
		set_crashing(1);
		printlog(LOGTYPE_IRQ, LOGLEVEL_ALWAYS,
		         "unexpected debug exception\n");
		dump_regs(regs);
		set_crashing(0);
		stopsim();
	}
 
	gcpu->dsrr0 = regs->srr0;
	gcpu->dsrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DEBUG];
	regs->srr1 &= MSR_GS | MSR_UCLE | MSR_RI;
#ifdef CONFIG_LIBOS_64BIT
	if (mfspr(SPR_EPCR) & EPCR_GICM)
		regs->srr1 |= MSR_CM;
#endif
}

void guest_doorbell(trapframe_t *regs)
{
	unsigned long gsrr1 = mfspr(SPR_GSRR1);
	gcpu_t *gcpu = get_gcpu();
	interrupt_t *irq;

	assert(gsrr1 & MSR_GS);
	assert(gsrr1 & MSR_EE);

	set_stat(bm_stat_gdbell, regs);

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
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
		if (mfspr(SPR_EPCR) & EPCR_GICM)
			regs->srr1 |= MSR_CM;
#endif
		goto check_flags;
	}
no_virq:

	/* Then, check for a FIT. */
	if ((gcpu->gtcr & TCR_FIE) && (gcpu->gtsr & TSR_FIS)) {
		/* If FIS is set, then it means that the guest didn't get an
		 * interrupt when the FIT expired, or it didn't clear FIS in its
		 * interrupt handler. We need to reassert the FIT interrupt, but
		 * we can only do this is FIE is enabled.
		 */
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_FIT];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
		if (mfspr(SPR_EPCR) & EPCR_GICM)
			regs->srr1 |= MSR_CM;
#endif
		goto check_flags;
	}

	/* Then, check for a decrementer. */
	if ((gcpu->gtcr & TCR_DIE) && (gcpu->gtsr & TSR_DIS)) {
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DECR];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
		if (mfspr(SPR_EPCR) & EPCR_GICM)
			regs->srr1 |= MSR_CM;
#endif
		goto check_flags;
	}

	if (gcpu->gdbell_pending & GCPU_PEND_MSGSND) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_MSGSND);
		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DOORBELL];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
		if (mfspr(SPR_EPCR) & EPCR_GICM)
			regs->srr1 |= MSR_CM;
#endif
		goto check_flags;
	}

	/* Perfmon is lowest priority. */
	if (gcpu->gdbell_pending & GCPU_PEND_PERFMON) {
		/* Don't reflect an interrupt that has been deasserted
		 * by the hardware.
		 */
		if (check_perfmon(regs)) {
			atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_PERFMON);
			regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_PERFMON];
			regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE |
			                      MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
			if (mfspr(SPR_EPCR) & EPCR_GICM)
				regs->srr1 |= MSR_CM;
#endif
			goto check_flags;
		}
	}

	return;

check_flags:
	/* Are there still any pending doorbells?  If so, then we need to
	 * issue another guest doorbell so that we return to this function to
	 * process them.
	 *
	 * A FIT or decrementer interrupt is still considered pending
	 * even if we just reflected it, to mimic level-triggered
	 * hardware behavior.
	 */
	if (gcpu->gdbell_pending)
		send_local_guest_doorbell();
	if ((gcpu->gtcr & TCR_FIE) && (gcpu->gtsr & TSR_FIS))
		send_local_guest_doorbell();
	if ((gcpu->gtcr & TCR_DIE) && (gcpu->gtsr & TSR_DIS))
		send_local_guest_doorbell();
}

static int check_wdog_crit_event(gcpu_t *gcpu, trapframe_t *regs)
{
	if ((gcpu->gtcr & TCR_WIE) && (gcpu->gtsr & TSR_WIS)) {
		reflect_watchdog(gcpu, regs);

		return 1;
	}

	return 0;
}

static int check_msgsnd_crit_event(gcpu_t *gcpu, trapframe_t *regs)
{
	if (gcpu->crit_gdbell_pending & GCPU_PEND_MSGSNDC) {
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_MSGSNDC);
		reflect_crit_int(regs, EXC_DOORBELLC);

		return 1;
	}

	return 0;
}

void reflect_crit_int(trapframe_t *regs, int trap_type)
{
	gcpu_t *gcpu = get_gcpu();

	assert(!is_idle());

	/*
	 * We save the values of CSSR0 and CSSR1 into gcpu for emu_mfspr() and
	 * emu_mtspr().  Since there are no GCSRR0 and GCSRR1 registers, the
	 * guest can't access CSRR0 or CSRR1 directly when the hypervisor is
	 * running -- they need to be emulated.
	 */
	gcpu->csrr0 = regs->srr0;
	gcpu->csrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[trap_type];
	regs->srr1 &= (MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI);
#ifdef CONFIG_LIBOS_64BIT
	if (mfspr(SPR_EPCR) & EPCR_GICM)
		regs->srr1 |= MSR_CM;
#endif
}

static int check_crit_int_event(gcpu_t *gcpu, trapframe_t *regs)
{
	if (gcpu->crit_gdbell_pending & GCPU_PEND_CRIT_INT) {
		reflect_crit_int(regs, EXC_CRIT_INT);
		return 1;
	}

	return 0;
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
	int ret;

	assert(!is_idle());

	set_stat(bm_stat_gdbell_crit, regs);

	if ((gcpu->mcsr & MCSR_MCP) && (regs->srr1 & MSR_ME)) {
		reflect_mcheck(regs, MCSR_MCP, 0);
		goto check_flags;
	}

	if (regs->srr1 & MSR_CE) {
		ret = check_wdog_crit_event(gcpu, regs);
		if (ret)
			goto check_flags;

		ret = check_crit_int_event(gcpu, regs);
		if (ret)
			goto check_flags;

		ret = check_msgsnd_crit_event(gcpu, regs);
		if (ret)
			goto check_flags;
	}

	return;

check_flags:
	/* Are there still any pending doorbells?  If so, then we need to
	 * issue another guest doorbell so that we return to this function to
	 * process them.
	 */
	if (gcpu->mcsr & MCSR_MCP)
		send_local_mchk_guest_doorbell();

	if (gcpu->crit_gdbell_pending)
		send_local_crit_guest_doorbell();
}

void reflect_mcheck(trapframe_t *regs, register_t mcsr, uint64_t mcar)
{
	gcpu_t *gcpu = get_gcpu();

	assert(!is_idle());

	gcpu->mcsr = mcsr;
	if (!queue_empty(&gcpu->guest->error_event_queue))
		atomic_or(&gcpu->mcsr, MCSR_MCP);

	gcpu->mcar = mcar;

	gcpu->mcsrr0 = regs->srr0;
	gcpu->mcsrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_MCHECK];

	if (gcpu->guest->guest_debug_mode)
		regs->srr1 &= MSR_GS | MSR_UCLE;
	else
		regs->srr1 &= MSR_GS | MSR_UCLE | MSR_DE;
#ifdef CONFIG_LIBOS_64BIT
	if (mfspr(SPR_EPCR) & EPCR_GICM)
		regs->srr1 |= MSR_CM;
#endif
}

void deliver_nmi(trapframe_t *regs)
{
	reflect_mcheck(regs, MCSR_NMI, 0);
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

	set_stat(bm_stat_dsi, regs);

	/* If it's from the guest, then it was a virtualization
	 * fault.  Currently, we only use that for bad mappings.
	 * This includes emulated devices that do not have page table entries.
	 */

	if (regs->srr1 & MSR_GS) {
#ifdef CONFIG_DEVICE_VIRT
		guest_t *guest = get_gcpu()->guest;
		unsigned long vaddr = regs->dear;
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
	set_stat(bm_stat_isi, regs);

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

	if (cpu->client.tlb1_virt) {
		if (guest || epid) {
			register_t mas1, mas2;
			int store = mfspr(SPR_ESR) & ESR_ST;
			int pid, space, ret;
			uint32_t epc = 0;

			if (epid) {
				if (store)
					epc = guest ? regs->epsc : mfspr(SPR_EPSC);
				else
					epc = guest ? regs->eplc : mfspr(SPR_EPLC);

				pid = epc & EPC_EPID;
				space = !!(epc & EPC_EAS);
			} else {
				pid = mfspr(SPR_PID);
				space = itlb ? (regs->srr1 & MSR_IS) >> 5 :
							   (regs->srr1 & MSR_DS) >> 4;
			}

			if (guest || (epc & EPC_EGS)) {
				ret = guest_tlb1_miss(vaddr, space, pid);
				if (likely(ret == TLB_MISS_HANDLED))
					return;
			}

			if (guest) {
				if (ret == TLB_MISS_MCHECK) {
					uint32_t mcsr = MCSR_MAV | MCSR_MEA;

					if (itlb)
						mcsr |= MCSR_IF;
					else if (store)
						mcsr |= MCSR_ST;
					else
						mcsr |= MCSR_LD;

					reflect_mcheck(regs, mcsr, vaddr);
					return;
				};

				assert(ret == TLB_MISS_REFLECT);

				set_stat(bm_stat_tlb_miss_reflect, regs);

				mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | space);
				asm volatile("isync; tlbsx 0, %0" : : "r" (vaddr));

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
	} else {
		set_stat(bm_stat_tlb_miss, regs);
		assert(!(regs->srr1 & MSR_GS));
	}

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

void perfmon_int(trapframe_t *regs)
{
	/* The perfmon interrupt cannot be masked by EE in guest state.
	 * When an interrupt occurs, we mask it with PMGC0 and disable
	 * direct guest access to the PMRs.  Access is restored when
	 * the guest either masks or clears the interrupt.
	 *
	 * While PMRs are trapping to the hypervisor, the guest must not
	 * set MSR[PMM] (it will be ignored) or read user PMRs from
	 * userspace (will return zero).  This is a hardware limitation.
	 */

	uint32_t reg = mfpmr(PMR_PMGC0);
	assert(reg & PMGC0_PMIE);

	mtpmr(PMR_PMGC0, reg & ~PMGC0_PMIE);

	reg = mfspr(SPR_MSRP);
	assert(!(reg & MSRP_PMMP));
	mtspr(SPR_MSRP, reg | MSRP_PMMP);

	if (likely((regs->srr1 & (MSR_GS | MSR_EE)) == (MSR_GS | MSR_EE))) {
		reflect_trap(regs);
		return;
	}

	atomic_or(&get_gcpu()->gdbell_pending, GCPU_PEND_PERFMON);
	send_local_guest_doorbell();
}

/* If a partition had an interrupt pending with coreint, it will remain
 * pending on that core even after masking.  During partition shutdown,
 * we temporarily redirect interrupts to the hypervisor to drain these
 * interrupts.
 *
 * There should be only one per core.  We may want to record what it is, and
 * if edge-triggered we reissue it if the interrupt is enabled again (e.g. 
 * in a new claimable owner) -- but consider how to deal with the EOI from
 * that replayed interrupt, especially if direct EOI is enabled.
 */
void external_int(trapframe_t *regs)
{
	if (!mpic_coreint)
		in32(((uint32_t *)(CCSRBAR_VA + MPIC + MPIC_IACK)));

	out32((uint32_t *)(CCSRBAR_VA + MPIC + MPIC_EOI), 0);

	/* Flush the write, if there's another interrupt we
	 * want to take it immediately before EXTGS is set
	 * again.
	 */
	in32((uint32_t *)(CCSRBAR_VA + MPIC + MPIC_EOI));
}
