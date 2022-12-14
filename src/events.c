/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <libos/platform_error.h>

#include <events.h>
#include <vpic.h>
#include <doorbell.h>
#include <gdb-stub.h>
#include <percpu.h>
#include <thread.h>
#include <error_log.h>
#include <error_mgmt.h>

static eventfp_t event_table[] = {
	&dbell_to_gdbell_glue,           /* EV_ASSERT_VINT */
	&tlbivax_ipi,                    /* EV_TLBIVAX */
	&schedule,                       /* EV_RESCHED */
	&dbell_to_mcgdbell_glue,         /* EV_MCP */
	&dbell_to_cgdbell_glue,          /* EV_GUEST_CRIT_INT */
	&dump_hv_queue,                  /* EV_DUMP_HV_QUEUE */
	&deliver_mpic_errint,            /* EV_DELIVER_MPIC_ERR_INT */
#ifdef CONFIG_PM
	&sync_nap,                       /* EV_SYNC_NAP */
#else
	NULL,
#endif
};

/* Guest events are processed when returning to the guest, but
 * without regard for the MSR[EE/CE/ME] bits of the guest.
 */

int gev_stop;
int gev_start;
int gev_restart;
int gev_load;
int gev_start_load;
int gev_pause;
int gev_resume;
int gev_nmi;

/* Guest events are processed when returning to the guest, but
 * without regard for the MSR[EE/CE/ME] bits of the guest.
 */
#define MAX_GEVENTS 32
static eventfp_t gevent_table[MAX_GEVENTS];
static uint32_t gevent_lock;

int register_gevent(eventfp_t handler)
{
	register_t saved = spin_lock_intsave(&gevent_lock);
	int i;

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

	spin_unlock_intsave(&gevent_lock, saved);
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

	/* Make sure that we see a gcpu->napping that is at least as
	 * recent as when the napping core checked gevent_pending, if
	 * that check was too early to see the above store.
	 *
	 * Also, make sure ret_hook is not seen before gevent_pending.
	 */
	sync();

	gcpu->cpu->ret_hook = 1;

	if (gcpu->cpu != cpu || cpu->traplevel != TRAPLEVEL_NORMAL)
		send_doorbell(gcpu->cpu->coreid);

#ifdef CONFIG_PM
	if (gcpu->napping)
		setevent(cpu0.client.gcpu, EV_SYNC_NAP);
#endif
}

void return_hook(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	if (unlikely(!(regs->srr1 & MSR_GS)) &&
	    (!cur_thread()->can_take_gevent ||
	     regs->traplevel != TRAPLEVEL_THREAD))
		return;

	if (unlikely(cpu->traplevel != TRAPLEVEL_NORMAL))
		return;

	assert(cpu->ret_hook);
	assert(!(mfmsr() & MSR_EE));

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
		enable_int();
		gevent_table[bit](regs);
		disable_int();
	}
}

void crit_dbell_int(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	assert(!(mfmsr() & (MSR_CE | MSR_EE)));
	halt_core(regs);
}

void doorbell_int(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	assert(!(mfmsr() & MSR_EE));

	set_stat(bm_stat_dbell, regs);

	while (gcpu->dbell_pending) {
		/* get the next event */
		unsigned int bit = count_lsb_zeroes(gcpu->dbell_pending);
		assert(bit < sizeof(event_table) / sizeof(eventfp_t));

		/* clear the event */
		atomic_and(&gcpu->dbell_pending, ~(1 << bit));

		smp_lwsync();

		/* invoke the function */
		event_table[bit](regs);
	}
}

void dbell_to_mcgdbell_glue(trapframe_t *regs)
{
	send_local_mchk_guest_doorbell();
}

void dbell_to_cgdbell_glue(trapframe_t *regs)
{
	send_local_crit_guest_doorbell();
}

void halt_core(trapframe_t *regs)
{
	set_crashing(1);
	dump_regs(regs);
	set_crashing(0);

	asm volatile("1: wait;" "b 1b;");
}

void dump_hv_queue(trapframe_t *regs)
{
	hv_error_t err;

	spin_lock(&hv_queue_cons_lock);

	while (!error_get(&hv_global_event_queue, &err, NULL, 0, 0)) {
		if (!strcmp(err.domain, get_domain_str(error_mcheck))) {
			dump_domain_error_info(&err, error_mcheck);
		}

		if (!strcmp(err.domain, get_domain_str(error_pamu))) {
			dump_domain_error_info(&err, error_pamu);
		}

		if (!strcmp(err.domain, get_domain_str(error_ccf))) {
			dump_domain_error_info(&err, error_ccf);
		}

		if (!strcmp(err.domain, get_domain_str(error_cpc))) {
			dump_domain_error_info(&err, error_cpc);
		}

		if (!strcmp(err.domain, get_domain_str(error_misc))) {
			dump_domain_error_info(&err, error_misc);
		}

		if (!strcmp(err.domain, get_domain_str(error_ddr))) {
			dump_domain_error_info(&err, error_ddr);
		}
	}

	spin_unlock(&hv_queue_cons_lock);
}

void deliver_mpic_errint(trapframe_t *regs)
{
	vpic_interrupt_t *virq;

	while (queue_read(&mpic_errint_q, (uint8_t *) &virq,
	       sizeof(vpic_interrupt_t *), 0)) {

		error_sub_int_t *errint =
			to_container(virq->irq.parent, error_sub_int_t, dev_err_irq);

		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				"Error interrupt reflected, error-int=%d, guest=%s\n",
		                 errint->subintnum,
		                 virq->guest->name);

		vpic_assert_vint(virq);
	}
}

/* Initialize the global gevents
 */
void init_gevents(void)
{
	gev_stop = register_gevent(&stop_core);
	gev_start = register_gevent(&start_core);
	gev_restart = register_gevent(&restart_core);
	gev_load = register_gevent(&load_guest);
	gev_start_load = register_gevent(&start_load_guest);
	gev_pause = register_gevent(&pause_core);
	gev_resume = register_gevent(&resume_core);
	gev_nmi = register_gevent(&deliver_nmi);

	if (gev_stop < 0 || gev_start < 0 || gev_restart < 0 ||
	    gev_load < 0 || gev_start_load < 0 ||
	    gev_pause < 0 || gev_resume < 0 || gev_nmi < 0)
		BUG();
}
