/** virtual interrupt controller support
 *
 * @file
 *
 */

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

#include <libos/console.h>
#include <libos/io.h>
#include <libos/spr.h>
#include <libos/8578.h>
#include <libos/bitops.h>
#include <vpic.h>
#include <vmpic.h>
#include <doorbell.h>
#include <events.h>

/**
 * virtual interrupts / vpic overview
 *
 *    Each guest has a vpic struct that contains an array 
 *    of vint data structures.  The index into the array
 *    is the vint number, and is  allocated by
 *    vpic_alloc_irq().
 *
 *    The vpic routines are invoked by the fh_vmpic* hypercalls
 *    through a pic_ops structure (see src/vmpic.c).
 *
 *    Asserting virtual interrupts
 *       -vpic_assert_vint -- asserts a virtual interrupt
 *       -This puts the interrupt in the pending state.
 *        and asserts a critical doorbell to the physical
 *        cpu.
 *       -the critical doorbell asserts a local guest doorbell
 *        (which is gated off of guest's EE)
 *
 *    Reflecting virtual interrupts to the cpu
 *      -reflecting interrupts is done in the guest doorbell
 *       handler
 *      -if there is no active interrupt and the interrupt is 
 *       not masked it moves into the active state
 *      -it remains active until EOI.
 *      -only one virtual interrupt (per guest) can be active
 *       at a time.
 *
 *    EOI clears the pending and active state of the interrupt.
 *
 */

int vpic_alloc_irq(guest_t *guest)
{
	register_t save;

	save = spin_lock_critsave(&guest->vpic.lock);
	int irq = guest->vpic.alloc_next++;
	guest->vpic.ints[irq].destcpu = 1;
	spin_unlock_critsave(&guest->vpic.lock,save);

	return irq;
}

void critdbell_to_gdbell_glue(trapframe_t *regs)
{
	send_local_guest_doorbell();
}

int vpic_process_pending_ints(guest_t *guest)
{
	register_t save;

	save = spin_lock_critsave(&guest->vpic.lock);

	uint32_t pending = guest->vpic.pending;

	/* look for a pending interrupt that is not masked */
	while (pending) {

		/* get int num */
		int irq = count_lsb_zeroes(pending);

		/* if any vint is already active defer */
		if (guest->vpic.active)
			break;

		if (guest->vpic.ints[irq].enable) {
			/* int now moves to active state */
			guest->vpic.active |= 1 << irq;

			if (guest->coreint) {
				mtspr(SPR_GEPR, guest->vpic.ints[irq].vector);
			}

			spin_unlock_critsave(&guest->vpic.lock, save);
	
			return 1;
		} else {
			pending &= ~(1 << irq);
		}
	}

	spin_unlock_critsave(&guest->vpic.lock, save);

	return 0;
}

void vpic_assert_vint_rxq(queue_t *q)
{
	vint_desc_t *vint = q->consumer;

	vpic_assert_vint(vint->guest, vint->vpic_irq);
}

void vpic_assert_vint_txq(queue_t *q)
{
	vint_desc_t *vint = q->producer;

	vpic_assert_vint(vint->guest, vint->vpic_irq);
}

void vpic_assert_vint(guest_t *guest, int irq)
{
	register_t save;
	uint8_t destcpu = count_lsb_zeroes(vpic_irq_get_destcpu(irq));

	/* set pending */
	save = spin_lock_critsave(&guest->vpic.lock);
	guest->vpic.pending |= 1 << irq;
	spin_unlock_critsave(&guest->vpic.lock, save);

	if (guest->vpic.ints[irq].enable) {
		setevent(guest->gcpus[destcpu], EV_ASSERT_VINT);
	}
}

void vpic_irq_mask(int irq)
{
	guest_t *guest = get_gcpu()->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	guest->vpic.ints[irq].enable = 0;

	spin_unlock_critsave(&guest->vpic.lock, save);
}

void vpic_irq_unmask(int irq)
{
	guest_t *guest = get_gcpu()->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	guest->vpic.ints[irq].enable = 1;
	if (guest->vpic.pending) {
		uint8_t destcpu = count_lsb_zeroes(vpic_irq_get_destcpu(irq));
		setevent(guest->gcpus[destcpu], EV_ASSERT_VINT);
	}

	spin_unlock_critsave(&guest->vpic.lock, save);
}

void vpic_irq_set_vector(int irq, uint32_t vector)
{
	guest_t *guest = get_gcpu()->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	guest->vpic.ints[irq].vector = vector;

	spin_unlock_critsave(&guest->vpic.lock, save);
}

uint32_t vpic_irq_get_vector(int irq)
{
	guest_t *guest = get_gcpu()->guest;
	return guest->vpic.ints[irq].vector;
}

void vpic_irq_set_destcpu(int irq, uint8_t destcpu)
{
	guest_t *guest = get_gcpu()->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	if (destcpu != 0 && (destcpu & ((1 << guest->cpucnt) - 1)))
		guest->vpic.ints[irq].destcpu = destcpu;

	spin_unlock_critsave(&guest->vpic.lock, save);
}

uint8_t vpic_irq_get_destcpu(int irq)
{
	guest_t *guest = get_gcpu()->guest;

	return guest->vpic.ints[irq].destcpu;
}

/* 
 * When iack is called, a single virtual interrupt is active
 * for the guest's vpic.  The iack call simply
 * returns the vector, no additional state transitions
 * occur.
 * 
 */
uint16_t vpic_iack(void)
{
	int irq;
	guest_t *guest = get_gcpu()->guest;
	if (!guest->vpic.active) {
		return 0xffff;  /* spurious */
	}

	irq = count_lsb_zeroes(guest->vpic.active);

	return guest->vpic.ints[irq].vector;
}

/*
 * Clears the interrupt from being active.
 *
 */
void vpic_eoi(void)
{
	int irq;
	guest_t *guest = get_gcpu()->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	irq = count_lsb_zeroes(guest->vpic.active);

	guest->vpic.active &= ~(1 << irq);
	guest->vpic.pending &= ~(1 << irq);

	/* check if more vints are pending */
	if (guest->vpic.pending) {
		irq = count_lsb_zeroes(guest->vpic.pending);
		uint8_t destcpu = count_lsb_zeroes(vpic_irq_get_destcpu(irq));
		setevent(guest->gcpus[destcpu], EV_ASSERT_VINT);
	}

	spin_unlock_critsave(&guest->vpic.lock, save);
}
