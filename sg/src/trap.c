
#include "uv.h"
#include "trap_booke.h"
#include "frame.h"
#include "console.h"
#include "spr.h"

struct powerpc_exception {
	int vector;
	char *name;
};

void traceback(trapframe_t *regs)
{
        unsigned long *sp = ((unsigned long *)regs->gpregs[1]);
        printf("sp %p %lx %lx %lx %lx\n", sp, sp[0], sp[1], sp[2], sp[3]);
        sp = ((unsigned long **)regs->gpregs[1])[0];

        printf("Traceback: ");

        for (int i = 1; sp != NULL; i++, sp = (unsigned long *)sp[0]) {
                if ((i & 7) == 0)
                        printf("\n");

                printf("0x%08lx ", sp[1] - 4);
        }

        printf("\n");
}

void dump_regs(trapframe_t *regs)
{
        printf("NIP 0x%08x MSR 0x%08x LR 0x%08x ESR 0x%08x EXC %d\n"
               "CTR 0x%08x CR 0x%08x XER 0x%08x DEAR 0x%08x\n",
               regs->srr0, regs->srr1, regs->lr, mfspr(SPR_ESR), regs->exc,
               regs->ctr, regs->cr, regs->xer, mfspr(SPR_DEAR));

        for (int i = 0; i < 32; i++) {
                printf("r%02d 0x%08x  ", i, regs->gpregs[i]);

                if ((i & 3) == 3)
                        printf("\n");
        }

        if (!(regs->srr1 & MSR_GS))
                traceback(regs);
}


static const struct powerpc_exception powerpc_exceptions[] = {
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
	const struct powerpc_exception *pe;

	for (pe = powerpc_exceptions; pe->vector != EXC_LAST; pe++) {
		if (pe->vector == vector)
			return (pe->name);
	}

	return "unknown";
}

void trap(trapframe_t *regs)
{
	printf("simple guest : unknown exception: %s\n", trapname(regs->exc));
	dump_regs(regs); 
	
	stopsim();

}
