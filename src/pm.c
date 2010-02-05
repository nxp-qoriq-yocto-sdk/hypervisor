/** @file
 *
 * p4080-style power management
 */

/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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

#include <libos/hcalls.h>
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

#define CNAPSRL   (0x014 / 4)	/* Core Nap Status */
#define CNAPCRL   (0x01c / 4)	/* Core Nap Control */
#define CWAITSRL  (0x034 / 4)	/* Core Wait Status */

typedef struct {
	int reg, state;
} sleep_status_t;

sleep_status_t sleep_regs[] = {
	{ CNAPSRL, FH_VCPU_NAP },
	{ CWAITSRL, FH_VCPU_IDLE },
};

static uint32_t *rcpm; /* run control and power management */

static int rcpm_probe(driver_t *drv, device_t *dev);

static driver_t __driver rcpm_driver = {
	.compatible = "fsl,qoriq-rcpm-1.0",
	.probe = rcpm_probe,
};

static int rcpm_probe(driver_t *drv, device_t *dev)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_PM, LOGLEVEL_ERROR, "rcpm: bad regs\n");
		return ERR_INVALID;
	}

	rcpm = (uint32_t *)dev->regs[0].virt;
	dev->driver = &rcpm_driver;

	return 0;
}

void hcall_get_core_state(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	unsigned int vcpu = regs->gpregs[4];
	gcpu_t *gcpu;
	int core, state = FH_VCPU_RUN;
	int i;

	if (!rcpm || !guest || vcpu >= guest->cpucnt) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	gcpu = guest->gcpus[vcpu];
	core = gcpu->cpu->coreid;

	assert(core < 32);

	for (i = 0; i < sizeof(sleep_regs) / sizeof(sleep_regs[0]); i++) {
		if (in32(&rcpm[sleep_regs[i].reg]) & (1 << core)) {
			state = sleep_regs[i].state;
			break;
		}
	}

	regs->gpregs[3] = 0;
	regs->gpregs[5] = state;
}

typedef struct nap_state {
	uint32_t tsr, tcr;
	uint32_t l1csr0, l1csr1, l2csr0;
} nap_state_t;

#define set_cache_reg(reg, val) do { \
	sync(); \
	isync(); \
	mtspr((reg), (val)); \
	isync(); \
} while (0)

static void restore_caches(nap_state_t *ns)
{
	set_cache_reg(SPR_L2CSR0, ns->l2csr0);
	set_cache_reg(SPR_L1CSR0, ns->l1csr0);
	set_cache_reg(SPR_L1CSR1, ns->l1csr1);
}

extern char _start;

/* No snooping during nap, so flush core caches, and stop allocation */
static int flush_caches(nap_state_t *ns)
{
	/* Arbitrary 100ms timeout for cache to flush */
	uint32_t timeout = dt_get_timebase_freq() / 10;

	ns->l1csr0 = mfspr(SPR_L1CSR0);
	ns->l1csr1 = mfspr(SPR_L1CSR1);
	ns->l2csr0 = mfspr(SPR_L2CSR0);

	if (ns->l2csr0 & L2CSR0_L2E) {
		uint32_t tb = mfspr(SPR_TBL);
		int lock = L2CSR0_L2IO | L2CSR0_L2DO;

		set_cache_reg(SPR_L2CSR0, ns->l2csr0 | L2CSR0_L2FL | lock);

		while ((mfspr(SPR_L2CSR0) & (L2CSR0_L2FL | lock)) != lock) {
			if (mfspr(SPR_TBL) - tb > timeout) {
				restore_caches(ns);
				printlog(LOGTYPE_PM, LOGLEVEL_ERROR,
				         "flush_caches: L2 cache "
				         "flush timeout\n");
				return -1;
			}
		}

		set_cache_reg(SPR_L2CSR0, ns->l2csr0 | L2CSR0_L2FI | lock);

		while ((mfspr(SPR_L2CSR0) & (L2CSR0_L2FI | lock)) != lock) {
			if (mfspr(SPR_TBL) - tb > timeout) {
				restore_caches(ns);
				printlog(LOGTYPE_PM, LOGLEVEL_ERROR,
				         "flush_caches: L2 cache "
				         "invalidate timeout\n");
				return -1;
			}
		}
	}

	if (flush_disable_l1(&_start, timeout) < 0) {
		restore_caches(ns);
		printlog(LOGTYPE_PM, LOGLEVEL_ERROR,
		         "flush_caches: L1 cache invalidate timeout\n");
		return -1;
	}

	return 0;
}

/* Runs only on the boot core (which cannot nap).  Checks every
 * gcpu, and puts to sleep any that need it.  Allowing cores to
 * touch CNAPCRL directly could race if multiple cores try to
 * sleep at the same time.
 */
void sync_nap(trapframe_t *regs)
{
	uint32_t cnapcrl = 0;
	int i;

	for (i = 0; i < MAX_CORES - 1; i++) {
		cpu_t *c = &secondary_cpus[i];
		gcpu_t *gcpu = c->client.gcpu;

		if (gcpu && gcpu->napping)
			cnapcrl |= 1 << c->coreid;
	}

	out32(&rcpm[CNAPCRL], cnapcrl);
}

void hcall_enter_nap(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	int vcpu = regs->gpregs[4];
	gcpu_t *gcpu = get_gcpu();
	nap_state_t ns;
	int core;
	int ret = EINVAL;

	/* Arbitrary 1ms spin period between sync_nap()s */
	uint32_t timeout = dt_get_timebase_freq() / 1000;

	if (!rcpm) {
		ret = ENODEV;
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

	disable_int();

	/* Disable decrementer, FIT, watchdog */
	ns.tcr = mfspr(SPR_TCR);
	mtspr(SPR_TCR, ns.tcr & ~(TCR_DIE | TCR_FIE | TCR_WIE));

	/* Remember watchdog status, so that we don't get confused when
	 * the watchdog expires during nap.
	 */
	ns.tsr = mfspr(SPR_TSR);

	if (flush_caches(&ns)) {
		ret = EIO;
		goto out_timers;
	}

	/* To avoid races with multiple nap transitions, we can't let
	 * cores set their own nap bit.  Instead, we ask the boot core
	 * to do it for us.  The napping flag also protects against
	 * spurious wakeups.
	 */

	gcpu->napping = 1;

	while (gcpu->napping) {
		uint32_t tb;
		setevent(cpu0.client.gcpu, EV_SYNC_NAP);

		/* We can't use "wait", at least with doorbells as a
		 * wakeup source, because the doorbell can get lost if
		 * we're still napping when it's sent.  Instead, spin in
		 * a minimally-intrusive manner, checking gcpu->napping
		 * every 100 us, and calling sync_nap() every 1000 us.
		 */

		tb = mfspr(SPR_TBL);
		while (mfspr(SPR_TBL) - tb < timeout) {
			uint32_t tb2 = mfspr(SPR_TBL);
			while (mfspr(SPR_TBL) - tb2 < timeout / 10)
				;

			if (!gcpu->napping)
				break;
		}
	}

	restore_caches(&ns);

	ret = 0;
out_timers:
	/* Restore decrementer, FIT, watchdog.  Make sure any watchdog
	 * status bits that were clear before are still clear.
	 */
	mtspr(SPR_TSR, ~ns.tsr & (TSR_ENW | TSR_WIS));
	mtspr(SPR_TCR, ns.tcr);

	enable_int();
out:
	regs->gpregs[3] = ret;
}

void hcall_exit_nap(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	unsigned int vcpu = regs->gpregs[4];
	gcpu_t *gcpu;
	int core;
	int ret = 0;

	if (!rcpm) {
		regs->gpregs[3] = ENODEV;
		return;
	}

	if (!guest || vcpu >= guest->cpucnt) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	gcpu = guest->gcpus[vcpu];
	core = gcpu->cpu->coreid;

	gcpu->napping = 0;
	setevent(cpu0.client.gcpu, EV_SYNC_NAP);

	regs->gpregs[3] = 0;
}
