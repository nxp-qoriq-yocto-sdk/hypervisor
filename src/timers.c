/*
 * Timer support
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

/* Eventually, this will do virtualized timers -- but for now,
 * just let the guest run things.
 */

#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <percpu.h>
#include <libos/console.h>
#include <libos/spr.h>
#include <doorbell.h>

/* gcpu->timer_flags */
#define TF_ENABLED        1
#define TF_ENABLED_SHIFT  0

void decrementer(trapframe_t *regs)
{
	if (__builtin_expect(!!(regs->srr1 & MSR_EE), 1)) {
		reflect_trap(regs);
		return;
	}

	// The guest has interrupts disabled, so defer it.
	get_gcpu()->pending |= GCPU_PEND_DECR;
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_DIE);

	send_local_guest_doorbell();
}

void run_deferred_decrementer(void)
{
	gcpu_t *gcpu = get_gcpu();
	gcpu->pending &= ~GCPU_PEND_DECR;

	if (gcpu->timer_flags & TF_ENABLED) {
		uint32_t val = mfspr(SPR_TCR) & ~TCR_DIE;
		val |= (gcpu->timer_flags << (TCR_DIE_SHIFT - TF_ENABLED_SHIFT)) &
	   	    TCR_DIE;

		mtspr(SPR_TCR, val);
	}
}

void guest_timer_init(gcpu_t *gcpu)
{
	gcpu->timer_flags |= TF_ENABLED;
}

void set_tcr(uint32_t val)
{
	gcpu_t *gcpu = get_gcpu();

	gcpu->timer_flags &= ~TF_ENABLED;
	gcpu->timer_flags |= (val >> (TCR_DIE_SHIFT - TF_ENABLED_SHIFT)) &
	                     TF_ENABLED;

	if (gcpu->pending & GCPU_PEND_DECR)
		val &= ~TCR_DIE;

	mtspr(SPR_TCR, val);
}

void set_tsr(uint32_t val)
{
	if (val & TCR_DIS)
		get_gcpu()->pending &= ~GCPU_PEND_DECR;

	mtspr(SPR_TSR, val);
}

uint32_t get_tcr(void)
{
	uint32_t val = mfspr(SPR_TCR);

	val &= ~TCR_DIE;
	val |= (get_gcpu()->timer_flags << (TCR_DIE_SHIFT - TF_ENABLED_SHIFT)) &
	       TCR_DIE;

	return val;
}

uint32_t get_tsr(void)
{
	return mfspr(SPR_TSR);
}
