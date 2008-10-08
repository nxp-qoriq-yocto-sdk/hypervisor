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

/* Eventually, this will do virtualized timers -- but for now,
 * just let the guest run things.
 */

#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <percpu.h>
#include <libos/console.h>
#include <libos/core-regs.h>
#include <doorbell.h>
#include <libos/interrupts.h>
#include <libos/mpic.h>
#include <events.h>

void decrementer(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	gcpu->stats[stat_decr]++;

	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_DIE);

	if (__builtin_expect(!!(regs->srr1 & MSR_EE), 1)) {
		reflect_trap(regs);
		return;
	}

	/* The guest has interrupts disabled, so defer it. */
	atomic_or(&gcpu->gdbell_pending, GCPU_PEND_DECR);
	send_local_guest_doorbell();
}

void run_deferred_decrementer(void)
{
	gcpu_t *gcpu = get_gcpu();
	atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_DECR);

	if (gcpu->timer_flags & TCR_DIE) {
		atomic_or(&gcpu->gdbell_pending, GCPU_PEND_TCR_DIE);
		send_local_guest_doorbell();
	}
}

void enable_tcr_die(void)
{
	gcpu_t *gcpu = get_gcpu();
	atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_TCR_DIE);

	if (gcpu->timer_flags & TCR_DIE)
		mtspr(SPR_TCR, mfspr(SPR_TCR) | TCR_DIE);
}

void fit(trapframe_t *regs)
{
	if (__builtin_expect(!!(regs->srr1 & MSR_EE), 1)) {
		reflect_trap(regs);
		return;
	}

	/* We disable the FIT interrupt because when we jump to the guest, GS is
	 * set to one.  When GS=1, interrupts are enabled (EE and CE are
	 * ignored). The core ignores EE/CE because we don't want the guest to
	 * be able to disable interrupts for the hypervisor.  The side-effect is
	 * that as soon as interrupts are enabled, the FIT interrupt will be
	 * triggered again.  This is because the FIT interrupt remains pending
	 * until the guest clears the FIT (by writing 1 to TSR[FIS]), and that
	 * won't happen until (the end of) the guest's ISR.  We trap the write
	 * to TSR[FIS], and so after TSR[FIS] is cleared, we re-enable the FIT
	 * interrupt.
	 */
	atomic_or(&get_gcpu()->gdbell_pending, GCPU_PEND_FIT);
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_FIE);

	send_local_guest_doorbell();
}

void run_deferred_fit(void)
{
	gcpu_t *gcpu = get_gcpu();
	atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_FIT);

	if (gcpu->timer_flags & TCR_FIE) {
		atomic_or(&gcpu->gdbell_pending, GCPU_PEND_TCR_FIE);
		send_local_guest_doorbell();
	}
}

void enable_tcr_fie(void)
{
	gcpu_t *gcpu = get_gcpu();
	atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_TCR_DIE);

	if (gcpu->timer_flags & TCR_FIE)
		mtspr(SPR_TCR, mfspr(SPR_TCR) | TCR_FIE);
}

/**
 * reflect_watchdog - reflect a watchdog timeout interrupt to a core
 *
 * We save the values of CSSR0 and CSSR1 into gcpu for emu_mfspr() and
 * emu_mtspr().  Since there are no GCSRR0 and GCSRR1 registers, the guest
 * can't access CSRR0 or CSRR1 directly when the hypervisor is running.
 * These registers need to be emulated.
 *
 * Never call this function directly. It should only be called as
 * a response to send_local_crit_guest_doorbell().
 */
void reflect_watchdog(gcpu_t *gcpu, trapframe_t *regs)
{
	gcpu->csrr0 = regs->srr0;
	gcpu->csrr1 = regs->srr1;

	regs->srr0 = gcpu->ivpr | gcpu->ivor[EXC_WDOG];
	regs->srr1 &= MSR_ME | MSR_DE | MSR_GS | MSR_UCLE;
}

/**
 * watchdog_trap - called whenever the watchdog timer generates an interrupt
 */
void watchdog_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	/* watchdog_timeout is used to emulate the second watchdog timeout.
	 * Normally, this timeout would cause a core reset and/or external
	 * interrupt.  To avoid this, we emulate the second timeout via
	 * watchdog_timeout.  If this variable is 1 when we get watchdog
	 * interrupt, then it means that the guest has missed two watchdog
	 * timeouts, so we need to do whatever the watchdog would do during a
	 * second timeout.
	 */
	if (gcpu->watchdog_timeout) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			"Watchdog: restarting partition\n");

		/* Disable the watchdog so we don't get another interrupt
		 * while we're restarting the guest.
		 */
		mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_WIE);
		mtspr(SPR_TSR, TSR_ENW | TSR_WIS);
		restart_guest(gcpu->guest);
		return;
	}

	/* We received the first watchdog timeout, so let's remember that in
	 * case we get another one before the guest resets the watchdog timer.
	 * Note that we actually reset the watchdog timer ourselves.  We do this
	 * because we don't want to have the interrupt come right back as soon
	 * as we return to guest state.
	 */
	gcpu->watchdog_timeout = 1;
	mtspr(SPR_TSR, TSR_WIS);

	/* Unlike the Decrementer and Fixed-Interval timers, we don't disable
	 * the Watchdog interrupt.  First, we don't ever want the watchdog
	 * interrupt to be disabled, because otherwise the HV might miss it.
	 * Second, we've already cleared the interrupt status above, so there's
	 * no need to support a pending interrupt.  If the guest thinks that WIE
	 * is disabled, then we just won't reflect the interrupt.
	 */
	if (gcpu->timer_flags & TCR_WIE) {
		/* We have already cleared the interrupt (by writing to
		 * TSR[WIS], so we need to emulate the interrupt whenever the
		 * guest would normally get it.
		 *
		 * In this case, even if WIE and MSR[CE] are enabled, it's still
		 * possible for the guest to get multiple watchdog interrupts
		 * for the same event.  This can happen if the guest doesn't
		 * clear the virtual TCR[WIS] but does enable MSR[CE].  As long
		 * as virtual-TCR[WIS] is not cleared, every time the guest
		 * re-enables MSR[CE], it will get a watchdog interrupt.
		 *
		 * To support that, we need to send a guest critical doorbell.
		 * At this moment, the MSR[CE] is disabled, since we're in
		 * a critical interrupt handler.  It will stay disabled in the
		 * guest's critical interrupt handler as well.  If the guest
		 * clears TSR[WIS], then set_tsr() will be called, which will
		 * clear GCPU_PEND_WATCHDOG.
		 *
		 * However, if the guest doesn't clear TSR[WIS], then as soon
		 * as MSR[CE] is enabled in the guest, guest_critical_doorbell()
		 * will be called.  Since GCPU_PEND_WATCHDOG is set, we'll send
		 * another watchdog interrupt to the guest.
		 */
		atomic_or(&gcpu->crit_gdbell_pending, GCPU_PEND_WATCHDOG);
		send_local_crit_guest_doorbell();
	}
}

/* Bit mask of the bits that are remembered in gcpu->timer_flags */
#define TIMER_FLAGS_MASK (TCR_WRC | TCR_DIE | TCR_FIE | TCR_WIE)

void set_tcr(uint32_t val)
{
	gcpu_t *gcpu = get_gcpu();

	/* TCR[WRC] can only be written to once.  If the guest tries to write
	 * non-zero to TCR[WRC] again, we override those bits with the current
	 * value.
	 */
	if (gcpu->timer_flags & TCR_WRC)
		val = (val & ~TCR_WRC) | (gcpu->timer_flags & TCR_WRC);

	gcpu->timer_flags = val & TIMER_FLAGS_MASK;

	/* The WRC register controls what the watchdog does on the second
	 * timeout.  Because we emulate that behavior, we program our own value
	 * and just remember what the guest wanted.
	 *
	 * Regardless of what the guest wants, we configure the watchdog to do
	 * nothing on a second timeout. This way, if the hypervisor fails to
	 * react to the first timeout, nothing bad will happen.
	 */
	val = (val & ~TCR_WRC) | TCR_WRC_NOP;

	if (gcpu->gdbell_pending & GCPU_PEND_DECR)
		val &= ~TCR_DIE;
	if (gcpu->gdbell_pending & GCPU_PEND_FIT)
		val &= ~TCR_FIE;

	if (val & TCR_WIE) {
		/* If there's a watchdog interrupt pending while the virtual
		 * WIE was disabled, and the guest re-enables it, then send
		 * that interrupt now.  watchdog_timeout is non-zero only when
		 * there's a pending watchdog interrupt.
		 */
		if (gcpu->watchdog_timeout) {
			atomic_or(&gcpu->crit_gdbell_pending, GCPU_PEND_WATCHDOG);
			send_local_crit_guest_doorbell();
		}
	} else
		/* If the guest disables watchodog interrupts, then we
		 * obviously should not send one.
		 */
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_WATCHDOG);

	/* Once the guest has enabled watchdog support (by programming something
	 * other than zero into TRC[WRC], we keep watchdog interrupts enabled
	 * forever, since we always want the HV to receive them. Otherwise, it
	 * might miss the first timeout interrupt and then we won't be able to
	 * emulate the second timeout (e.g. restart the partition).
	 */
	if (gcpu->timer_flags & TCR_WRC)
		val |= TCR_WIE;

	mtspr(SPR_TCR, val);
}

void set_tsr(uint32_t tsr)
{
	gcpu_t *gcpu = get_gcpu();
	register_t saved;
	register_t tcr;

	/*
	 * We disable critical interrupts so that watchdog_trap() is not called
	 * while we're working on TCR.
	 */
	saved = disable_critint_save();

	tcr = mfspr(SPR_TCR);

	if (tsr & TSR_DIS) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_DECR);
		tcr |= (gcpu->timer_flags & TCR_DIE);
	}

	if (tsr & TSR_FIS) {
		atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_FIT);
		tcr |= (gcpu->timer_flags & TCR_FIE);
	}

	if (tsr & TSR_WIS) {
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_WATCHDOG);

		/* If the guest is resetting the watchdog, then we no longer
		 * emulate the second timeout.
		 */
		gcpu->watchdog_timeout = 0;
	}

	mtspr(SPR_TSR, tsr);
	mtspr(SPR_TCR, tcr);

	restore_critint(saved);
}

uint32_t get_tcr(void)
{
	uint32_t val = mfspr(SPR_TCR);

	val &= ~TIMER_FLAGS_MASK;
	val |= get_gcpu()->timer_flags;

	return val;
}

uint32_t get_tsr(void)
{
	gcpu_t *gcpu = get_gcpu();
	uint32_t val = mfspr(SPR_TSR);

	/* If watchdog_timeout is true, then it means that the HV has reset
	 * TSR[ENW,WIS] to 0b10, but we're pretending it's 0b11.  We emulate the
	 * second timeout of the watchdog.
	 */
	if (gcpu->watchdog_timeout)
		val |= TSR_WIS;

	return val;
}
