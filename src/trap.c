#include <uv.h>
#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <libos/console.h>
#include <percpu.h>
#include <libos/spr.h>
#include <timers.h>
#include <guestmemio.h>

// Do not use this when entering via guest doorbell, since that saves
// state in gsrr rather than srr, despite being directed to the
// hypervisor.

void reflect_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (__builtin_expect(!(regs->srr1 & MSR_GS), 0)) {
		printf("unexpected trap in hypervisor\n");
		dump_regs(regs);
		stopsim();
	}

	assert(regs->exc >= 0 && regs->exc < sizeof(gcpu->ivor) / sizeof(int));

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);

	regs->srr0 = gcpu->ivpr | gcpu->ivor[regs->exc];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE;
}

void guest_doorbell(trapframe_t *regs)
{
	unsigned long gsrr1 = mfspr(SPR_GSRR1);
	gcpu_t *gcpu = get_gcpu();

	assert(gsrr1 & MSR_GS);
	assert(gsrr1 & MSR_EE);

	// First, check external interrupts. (TODO).
	if (0) {
	}

	// Then, check for a decrementer.
	if (gcpu->pending & GCPU_PEND_DECR) {
		run_deferred_decrementer();

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DECR];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS | MSR_UCLE);
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
	// If it's from the guest, then it was a virtualization
	// fault.  Currently, we only use that for bad mappings.
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
