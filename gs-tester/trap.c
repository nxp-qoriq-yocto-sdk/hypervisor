
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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



#include <libos/libos.h>
#include <libos/trap_booke.h>
#include <libos/trapframe.h>
#include <libos/console.h>
#include <libos/spr.h>

struct powerpc_exception {
	int vector;
	char *name;
};

static const struct powerpc_exception powerpc_exceptions[] = {
	{ EXC_CRIT_INT, "critical input" },
	{ EXC_MCHECK, "machine check" },
	{ EXC_DSI, "data storage interrupt" },
	{ EXC_ISI, "instruction storage interrupt" },
	{ EXC_EXT_INT, "external interrupt" },
	{ EXC_ALIGN, "alignment" },
	{ EXC_PROGRAM, "program" },
	{ EXC_SYSCALL, "system call" },
	{ EXC_DECR, "decrementer" },
	{ EXC_FIT, "fixed-interval timer" },
	{ EXC_WDOG, "watchdog timer" },
	{ EXC_DTLB, "data tlb miss" },
	{ EXC_ITLB, "instruction tlb miss" },
	{ EXC_DEBUG, "debug" },
	{ EXC_PERFMON, "performance monitoring" },
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

extern volatile int exception_type;

void trap(trapframe_t *frameptr)
{
	int type;    

	type = frameptr->exc;
	exception_type = type;  /* for use by guest main */

	switch (type) {
	case EXC_CRIT_INT:
	case EXC_MCHECK:
	case EXC_DSI:
	case EXC_ISI:
	case EXC_EXT_INT:
	case EXC_ALIGN:
	case EXC_PROGRAM:
	case EXC_SYSCALL:
	case EXC_DECR:
	case EXC_FIT:
	case EXC_WDOG:
	case EXC_DTLB:
	case EXC_ITLB:
	case EXC_DEBUG:
	case EXC_PERFMON:
	case EXC_DOORBELL:
	case EXC_DOORBELLC:
	case EXC_GDOORBELL:
	case EXC_GDOORBELLC:
	case EXC_HCALL:
		printf("gs-tester trap() - unknown exception: %s\n", trapname(frameptr->exc));
		dump_regs(frameptr); 
		stopsim();
	case EXC_EHPRIV:
		frameptr->srr0 += 4;
		break;
	default:
		printf("gs-tester trap() - unknown exception: %s\n", trapname(frameptr->exc));
		dump_regs(frameptr); 
		stopsim();
	}

	
}
