
#include "uv.h"
#include "frame.h"
#include "trap_booke.h"
#include "console.h"

struct powerpc_exception {
	int vector;
	char *name;
};

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
	stopsim();
}
