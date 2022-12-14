/** @file
 *
 * p4080-style power management
 */

/*
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc.
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

#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/trapframe.h>
#include <libos/printlog.h>
#include <libos/io.h>
#include <libos/core-regs.h>

#include <hv.h>
#include <percpu.h>
#include <errors.h>
#include <devtree.h>
#include <events.h>
#include <doorbell.h>

#define RCPM_REV1	1
#define RCPM_REV2	2

#define RCPM1_CNAPSRL    (0x014 / 4)	/* Core Nap Status */
#define RCPM1_CNAPCRL    (0x01c / 4)	/* Core Nap Control */
#define RCPM1_CWAITSRL   (0x034 / 4)	/* Core Wait Status */
#define RCPM1_CTBHLTCRL  (0x094 / 4)	/* Core Timebase Halt Control */

#define RCPM2_TPH10SRL   (0x00c / 4)	/* Thread PH10 Status Register */
#define RCPM2_TPH10SETRL (0x01c / 4)	/* Thread PH10 Set Control Register */
#define RCPM2_TPH10CLRRL (0x02c / 4)	/* hread PH10 Clear Control Register */
#define RCPM2_TPH15SRL   (0x0b0 / 4)	/* Thread PH15 Status Register */
#define RCPM2_TPH15SETRL (0x0b4 / 4)	/* Thread PH15 Set Control Register */
#define RCPM2_TPH15CLRRL (0x0b8 / 4)	/* hread PH15 Clear Control Register */
#define RCPM2_TWAITSRL   (0x04c / 4)	/* Thread Wait Status Register */
#define RCPM2_TTBHLTCRL  (0x1bc / 4)	/* Thread Time Base Halt Control */

typedef struct {
	int reg, state;
} sleep_status_t;

sleep_status_t sleep_regs_rev1[] = {
	{ RCPM1_CNAPSRL, FH_VCPU_NAP },
	{ RCPM1_CWAITSRL, FH_VCPU_IDLE },
	{ -1, -1 }
};

sleep_status_t sleep_regs_rev2[] = {
	{ RCPM2_TPH15SRL, FH_VCPU_NAP },
	{ RCPM2_TWAITSRL, FH_VCPU_IDLE },
	{ -1, -1 }
};

static uint32_t *rcpm; /* run control and power management */
int rcpm_rev;

static int rcpm_probe(device_t *dev, const dev_compat_t *compat_id);

static const dev_compat_t rcpm_compats[] = {
	{
		.compatible	= "fsl,qoriq-rcpm-1.0",
		.data		= (void *)RCPM_REV1
	},
	{
		.compatible	= "fsl,qoriq-rcpm-2.0",
		.data		= (void *)RCPM_REV2
	},
	{}
};

static driver_t __driver rcpm_driver = {
	.compatibles = rcpm_compats,
	.probe = rcpm_probe,
};

static int rcpm_probe(device_t *dev, const dev_compat_t *compat_id)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_PM, LOGLEVEL_ERROR, "rcpm: bad regs\n");
		return ERR_INVALID;
	}

	rcpm = (uint32_t *)dev->regs[0].virt;
	rcpm_rev = (int)(uintptr_t)compat_id->data;

	/* Make sure timebase runs while napping, to preserve sync */
	if (rcpm_rev == RCPM_REV2)
		out32(&rcpm[RCPM2_TTBHLTCRL], 0xffffffff);
	else
		out32(&rcpm[RCPM1_CTBHLTCRL], 0xffffffff);

	return 0;
}

int get_vcpu_state(guest_t *guest, unsigned int vcpu)
{
	gcpu_t *gcpu;
	int core, i;
	sleep_status_t *p;

	if (!rcpm || !guest || vcpu >= guest->cpucnt)
		return -EV_EINVAL;

	gcpu = guest->gcpus[vcpu];
	core = gcpu->cpu->coreid;

	assert(core < 32);

	p = rcpm_rev == RCPM_REV2 ? sleep_regs_rev2 : sleep_regs_rev1;

	for (; p->state != -1; p++)
		if (in32(&rcpm[p->reg]) & (1 << core / cpu_caps.threads_per_core))
			return p->state;

	return FH_VCPU_RUN;
}

void hcall_get_core_state(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	unsigned int vcpu = regs->gpregs[4];
	int state;

	state = get_vcpu_state(guest, vcpu);
	if (state < 0) {
		regs->gpregs[3] = -state;
		return;
	}

	regs->gpregs[3] = 0;
	regs->gpregs[4] = state;
}

typedef struct nap_state {
	uint32_t tcr;
	core_cache_state_t state;
} nap_state_t;

/* Runs only on the boot core (which cannot nap).  Checks every
 * gcpu, and puts to sleep any that need it.  Allowing cores to
 * touch CNAPCRL directly could race if multiple cores try to
 * sleep at the same time.
 */
void sync_nap(trapframe_t *regs)
{
	int i, j;

	if (!rcpm)
		return;

	if (rcpm_rev == RCPM_REV2) {
		uint32_t sleep_mask = 0, nap_mask = 0, mask;

		/* Used the following algorithm to put multiple cores into nap:
		 * 1. build a mask with all the cpus (threads) requested to sleep
		 * 2. build a mask with cores that have all threads requested
		 *    to sleep
		 * 3. using the mask built at #2, nap the cores
		 * 4. additionally, doze the remaining scattered threads
		 * 5. to ensure cores / threads not having a pending nap
		 *    request are awake, clear their nap / doze state
		 */
		for (i = 0; i < CONFIG_LIBOS_MAX_CPUS - 1; i++) {
			cpu_t *c = &secondary_cpus[i];

			if (c->client.nap_request &&
			    c->client.gcpu->napping &&
			    !c->client.gcpu->gevent_pending)
				sleep_mask |= 1 << c->coreid;
		}

		/* boot core should never have a nap request */
		assert(!(sleep_mask & 1));

		for (i = 0;
		     i < CONFIG_LIBOS_MAX_CPUS / cpu_caps.threads_per_core;
		     i++) {
			/* this just builds a mask with all the threads in
			 * core 'i' so, for dual threaded cores core0 will
			 * have 11b, core1 1100b core1 110000b a.s.o.
			 */
			mask = (1 << cpu_caps.threads_per_core) - 1;
			mask <<= i * cpu_caps.threads_per_core;

			/* Are all threads in this core requested to sleep?
			 * If so mark it down to put the whole core in nap.
			 */
			if ((sleep_mask & mask) == mask) {
				sleep_mask &= ~mask;
				nap_mask |= 1 << i;
			}
		}

		out32(&rcpm[RCPM2_TPH10SETRL], sleep_mask);  /* doze threads */
		out32(&rcpm[RCPM2_TPH15SETRL], nap_mask);    /* nap cores */
		out32(&rcpm[RCPM2_TPH15CLRRL], ~nap_mask);   /* wake cores */
		out32(&rcpm[RCPM2_TPH10CLRRL], ~sleep_mask); /* wake threads */
	} else {
		uint32_t cnapcrl = 0, prev_cnapcrl;

		/* Used the algorithm from ref man to put multiple cores into nap
		 * mode:
		 * 1. Write 1 to the bit corresponding to the first core to be
		 *    put in nap
		 * 2. Read CNAPCR to push the previous write
		 * 3. Repeat steps 1 and 2 for all desired cores.
		 * The same algorithm applies when waking up a core from nap.
		 */
		prev_cnapcrl = in32(&rcpm[RCPM1_CNAPCRL]);

		for (i = 0; i < CONFIG_LIBOS_MAX_CPUS - 1; i++) {
			cpu_t *c = &secondary_cpus[i];

			if (c->client.nap_request &&
			    c->client.gcpu->napping &&
			    !c->client.gcpu->gevent_pending)
				cnapcrl = prev_cnapcrl | 1 << c->coreid;
			else
				cnapcrl = prev_cnapcrl & ~(1 << c->coreid);

			if (cnapcrl != prev_cnapcrl) {
				out32(&rcpm[RCPM1_CNAPCRL], cnapcrl);
				prev_cnapcrl = in32(&rcpm[RCPM1_CNAPCRL]);
			}
		}
	}
}

void hcall_enter_nap(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	int vcpu = regs->gpregs[4];
	gcpu_t *gcpu = get_gcpu();
	int core;
	int ret = EV_EINVAL;

	if (!rcpm || !displacement_flush_area[cpu->coreid]) {
		ret = EV_ENODEV;
		goto out;
	}

	/* Must be calling vcpu only */
	if (guest != gcpu->guest || vcpu != gcpu->gcpu_num)
		goto out;

	/* Can't nap the boot core.  Besides handling sync_nap
	 * requests, it is where hv-owned interrupts go.
	 */
	core = gcpu->cpu->coreid;
	if (core == 0)
		goto out;

	/* sync_ipi_lock is used for senders of events that want to wait
	 * for a response from the target and skip napping cores (currently
	 * just tlbivax).  See emu_tlbivax for why we use raw_spin_lock.
	 */
	raw_spin_lock(&guest->sync_ipi_lock);
	atomic_or(&gcpu->napping, GCPU_NAPPING_HCALL);
	spin_unlock(&guest->sync_ipi_lock);

	/* Check for races with events that are guest-visible wakeup
	 * sources. 
	 */
	sync();
	if ((gcpu->gdbell_pending & (1 << gev_nmi)) ||
	    (gcpu->mcsr & MCSR_MCP)) {
	    	atomic_and(&gcpu->napping, GCPU_NAPPING_HCALL);
	    	ret = 0;
	    	goto out;
	}
		

	while (1) {
		prepare_to_block();

		if (!gcpu->napping)
			break;

		assert(!cpu->client.nap_request);
		block();
		assert(!cpu->client.nap_request);
	}

	ret = 0;
out:
	regs->gpregs[3] = ret;
}

void idle_loop(void)
{
	nap_state_t ns;
	gcpu_t *gcpu = get_gcpu();
	uint64_t tb_freq = dt_get_timebase_freq();

	if (cpu->coreid == 0 || !rcpm || !displacement_flush_area[cpu->coreid])
		goto wait;

	/* Currently, other than the boot core we should never become
	 * idle without intending to nap.
	 */
	while (1) {
		disable_int();

		/* Disable decrementer, FIT, and watchdog interrupts.  Reset
		 * the watchdog timer to avoid a timeout.  This should prevent
		 * any of the watchdog TSR bits from changing while we're
		 * napping.
		 */
		ns.tcr = mfspr(SPR_TCR);
		mtspr(SPR_TCR, ns.tcr & ~(TCR_DIE | TCR_FIE | TCR_WIE | TCR_WP_MASK));

		/* There is a small possibility that the that the watchdog
		 * expired after we disabled (critical) interrupts, but before
		 * we changed the period to 0.  To handle that situation, ping
		 * the watchdog.
		 */
		mtspr(SPR_TSR, TSR_WIS);

		if (flush_disable_core_caches(&ns.state))
			goto out_timers;

		/* To avoid races with multiple nap transitions, we can't let
		 * cores set their own nap bit.  Instead, we ask the boot core
		 * to do it for us.  The napping flag also protects against
		 * spurious wakeups.
		 */

		cpu->client.nap_request = 1;

		while (gcpu->napping && !gcpu->gevent_pending) {
			uint64_t tb;
			setevent(cpu0.client.gcpu, EV_SYNC_NAP);

			/* We can't use "wait", at least with doorbells
			 * as a wakeup source, because the doorbell can
			 * get lost if we're still napping when it's
			 * sent.  Instead, spin in a minimally-intrusive
			 * manner, checking for events every 100 us, and
			 * calling sync_nap() every 1 second.
			 */

			tb = get_tb();
			while (get_tb() - tb < tb_freq) {
				uint64_t tb2 = get_tb();
				while (get_tb() - tb2 < tb_freq / 10000)
					;

				if (!gcpu->napping || gcpu->gevent_pending)
					break;
			}
		}

		cpu->client.nap_request = 0;

		restore_core_caches(&ns.state);

		/* Restore decrementer, FIT, and watchdog. */
		mtspr(SPR_TCR, ns.tcr);

		enable_int();

		/* The core doesn't listen to doorbells while napping. */
		if (gcpu->gevent_pending || gcpu->dbell_pending ||
		    gcpu->gdbell_pending) {
			send_doorbell(cpu->coreid);

			/* What is required to ensure that a doorbell
			 * sent to self has been delivered before we
			 * disable interrupts?  This may be overkill,
			 * but if all we have to go on is that it's
			 * ordered as a store, this should do it.
			 */
			register_t tmp;
			asm volatile("msync;"
			             "lwz %0, 0(1);"
			             "twi 0, %0, 0;"
			             "isync" : "=r" (tmp) : : "memory");
		}
	}

out_timers:
	mtspr(SPR_TCR, ns.tcr);

	enable_int();

	printlog(LOGTYPE_PM, LOGLEVEL_ERROR,
	         "idle_loop: couldn't nap, using wait instead\n");

wait:
	while (1)
		asm volatile("wait");
}

void wake_hcall_nap(gcpu_t *gcpu)
{
	sync();

	if (gcpu->napping & GCPU_NAPPING_HCALL) {
		atomic_and(&gcpu->napping, ~GCPU_NAPPING_HCALL);
		unblock(&gcpu->thread);
		setevent(cpu0.client.gcpu, EV_SYNC_NAP);
	}
}

void hcall_exit_nap(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	unsigned int vcpu = regs->gpregs[4];
	gcpu_t *gcpu;
	int ret = 0;

	if (!rcpm) {
		regs->gpregs[3] = EV_ENODEV;
		return;
	}

	if (!guest || vcpu >= guest->cpucnt) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	wake_hcall_nap(guest->gcpus[vcpu]);
	regs->gpregs[3] = 0;
}
