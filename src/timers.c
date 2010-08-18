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
 * watchdog_timeout -- called when a guest's watchdog has its final timeout
 *
 * This function is called when the guest watchdog emulator determines that
 * the watchdog for that guest has had its last timeout and needs to be reset,
 * or whatever other action is required.
 */
static void watchdog_timeout(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;

	if (!(gcpu->gtcr & TCR_WRC))
		return;

	if (guest->wd_action == wd_notify) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			"Watchdog: notifying manager\n");

		send_doorbells(guest->dbell_watchdog_expiration);
	} else if (guest->wd_action == wd_stop) {
		char buf[64];
		int ret;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			 "Watchdog: stopping partition\n");

		ret = snprintf(buf, sizeof(buf), "vcpu-%d", gcpu->gcpu_num);
		assert(ret < (int)sizeof(buf));

		stop_guest(guest, buf, "watchdog");
	} else if (guest->wd_action == wd_reset) {
		char buf[64];
		int ret;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			 "Watchdog: restarting partition\n");

		// Save the current value of TCR[WRC]
		gcpu->watchdog_tsr = gcpu->gtcr & TCR_WRC;

		ret = snprintf(buf, sizeof(buf), "vcpu-%d", gcpu->gcpu_num);
		assert(ret < (int)sizeof(buf));

		restart_guest(guest, buf, "watchdog");
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
	 * then the bit we care about will never be 0.  To handle this, we set
	 * the hardware's FIT period bit (hwbit) to zero.  Note: bit >= hwbit
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

		/* We only reflect the interrupt (via a doorbell) if these three
		 * conditions are true:
		 *
		 * 1) We were interrupt from guest state (the guest was running
		 * when the interrupt occurred).
		 * 2) Guest interrupts are enabled in (guest MSR[EE] is 1).
		 * 3) Guest FIT interrupts are enabled (guest TCR[FIE] is 1).
		 */
		if (likely(gcpu->gtcr & TCR_FIE)) {
			/* If MSR[EE] and MSR[GS] are both set, then normally we
			 * could just reflect the trap right away.  However,
			 * because both the watchdog and FI timers might
			 * expire at the same time, and we can't call
			 * reflect_trap() twice, it's just simpler to handle all
			 * interrupt reflections in the doorbell handler.
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

	// Check for watchdog expiration
	bit = 63 - TCR_WP_TO_INT(gcpu->gtcr);
	mask = (1ULL << bit) & ~(1ULL << hwbit);
	was_zero = (cpu->client.previous_tb & mask) == 0;

	if (was_zero && (tb & (1ULL << bit))) {
		// See AN2804 for a description of the watchdog ENW|WIS behavior
		switch (gcpu->gtsr & (TSR_ENW | TSR_WIS)) {
		case 0:
			atomic_or(&gcpu->gtsr, TSR_ENW);
			break;
		case TSR_ENW:
			atomic_or(&gcpu->gtsr, TSR_WIS);
			if (likely(gcpu->gtcr & TCR_WIE))
				send_local_crit_guest_doorbell();
			break;
		case TSR_WIS:
			/* The only way we can get here is if we wait until
			 * ENW,WIS == 1,1, and we clear ENW before the chip
			 * resets.  Clearing ENW does not clear the interrupt,
			 * however, so there should still be an interrupt
			 * pending.
			 */
			atomic_or(&gcpu->gtsr, TSR_ENW);
			break;
		case TSR_ENW | TSR_WIS:
			watchdog_timeout();
			break;
		}
	}

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
#ifdef CONFIG_LIBOS_64BIT
	if (mfspr(SPR_EPCR) & EPCR_GICM)
		regs->srr1 |= MSR_CM;
#endif
}

/**
 * watchdog_trap - called whenever the watchdog timer generates an interrupt
 */
void watchdog_trap(trapframe_t *regs)
{
	if (!system_health_check())
		// Ping the watchdog
		mtspr(SPR_TSR, TSR_WIS);
}

void set_tcr(uint32_t val)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned int period;
	register_t tcr = mfspr(SPR_TCR);

	/* The watchdog is fully emulated by the hypervisor, so we never allow
	 * the guest to read or modify any of the real watchdog bits in TCR.
	 *
	 * The hardware does not allow the guest to write a different value to
	 * TCR[WRC], if those bits are already non-zero.
	 */
	if (gcpu->gtcr & TCR_WRC)
		val = (val & ~TCR_WRC) | (gcpu->gtcr & TCR_WRC);

	// Technically, TCR[ARE] is not emulated, but it's okay to store it in
	// gtcr.  This way, real TCR[ARE] and gtcr[TCR_ARE] will always be the
	// same.
	gcpu->gtcr = val;

	// If the guest re-enables WIE, and there's an interrupt pending, then
	// create a doorbell so that we can send that pending interrupt.
	if ((val & TCR_WIE) && (gcpu->gtsr & TSR_WIS))
		send_local_crit_guest_doorbell();

	// If the guest re-enables FIE, and there's an interrupt pending, then
	// create a doorbell so that we can send that pending interrupt.
	if ((val & TCR_FIE) && (gcpu->gtsr & TSR_FIS))
		send_local_guest_doorbell();

	// If the guest re-enables DIE, and there's an interrupt pending, then
	// create a doorbell so that we can send that pending interrupt.
	if ((val & TCR_DIE)&&  (gcpu->gtsr & TSR_DIS))
		send_local_guest_doorbell();

	// Set the actual hardware FIT period to the maximum of the guest
	// watchdog and guest FIT periods
	period = max(TCR_FP_TO_INT(gcpu->gtcr), TCR_WP_TO_INT(gcpu->gtcr));
	tcr = (tcr & ~TCR_FP_MASK) | TCR_INT_TO_FP(period);

	// Pass on any bits that are not fully emulated
	tcr = (tcr & ~GCPU_TCR_HW_BITS) | (val & GCPU_TCR_HW_BITS);

	mtspr(SPR_TCR, tcr);
}

void set_tsr(uint32_t tsr)
{
	gcpu_t *gcpu = get_gcpu();

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
		// The watchdog is emulated, so writing a 1 to WIS doesn't go to
		// the actual hardware.
		tsr &= ~TSR_WIS;

		// But we still need to remember that the guest cleared WIS
		atomic_and(&gcpu->gtsr, ~TSR_WIS);

		/* There might be a doorbell waiting to handle a pending
		 * watchdog interrupt.  We don't have to worry about that
		 * because guest_doorbell() will notice that the conditions for
		 * a pending watchdog are no longer there, and it won't do
		 * anything.
		 */
	}

	if (tsr & TSR_ENW) {
		// The watchdog is emulated, so writing a 1 to ENW doesn't go to
		// the actual hardware.
		tsr &= ~TSR_ENW;

		// But we still need to remember that the guest cleared ENW
		atomic_and(&gcpu->gtsr, ~TSR_ENW);
	}

	mtspr(SPR_TSR, tsr);

	// Since we're setting guest TSR, we don't want to remember the value of
	// TCR upon a watchdog reset any more.  gcpu->watchdog_tsr is non-zero
	// only if the partition was reset by a watchdog.
	gcpu->watchdog_tsr = 0;
}

uint32_t get_tcr(void)
{
	gcpu_t *gcpu = get_gcpu();
	uint32_t tcr = mfspr(SPR_TCR);

	return (tcr & GCPU_TCR_HW_BITS) | (gcpu->gtcr & ~GCPU_TCR_HW_BITS);
}

uint32_t get_tsr(void)
{
	gcpu_t *gcpu = get_gcpu();
	uint32_t val = mfspr(SPR_TSR);

	// gcpu->watchdog_tsr is non-zero only if the system was reset by a
	// watchdog.
	if (gcpu->watchdog_tsr)
		val = (val & ~TSR_WRS) | (gcpu->watchdog_tsr & TSR_WRS);

	/* FIS and DIS get cleared by the hypervisor immediately. The
	 * guest has its own virtualized status bits.
	 *
	 * The watchdog is fully emulated, so we never want to pass the
	 * hardware ENW and WIS bits to the guest.
	 *
	 * Only FIS keeps running despite the guest clearing the enable
	 * bit, though, so the guest should also see DIS if it is set
	 * in hardware.
	 *
	 * If the guest clears TCR[DIE], then we clear it in hardware
	 * immediately. This is different than how we treat the other timers.
	 *
	 * If the hardware DIS is set, but gtsr[DIS] is cleared, then it means
	 * that the decrementer expired while TCR[DIE] was cleared, so the HV
	 * never got the interrupt, so it couldn't update gtsr[DIS].  In this
	 * case, we want guest DIS to be set.
	 *
	 * If the hardware DIS is cleared, but gtsr[DIS] is set, then it means
	 * that the hypevisor got the decrementer interrupt and cleared the
	 * hardware DIS, but the guest hasn't cleared gtsr[DIS] yet.  In this
	 * case, we want guest DIS to be set.
	 *
	 * Only the FIS, DIS, WIS, and ENW bits are set in gtsr, so there's no
	 * need to mask gtsr.
	 */
	val = (val & ~(TSR_FIS | TSR_ENW | TSR_WIS)) | gcpu->gtsr;

	return val;
}


int system_health_check(void) __attribute__ ((weak));
/**
 * check the health of the system
 *
 * @return non-zero if an error was detected 
 */
int system_health_check(void)
{
	return 0;
}
