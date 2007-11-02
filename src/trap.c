
#include "uv.h"
#include "frame.h"
#include "trap_booke.h"
#include "console.h"

struct powerpc_exception {
	int vector;
	char *name;
};

void dump_regs(trapframe_t *regs)
{
	printf("NIP 0x%08x MSR 0x%08x LR 0x%08x\n"
	       "CTR 0x%08x CR 0x%08x XER 0x%08x\n",
	       regs->srr0, regs->srr1, regs->lr,
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

void unknown_exception(trapframe_t *frameptr)
{
	printf("unknown exception: %s\n", trapname(frameptr->exc));
	dump_regs(frameptr); 
	stopsim();
}
