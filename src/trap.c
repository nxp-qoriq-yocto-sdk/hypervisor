
#include "uv.h"
#include "frame.h"
#include "trap_booke.h"
#include "console.h"
#include <percpu.h>
#include <spr.h>

struct powerpc_exception {
	int vector;
	char *name;
};

void dump_regs(trapframe_t *regs)
{
	printf("NIP 0x%08x MSR 0x%08x LR 0x%08x EXC %d\n"
	       "CTR 0x%08x CR 0x%08x XER 0x%08x\n",
	       regs->srr0, regs->srr1, regs->lr, regs->exc,
	       regs->ctr, regs->cr, regs->xer);

	for (int i = 0; i < 32; i++) {
		printf("r%02d 0x%08x  ", i, regs->gpregs[i]);
		
		if ((i & 3) == 3)
			printf("\n");
	}
}

static struct powerpc_exception powerpc_exceptions[] = {
	{ EXC_CRIT, "critical input" },
	{ EXC_MCHK, "machine check" },
	{ EXC_DSI, "data storage interrupt" },
	{ EXC_ISI, "instruction storage interrupt" },
	{ EXC_EXI, "external interrupt" },
	{ EXC_ALI, "alignment" },
	{ EXC_PGM, "program" },
	{ EXC_SC, "system call" },
	{ EXC_DECR, "decrementer" },
	{ EXC_FIT, "fixed-interval timer" },
	{ EXC_WDOG, "watchdog timer" },
	{ EXC_DTLB, "data tlb miss" },
	{ EXC_ITLB, "instruction tlb miss" },
	{ EXC_DEBUG, "debug" },
	{ EXC_PERF, "performance monitoring" },
	{ EXC_DOORBELL, "doorbell"},
	{ EXC_DOORBELLC, "doorbell critical"},
	{ EXC_GDOORBELL, "guest doorbell"},
	{ EXC_GDOORBELLC, "guest doorbell critical"},
	{ EXC_HCALL, "hcall"},
	{ EXC_EHPRIV, "ehpriv"},
	{ EXC_LAST, NULL }
};

static const char *trapname(int vector)
{
	struct  powerpc_exception *pe;

	for (pe = powerpc_exceptions; pe->vector != EXC_LAST; pe++) {
		if (pe->vector == vector)
			return (pe->name);
	}

	return "unknown";
}

void unknown_exception(trapframe_t *regs)
{
	printf("unknown exception: %s\n", trapname(regs->exc));
	dump_regs(regs); 
	
	if (regs->srr0 & MSR_GS)
		reflect_trap(regs);
	else
		stopsim();
}

// Do not use this when entering via guest doorbell, since that saves
// state in gsrr rather than srr, despite being directed to the
// hypervisor.

void reflect_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = hcpu->gcpu;

	if (__builtin_expect(!(regs->srr1 & MSR_GS), 0)) {
		printf("unexpected trap in hypervisor\n");
		dump_regs(regs);
		BUG();
	}

	assert(regs->exc >= 0 && regs->exc < sizeof(gcpu->ivor) / sizeof(int));

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);

	regs->srr0 = gcpu->ivpr | gcpu->ivor[regs->exc];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS;
}

void guest_doorbell(trapframe_t *regs)
{
	gcpu_t *gcpu = hcpu->gcpu;
	unsigned long gsrr1 = mfspr(SPR_GSRR1);

	assert(gsrr1 & MSR_GS);
	assert(gsrr1 & MSR_EE);

	// First, check external interrupts. (TODO).
	if (0) {
	}

	// Then, check for a decrementer.
	if (gcpu->pending & GCPU_PEND_DECR) {
		printf("Running deferred guest decrementer...\n");
		gcpu->pending &= ~GCPU_PEND_DECR;

		regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_DECR];
		regs->srr1 = gsrr1 & (MSR_CE | MSR_ME | MSR_DE | MSR_GS);
	}
}
