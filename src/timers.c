/*
 * Timer support
 *
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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
#include <ipi_doorbell.h>
#include <timers.h>
#include <benchmark.h>

void decrementer(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	set_stat(bm_stat_decr, regs);

	/* Clear the interrupt now so that it won't immediately reassert. */
	mtspr(SPR_TSR, TSR_DIS);

	/* The guest will continue to receive decrementer interrupts
	 * reflected from the guest doorbell handler until it clears
	 * the guest DIS bit.
	 */
	atomic_or(&gcpu->gtsr, TSR_DIS);
	send_local_guest_doorbell();

	/* Reflect right away if possible to save a trap -- but
	 * we still need the doorbell armed in case the guest
	 * re-enables interrupts without clearing its DIS.
	 */
	if (likely((regs->srr1 & MSR_EE) && (regs->srr1 & MSR_GS))) {
		reflect_trap(regs);
		return;
	}
}

/**
 * fit -- Fixed Interval Timer interrupt handler
 *
 * Because we virtualize the FIT for the guest, the actual hardware FIT might
 * be running at a higher frequency than what the guest asked for.  So we need
 * to reflect the interrupt to the guest only when we're supposed to.  Let's
 * assume that the guest has programmed virtual FPEXT|FP to X (variable
 * 'bit').  We only want to call the guest if X has transitioned from 0 to 1.
 * So if this bit was 0 the last time this function was called, but it's 1
 * now, then call the guest.
 */
void fit(trapframe_t *regs)
{
	set_stat(bm_stat_fit, regs);
	uint64_t tb, mask;
	int was_zero; // True, if the period bit of the previous TB was zero
	unsigned int bit, hwbit;
	gcpu_t *gcpu = get_gcpu();

	// We always clear the FIS because we control the hardware FIT.
	mtspr(SPR_TSR, TSR_FIS);

	/* We use the current value of the timebase to determine whether we
	 * should reflect this interrupt to the guest and/or HV.  This does not
	 * need to be an atomic read because we only care about one bit at a
	 * time.
	 */
	tb = ((uint64_t)mfspr(SPR_TBU) << 32) | mfspr(SPR_TBL);

	/* Determine whether the TB bit that we care about has changed from
	 * 0 to 1.  If guest FIT period is *equal* to the hardware FIT period,
	 * then the bit we care about will never be 0.  To handle this, we force
	 * the hardware's FIT period bit to zero.  Note: bit >= hwbit
	 */
	hwbit = 63 - TCR_FP_TO_INT(mfspr(SPR_TCR));
	bit = 63 - TCR_FP_TO_INT(gcpu->gtcr);
	mask = (1ULL << bit) & ~(1ULL << hwbit);
	was_zero = (cpu->client.previous_tb & mask) == 0;

	// If the period bit was 0 last time, but is 1 this time, then reflect
	// the interrupt to the guest.
	if (was_zero && (tb & (1ULL << bit))) {
		// This is a FIT interrupt for the guest, so FIS should be set.
		atomic_or(&gcpu->gtsr, TSR_FIS);

		/* We only reflect the interrupt immediately if these three
		 * conditions are true:
		 *
		 * 1) We were interrupt from guest state (the guest was running
		 * when the interrupt occurred).
		 * 2) Guest interrupts are enabled in (guest MSR[EE] is 1).
		 * 3) Guest FIT interrupts are enabled (guest TCR[FIE] is 1).
		 */
		if (likely(gcpu->gtcr & TCR_FIE)) {
			if (likely((regs->srr1 & MSR_EE) && (regs->srr1 & MSR_GS)))
				reflect_trap(regs);

			/* There are three cases when we need to
			 * create a doorbell:
			 *
			 * 1) If we reflect the trap to the guest, we need to
			 * create a doorbell in case the guest does not clear
			 * FIS in its interrupt handler.  Because the FIT is
			 * level-sensitive, the FIT interrupt needs to be
			 * re-asserted after the RFI if FIS is not cleared, and
			 * the doorbell takes care of this.
			 *
			 * 2) If we do not reflect the trap because we were not
			 * in guest state when the FIT interrupt occured, then
			 * we need to reflect the interrupt when we return to
			 * guest state.
			 *
			 * 3) If we do not reflect the trap because MSR[EE] was
			 * disabled, then the trap will need to be reflected
			 * when the guest re-enables interrupts.
			 *
			 * We don't need to set a flag in gdbell_pending because
			 * guest_doorbell() checks the registers directly.
			 */
			send_local_guest_doorbell();
		}

		/* If we did not reflect the trap because TCR[FIE] was
		 * disabled, we will handle that in set_tcr() when the guest
		 * re-enables FIE.
		 */
	}

#if 0
	// Check for HV usage of the timer.
	bit = 63 - TCR_FP_TO_INT(cpu->client.hv_tcr);
	mask = (1ULL << bit) & ~(1ULL << hwbit);
	was_zero = (cpu->client.previous_tb & mask) == 0;

	if (was_zero && (tb & (1ULL << bit))) {
		// TODO: call function to handle HV timer interrupts
	}
#endif

	// Finally, remember the current timebase for next time
	cpu->client.previous_tb = tb;
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
	regs->srr1 &= MSR_ME | MSR_DE | MSR_GS | MSR_UCLE | MSR_RI;
}

/**
 * watchdog_trap - called whenever the watchdog timer generates an interrupt
 */
void watchdog_trap(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();

	set_stat(bm_stat_watchdog, regs);

	/* watchdog_timeout is used to emulate the second watchdog timeout.
	 * Normally, this timeout would cause a core reset and/or external
	 * interrupt.  To avoid this, we emulate the second timeout via
	 * watchdog_timeout.  If this variable is 1 when we get watchdog
	 * interrupt, then it means that the guest has missed two watchdog
	 * timeouts, so we need to do whatever the watchdog would do during a
	 * second timeout.
	 */
	if (gcpu->watchdog_timeout) {
		guest_t *guest = gcpu->guest;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			"Watchdog: second timeout\n");

		// Turn of TCR[WIE] so that watchdog_trap() isn't called again,
		// or prevent watchdog timeout during restart of the guest
		mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_WIE);

		// Does the guest want nothing to happen?
		if (!(gcpu->gtcr & TCR_WRC))
			return;

		// Either we notify the manager or we restart the guest.
		if (guest->wd_notify) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
				"Watchdog: notifying manager\n");

			send_doorbells(guest->dbell_watchdog_expiration);
		} else {
			int ret;
			char buf[64];
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
				"Watchdog: restarting partition\n");

			// Save the current value of TCR[WRC]
			gcpu->watchdog_tsr = gcpu->gtcr & TCR_WRC;

			ret = snprintf(buf, sizeof(buf), "vcpu-%d", gcpu->gcpu_num);
			assert(ret < (int)sizeof(buf));

			restart_guest(guest, buf, "watchdog");
		}
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
	if (gcpu->gtcr & TCR_WIE) {
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

// Bit mask of the bits that are remembered in gcpu->gtcr
#define GTCR_FLAGS_MASK (TCR_WRC | TCR_DIE | TCR_FIE | TCR_WIE | TCR_FP | TCR_FPEXT)

// Bit mask of the bits that are remembered in gcpu->gtsr
#define GTSR_FLAGS_MASK TSR_FIS

void set_tcr(uint32_t val)
{
	gcpu_t *gcpu = get_gcpu();

	/* TCR[WRC] can only be written to once.  If the guest tries to write
	 * non-zero to TCR[WRC] again, we override those bits with the current
	 * value.
	 */
	if (gcpu->gtcr & TCR_WRC)
		val = (val & ~TCR_WRC) | (gcpu->gtcr & TCR_WRC);

	gcpu->gtcr = val & GTCR_FLAGS_MASK;

	/* The WRC register controls what the watchdog does on the second
	 * timeout.  Because we emulate that behavior, we program our own value
	 * and just remember what the guest wanted.
	 *
	 * Regardless of what the guest wants, we configure the watchdog to do
	 * nothing on a second timeout. This way, if the hypervisor fails to
	 * react to the first timeout, nothing bad will happen.
	 */
	val = (val & ~TCR_WRC) | TCR_WRC_NOP;

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
	if (gcpu->gtcr & TCR_WRC)
		val |= TCR_WIE;

	// If the guest re-enables FIE, and there's an interrupt pending, then
	// create a doorbell so that we can send that pending interrupt.
	if ((val & TCR_FIE) && (gcpu->gtsr & TSR_FIS))
		send_local_guest_doorbell();

	// The hardware FIE is always enabled, so dn't let the guest turn it
	// off. We need FIT interrupts in order to properly emulate guest FIS
	val |= TCR_FIE;

	/* TODO: Check to see if the HV is already using the FIT, and if so,
	 * set the hardware TCR to the maximum (highest frequency) of the guest
	 * TCR and the HV TCR.
	 */

	mtspr(SPR_TCR, val);
}

void set_tsr(uint32_t tsr)
{
	gcpu_t *gcpu = get_gcpu();
	register_t saved;

	/*
	 * We disable critical interrupts so that watchdog_trap() is not called
	 * while we're working on TCR.
	 */
	saved = disable_int_save();

	if (tsr & TSR_DIS)
		atomic_and(&gcpu->gtsr, ~TSR_DIS);

	if (tsr & TSR_FIS) {
		// The FIT is emulated, so writing a 1 to FIS doesn't go to
		// the actual hardware.
		tsr &= ~TSR_FIS;

		// But we still need to remember that the guest cleared FIS
		atomic_and(&gcpu->gtsr, ~TSR_FIS);

		/* There might be a doorbell waiting to handle a pending FIT
		 * interrupt.  We don't have to worry about that because
		 * guest_doorbell() will notice that the conditions for a
		 * pending FIT are no longer there, and it won't do anything.
		 */
	}

	if (tsr & TSR_WIS) {
		atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_WATCHDOG);

		/* If the guest is resetting the watchdog, then we no longer
		 * emulate the second timeout.
		 */
		gcpu->watchdog_timeout = 0;
	}

	mtspr(SPR_TSR, tsr);

	// Since we're setting TSR, we don't want to remember the value of
	// TCR upon a watchdog reset any more.  gcpu->watchdog_tsr is non-zero
	// only if the system was reset by a watchdog.
	gcpu->watchdog_tsr = 0;

	restore_int(saved);
}

uint32_t get_tcr(void)
{
	uint32_t val = mfspr(SPR_TCR);

	val &= ~GTCR_FLAGS_MASK;
	val |= get_gcpu()->gtcr;

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

	// gcpu->watchdog_tsr is non-zero only if the system was reset by a
	// watchdog.
	if (gcpu->watchdog_tsr)
		val = (val & ~TSR_WRS) | (gcpu->watchdog_tsr & TSR_WRS);

	// FIS and DIS get cleared by the hypervisor immediately.
	// The guest has its own virtualized status bits.
	//
	// Only FIS keeps running despite the guest clearing the enable
	// bit, though, so the guest should also see DIS if it is set
	// in hardware.
	val &= ~TSR_FIS;
	val |= gcpu->gtsr & (TSR_FIS | TSR_DIS);

	return val;
}
