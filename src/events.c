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
#include <libos/trapframe.h>
#include <libos/libos.h>
#include <libos/bitops.h>
#include <libos/interrupts.h>
#include <libos/libos.h>
#include <libos/console.h>
#include <libos/printlog.h>
#include <libos/errors.h>

#include <events.h>
#include <vpic.h>
#include <doorbell.h>
#include <gdb-stub.h>
#include <percpu.h>
#include <thread.h>

static eventfp_t event_table[] = {
	&dbell_to_gdbell_glue,  /* EV_ASSERT_VINT */
	&tlbivax_ipi,           /* EV_TLBIVAX */
	&schedule,              /* EV_RESCHED */
};

/* Guest events are processed when returning to the guest, but
 * without regard for the MSR[EE/CE/ME] bits of the guest.
 */

int gev_stop;
int gev_start;
int gev_restart;
int gev_start_wait;
int gev_pause;
int gev_resume;

/* Guest events are processed when returning to the guest, but
 * without regard for the MSR[EE/CE/ME] bits of the guest.
 */
#define MAX_GEVENTS 32
static eventfp_t gevent_table[MAX_GEVENTS];
static uint32_t gevent_lock;

int register_gevent(eventfp_t handler)
{
	int i;
	spin_lock(&gevent_lock);

	/* find an open event slot */
	for (i = 0; i < MAX_GEVENTS; i++) {
		if (!gevent_table[i])
			break;
	}

	if (i == MAX_GEVENTS) {
		spin_unlock(&gevent_lock);
		return ERR_BUSY;  /* no events available */
	}

	gevent_table[i] = handler;

	spin_unlock(&gevent_lock);

	return i;
}

void setevent(gcpu_t *gcpu, int event)
{
	/* set the event bit */
	smp_mbar();
	atomic_or(&gcpu->dbell_pending, (1 << event));
	send_doorbell(gcpu->cpu->coreid);

	// TODO optimization -- could check if it's on
	// the same cpu and just invoke
	// the event function
}

void setgevent(gcpu_t *gcpu, int event)
{
	/* set the event bit */
	smp_mbar();
	atomic_or(&gcpu->gevent_pending, (1 << event));
	smp_mbar();
	gcpu->cpu->ret_hook = 1;

	if (gcpu->cpu != cpu || cpu->traplevel != 0)
		send_doorbell(gcpu->cpu->coreid);
}

void return_hook(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	int waiting = gcpu->waiting_for_gevent;

	if (unlikely(!(regs->srr1 & MSR_GS)) && !waiting)
		return;

	if (unlikely(cpu->traplevel != 0))
		return;

	assert(cpu->ret_hook);

	gcpu->waiting_for_gevent = 0;
	enable_int();
	
	while (gcpu->gevent_pending) {
		cpu->ret_hook = 0;
		smp_sync();

		if (!gcpu->gevent_pending)
			break;

		/* get the next event */
		unsigned int bit = count_lsb_zeroes(gcpu->gevent_pending);
		assert(bit < sizeof(gevent_table) / sizeof(eventfp_t));

		/* clear the event */
		atomic_and(&gcpu->gevent_pending, ~(1 << bit));

		smp_lwsync();

		/* invoke the function */
		gevent_table[bit](regs);
	}

	disable_int();
	gcpu->waiting_for_gevent = waiting;
}

void doorbell_int(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	assert(!(mfmsr() & MSR_EE));

	while (gcpu->dbell_pending) {
		/* get the next event */
		int bit = count_lsb_zeroes(gcpu->dbell_pending);
		assert(bit < sizeof(event_table) / sizeof(eventfp_t));

		/* clear the event */
		atomic_and(&gcpu->dbell_pending, ~(1 << bit));

		smp_lwsync();

		/* invoke the function */
		event_table[bit](regs);
	}
}

/* Initialize the global gevents
 */
void init_gevents(void)
{
	gev_stop = register_gevent(&stop_core);
	gev_start = register_gevent(&start_core);
	gev_restart = register_gevent(&restart_core);
	gev_start_wait = register_gevent(&start_wait_core);
	gev_pause = register_gevent(&pause_core);
	gev_resume = register_gevent(&resume_core);

	if (gev_stop < 0 || gev_start < 0 ||
	    gev_restart < 0 || gev_start_wait < 0 ||
	    gev_pause < 0 || gev_resume < 0)
		BUG();
}
