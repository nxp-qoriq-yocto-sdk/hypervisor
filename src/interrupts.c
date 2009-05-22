
/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc.
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
#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/interrupts.h>
#include <libos/mpic.h>
#include <libos/percpu.h>

#include <errors.h>
#include <hv.h>

void critical_interrupt(trapframe_t *frameptr)
{
	do_mpic_critint();
}

void powerpc_mchk_interrupt(trapframe_t *frameptr)
{
	register_t mcsr;

	cpu->crashing++;

	printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,"powerpc_mchk_interrupt: machine check interrupt!\n");
	mcsr = mfspr(SPR_MCSR);
	printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,"Machine check syndrome register = %lx\n", mcsr);

	if (mcsr & MCSR_MAV) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,"Machine check %s address = %lx\n",
			(mcsr & MCSR_MEA)? "effective" : "real",
			 mfspr(SPR_MCAR));
	} else if (mcsr & MCSR_MCP){
		do_mpic_mcheck();
	}

	dump_regs(frameptr);

	cpu->crashing--;

	mtspr(SPR_MCSR, mfspr(SPR_MCSR));
}
