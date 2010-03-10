
/*
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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
#include <libos/platform_error.h>

#include <errors.h>
#include <events.h>
#include <doorbell.h>
#include <percpu.h>
#include <error_log.h>
#include <hv.h>
#include <vpic.h>

/* queue for pending reflected (virtual) interrupts */

#define MAX_PEND_VIRQ 64
DECLARE_QUEUE(pend_virq, MAX_PEND_VIRQ * sizeof(vpic_interrupt_t *));

static void dump_and_halt(register_t mcsr, trapframe_t *regs)
{
	set_crashing(1);

	printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,"powerpc_mchk_interrupt: machine check interrupt!\n");
	printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			"mcheck registers: mcsr = %lx, mcssr0 = %lx, mcsrr1 = %lx\n",
			mcsr, mfspr(SPR_MCSRR0), mfspr(SPR_MCSRR1));
	if (mcsr & MCSR_MAV) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,"Machine check %s address = %lx\n",
			(mcsr & MCSR_MEA)? "effective" : "real",
			mfspr(SPR_MCAR));
	}

	smp_mbar();
	send_crit_doorbell_brdcast();
	dump_regs(regs);

	set_crashing(0);

	asm volatile("1: wait;" "b 1b;");
}

void critical_interrupt(trapframe_t *frameptr)
{
	set_stat(bm_stat_critint, frameptr);

	do_mpic_critint();
}

void powerpc_mchk_interrupt(trapframe_t *frameptr)
{
	register_t mcsr, reason;
	int guest_state = 0;
	error_info_t err = { };
	mcheck_error_t *mc;
	int recoverable = 1;

	set_stat(bm_stat_mcheck, frameptr);

	reason = mcsr = mfspr(SPR_MCSR);

	if (frameptr->srr1 & MSR_GS)
		guest_state = 1;

	if (reason & MCSR_MCP) {
		do_mpic_mcheck();
	}

	/* In case of I-cache errors (data or tag array) the cache
	 * is automatically invalidated by the hardware.
	 */
	if (reason & MCSR_ICPERR) {
		/*
		 * This will generally be accompanied by an instruction
		 * fetch error report -- only treat MCSR_IF as fatal
		 * if it wasn't due to an L1 parity error.
		 */
		reason &= ~MCSR_IF;
	}

	/* In case of D-cache errors, the cache is only invalidated
	 * if the write shadow mode is enabled.
	 */
	if ((reason & MCSR_DCPERR) && !guest_state &&
		!(mfspr(SPR_L1CSR2) & L1CSR2_DCWS)) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Data cache parity error\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_L2MMU_MHIT) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Hit on multiple tlb entries\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_IF) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Instruction fetch error report\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_LD) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Load error report\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_ST) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Store error report\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_LDG) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Guarded load error report\n");
		set_crashing(0);
		recoverable = 0;
	}

	if ((reason & MCSR_BSL2_ERR) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Level 2 cache error\n");
		set_crashing(0);
		recoverable = 0;
	}

	if (!(frameptr->srr1 & MSR_RI) && !guest_state) {
		set_crashing(1);
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Recursive machine check\n");
		set_crashing(0);
		recoverable = 0;
	}

	if (!recoverable)
		dump_and_halt(mcsr, frameptr);

	err.error_code = ERROR_MACHINE_CHECK;
	mc = &err.regs.mc_err;
	mc->mcsr = mcsr;
	mc->mcar = mcsr & MCSR_MAV ? mfspr(SPR_MCAR) : 0;
	mc->mcsrr0 = mfspr(SPR_MCSRR0);
	mc->mcsrr1 = mfspr(SPR_MCSRR1);
	error_log(&hv_global_event_queue, &err, &hv_queue_prod_lock);
	/* MCP machine checks would have been already reflected */
	if (guest_state && (mcsr & ~MCSR_MCP))
		reflect_mcheck(frameptr, (mc->mcsr & ~MCSR_MCP), mc->mcar);

	mtspr(SPR_MCSR, mcsr);

	return;
}

int reflect_errint(void *arg)
{
	interrupt_t *irq = arg;
	vpic_interrupt_t *virq = irq->priv;
	int ret;

	interrupt_mask(irq);

	ret = queue_write(&pend_virq, (const uint8_t *) &virq,
			sizeof(vpic_interrupt_t *));
	if (ret)
		setevent(cpu->client.gcpu, EV_DELIVER_PEND_VINT);

	return 0;
}
