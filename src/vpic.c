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

void critdbell_to_gdbell_glue(trapframe_t *regs)
{
	send_local_guest_doorbell();
}

static void send_vint(gcpu_t *gcpu)
{
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
	         "sending vint to cpu%d\n", gcpu->cpu->coreid);
	atomic_or(&gcpu->gdbell_pending, GCPU_PEND_VIRQ);
	setevent(gcpu, EV_ASSERT_VINT);
}

static void __vpic_assert_vint(vpic_interrupt_t *virq)
{
	uint32_t cpumask, destcpu;
	guest_t *guest = virq->guest;

	cpumask = virq->destcpu;
	assert(cpumask);
	destcpu = count_lsb_zeroes(cpumask);
	assert(destcpu < guest->cpucnt);

	gcpu_t *gcpu = guest->gcpus[destcpu];
	virq->pending = 1;

	if (!(gcpu->vpic.pending & (1 << virq->irqnum))) {
		if (virq->enable) {
			gcpu->vpic.pending |= 1 << virq->irqnum;
			send_vint(gcpu);
		} else {
			printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
			         "VPIC IRQ %p disabled\n", virq);
		}
	} else {
		printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
		         "VPIC IRQ %p already pending in CPU\n", virq);
	}
}

void vpic_assert_vint(vpic_interrupt_t *virq)
{
	register_t save;
	guest_t *guest = virq->guest;

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "assert virq %p\n", virq);

	save = spin_lock_critsave(&guest->vpic.lock);

	if (!virq->pending)
		__vpic_assert_vint(virq);
	else
		printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
		         "VPIC IRQ %p already pending\n", virq);

	spin_unlock_critsave(&guest->vpic.lock, save);
}

void vpic_deassert_vint(vpic_interrupt_t *virq)
{
	register_t save;
	guest_t *guest = virq->guest;

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "deassert virq %p\n", virq);

	save = spin_lock_critsave(&guest->vpic.lock);
	virq->pending = 0;
	spin_unlock_critsave(&guest->vpic.lock, save);
}

static vpic_interrupt_t *__vpic_iack(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	uint32_t pending;

	if (gcpu->vpic.active) {
		int irqnum = count_lsb_zeroes(gcpu->vpic.active);
		return &guest->vpic.ints[irqnum];
	}

	/* if any vint is already active defer */
	pending = gcpu->vpic.pending;

	/* look for a pending interrupt that is not masked */
	while (pending) {
		/* get int num */
		int irqnum = count_lsb_zeroes(pending);
		vpic_interrupt_t *virq = &guest->vpic.ints[irqnum];

		if (!virq->pending || !virq->enable) {
			/* IRQ was de-asserted, or is masked. */
			gcpu->vpic.pending &= ~(1 << irqnum);
		} else {
			if (virq->destcpu & (1 << gcpu->gcpu_num)) {
				/* int now moves to active state */
				gcpu->vpic.active |= 1 << irqnum;
				virq->active = 1;

				if (!virq->level) {
					gcpu->vpic.pending &= ~(1 << irqnum);
					virq->pending = 0;
				}

				return virq;
			}
			
			/* Tsk, tsk.  The guest changed the destcpu mask while the
			 * interrupt was pending.  Reissue it if it's still active.
			 */
			if (virq->pending) {
				printlog(LOGTYPE_IRQ, LOGLEVEL_NORMAL,
				         "cpu%d: vpic_iack: changed destcpu while pending\n",
				         cpu->coreid);
				__vpic_assert_vint(virq);
			}
		} 

		pending &= ~(1 << irqnum);
	}

	return NULL;
}

interrupt_t *vpic_iack(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	vpic_interrupt_t *virq;

	register_t save = spin_lock_critsave(&guest->vpic.lock);
	virq = __vpic_iack();
	spin_unlock_critsave(&guest->vpic.lock, save);

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic iack: %p\n", virq);

	return virq ? &virq->irq : NULL;
}

void vpic_assert_vint_rxq(queue_t *q)
{
	vpic_assert_vint(q->consumer);
}

void vpic_assert_vint_txq(queue_t *q)
{
	vpic_assert_vint(q->producer);
}

static void vpic_irq_mask(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;

	register_t save = spin_lock_critsave(&guest->vpic.lock);
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic mask: %p\n", virq);
	virq->enable = 0;
	spin_unlock_critsave(&guest->vpic.lock, save);
}

static void vpic_irq_unmask(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	virq->enable = 1;
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic unmask: %p\n", virq);
	
	if (virq->pending)
		__vpic_assert_vint(virq);
	
	spin_unlock_critsave(&guest->vpic.lock, save);
}

static int vpic_irq_is_enabled(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	return virq->enable;
}

static void vpic_irq_set_destcpu(interrupt_t *irq, uint32_t destcpu)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	register_t save;

	assert(destcpu != 0 && (destcpu & ((1 << guest->cpucnt) - 1)));
	
	save = spin_lock_critsave(&guest->vpic.lock);
	virq->destcpu = destcpu;
	spin_unlock_critsave(&guest->vpic.lock, save);
}

/*
 * Clears the interrupt from being active.
 *
 */
static void vpic_eoi(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	gcpu_t *gcpu = get_gcpu();
	register_t save = spin_lock_critsave(&guest->vpic.lock);

	assert(gcpu->guest == guest);

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic eoi: %p\n", virq);

	gcpu->vpic.active &= ~(1 << virq->irqnum);
	virq->active = 0;

	/* check if more vints are pending */
	if (gcpu->vpic.active == 0 && gcpu->vpic.pending)
		send_vint(gcpu);

	spin_unlock_critsave(&guest->vpic.lock, save);
}

static uint32_t vpic_irq_get_destcpu(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	return virq->destcpu;
}

static int vpic_irq_is_active(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	register_t save;
	int ret;

	save = spin_lock_critsave(&guest->vpic.lock);
	ret = virq->pending || virq->active;
	spin_unlock_critsave(&guest->vpic.lock, save);

	return ret;
}

int_ops_t vpic_ops = {
	.eoi = vpic_eoi,
	.enable = vpic_irq_unmask,
	.disable = vpic_irq_mask,
	.is_enabled = vpic_irq_is_enabled,
	.set_cpu_dest_mask = vpic_irq_set_destcpu,
	.get_cpu_dest_mask = vpic_irq_get_destcpu,
	.is_active = vpic_irq_is_active,
};

vpic_interrupt_t *vpic_alloc_irq(guest_t *guest)
{
	register_t save;
	vpic_interrupt_t *virq = NULL;
	int irq;

	save = spin_lock_critsave(&guest->vpic.lock);

	if (guest->vpic.alloc_next < MAX_VINT_CNT) {
		irq = guest->vpic.alloc_next++;
		virq = &guest->vpic.ints[irq];

		virq->destcpu = 1;
		virq->irq.ops = &vpic_ops;
		virq->guest = guest;
		virq->irqnum = irq;
	}

	spin_unlock_critsave(&guest->vpic.lock,save);
	return virq;
}
