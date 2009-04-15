/** virtual interrupt controller support
 *
 * @file
 *
 */

/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <libos/core-regs.h>
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

void dbell_to_gdbell_glue(trapframe_t *regs)
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

static void set_virq_pending(vpic_interrupt_t *virq, gcpu_t *gcpu)
{
	gcpu->vpic.pending[virq->irqnum / LONG_BITS] |=
					1 << (virq->irqnum % LONG_BITS);
	virq->pending = 1;
}

static void set_virq_active(vpic_interrupt_t *virq, gcpu_t *gcpu)
{
	gcpu->vpic.active[virq->irqnum / LONG_BITS] |=
					1 << (virq->irqnum % LONG_BITS);
	virq->active = 1;
}

static void clear_virq_pending(vpic_interrupt_t *virq, gcpu_t *gcpu)
{

	gcpu->vpic.pending[virq->irqnum / LONG_BITS] &=
					~(1 << (virq->irqnum % LONG_BITS));
	virq->pending = 0;
}

static void clear_virq_active(vpic_interrupt_t *virq, gcpu_t *gcpu)
{
	gcpu->vpic.active[virq->irqnum / LONG_BITS] &=
					~(1 << (virq->irqnum % LONG_BITS));
	virq->active = 0;
}

static int virq_pending(vpic_interrupt_t *virq, gcpu_t *gcpu)
{
	return gcpu->vpic.pending[virq->irqnum / LONG_BITS] &
					(1 << (virq->irqnum % LONG_BITS));
}


static void clear_gcpu_pending_virq(uint8_t irqnum, gcpu_t *gcpu)
{
	gcpu->vpic.pending[irqnum / LONG_BITS] &= ~(1 << (irqnum%LONG_BITS));
}


static int gcpu_virq_active(gcpu_t *gcpu)
{
	for (unsigned int i = 0; i < MAX_VINT_INDEX; i++) {
		if (gcpu->vpic.active[i])
			return 1;
	}

	return 0;
}

static int gcpu_virq_pending(gcpu_t *gcpu)
{
	for (unsigned int i = 0; i < MAX_VINT_INDEX; i++) {
		if (gcpu->vpic.pending[i])
			return 1;
	}

	return 0;
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

	if (!virq_pending(virq, gcpu)) {
		if (virq->enable) {
			set_virq_pending(virq, gcpu);
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

	save = spin_lock_intsave(&guest->vpic.lock);

	if (!virq->pending)
		__vpic_assert_vint(virq);
	else
		printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
		         "VPIC IRQ %p already pending\n", virq);

	spin_unlock_intsave(&guest->vpic.lock, save);
}

void vpic_deassert_vint(vpic_interrupt_t *virq)
{
	register_t save;
	guest_t *guest = virq->guest;

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "deassert virq %p\n", virq);

	save = spin_lock_intsave(&guest->vpic.lock);
	virq->pending = 0;
	spin_unlock_intsave(&guest->vpic.lock, save);
}

static vpic_interrupt_t *get_pending_virq(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned int irq = 0;

	for (unsigned int i = 0; i < MAX_VINT_INDEX; i++, irq += LONG_BITS) {
		if (gcpu->vpic.pending[i]) {
			irq += count_lsb_zeroes(gcpu->vpic.pending[i]);
			return &guest->vpic.ints[irq];
		}
	}

	return NULL;
}

static vpic_interrupt_t *get_active_virq(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned int irq = 0;

	for (unsigned int i = 0; i < MAX_VINT_INDEX; i++, irq += LONG_BITS) {
		if (gcpu->vpic.active[i]) {
			irq += count_lsb_zeroes(gcpu->vpic.active[i]);
			return &guest->vpic.ints[irq];
		}
	}

	return NULL;
}


static vpic_interrupt_t *__vpic_iack(void)
{
	vpic_interrupt_t *virq;
	gcpu_t *gcpu = get_gcpu();

	virq = get_active_virq();
	if (virq)
		return virq;

	/* look for a pending interrupt that is not masked */
	virq = get_pending_virq();
	while (virq) {
		if (!virq->pending || !virq->enable) {
			/* IRQ was de-asserted, or is masked. */
			clear_gcpu_pending_virq(virq->irqnum, gcpu);
		} else {
			if (virq->destcpu & (1 << gcpu->gcpu_num)) {
				/* int now moves to active state */
				set_virq_active(virq, gcpu);

				if (!(virq->config & IRQ_LEVEL))
					clear_virq_pending(virq, gcpu);

				return virq;
			}
			
			/* Tsk, tsk.  The guest changed the destcpu mask
			 * while the interrupt was pending. Reissue it if
			 * it's still active.
			 */
			if (virq->pending) {
				printlog(LOGTYPE_IRQ, LOGLEVEL_NORMAL,
					"vpic_iack: changed destcpu while"
					"pending\n");
				__vpic_assert_vint(virq);
			}
		}

		virq = get_pending_virq();
	}

	return NULL;
}

interrupt_t *vpic_iack(void)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	vpic_interrupt_t *virq;

	register_t save = spin_lock_intsave(&guest->vpic.lock);
	atomic_and(&gcpu->gdbell_pending, ~(GCPU_PEND_VIRQ));
	virq = __vpic_iack();
	spin_unlock_intsave(&guest->vpic.lock, save);

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

	register_t save = spin_lock_intsave(&guest->vpic.lock);
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic mask: %p\n", virq);
	virq->enable = 0;
	spin_unlock_intsave(&guest->vpic.lock, save);
}

static void vpic_irq_unmask(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	register_t save = spin_lock_intsave(&guest->vpic.lock);

	virq->enable = 1;
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic unmask: %p\n", virq);
	
	if (virq->pending)
		__vpic_assert_vint(virq);
	
	spin_unlock_intsave(&guest->vpic.lock, save);
}

static int vpic_irq_is_disabled(interrupt_t *irq)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	return !virq->enable;
}

static void vpic_irq_set_destcpu(interrupt_t *irq, uint32_t destcpu)
{
	vpic_interrupt_t *virq = to_container(irq, vpic_interrupt_t, irq);
	guest_t *guest = virq->guest;
	register_t save;

	assert(destcpu != 0 && (destcpu & ((1 << guest->cpucnt) - 1)));
	
	save = spin_lock_intsave(&guest->vpic.lock);
	virq->destcpu = destcpu;
	spin_unlock_intsave(&guest->vpic.lock, save);
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
	register_t save = spin_lock_intsave(&guest->vpic.lock);

	assert(gcpu->guest == guest);

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vpic eoi: %p\n", virq);

	clear_virq_active(virq, gcpu);

	/* check if more vints are pending */
	if (!gcpu_virq_active(gcpu) && gcpu_virq_pending(gcpu))
		send_vint(gcpu);

	spin_unlock_intsave(&guest->vpic.lock, save);
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

	save = spin_lock_intsave(&guest->vpic.lock);
	ret = virq->pending || virq->active;
	spin_unlock_intsave(&guest->vpic.lock, save);

	return ret;
}

int_ops_t vpic_ops = {
	.eoi = vpic_eoi,
	.enable = vpic_irq_unmask,
	.disable = vpic_irq_mask,
	.is_disabled = vpic_irq_is_disabled,
	.set_cpu_dest_mask = vpic_irq_set_destcpu,
	.get_cpu_dest_mask = vpic_irq_get_destcpu,
	.is_active = vpic_irq_is_active,
};

vpic_interrupt_t *vpic_alloc_irq(guest_t *guest, int config)
{
	register_t save;
	vpic_interrupt_t *virq = NULL;
	int irq;

	save = spin_lock_intsave(&guest->vpic.lock);

	if (guest->vpic.alloc_next < MAX_VINT_CNT) {
		irq = guest->vpic.alloc_next++;
		virq = &guest->vpic.ints[irq];

		virq->destcpu = 1;
		virq->irq.ops = &vpic_ops;
		virq->guest = guest;
		virq->irqnum = irq;
		virq->config = config;
	}

	spin_unlock_intsave(&guest->vpic.lock,save);
	return virq;
}

int vpic_alloc_handle(vpic_interrupt_t *vpic, uint32_t *intspec)
{
	int handle = vmpic_alloc_handle(vpic->guest, &vpic->irq,
	                                vpic->config);
	if (handle < 0)
		return handle;

	if (intspec) {
		intspec[0] = handle;
		intspec[1] = vpic->config;
	}

	return handle;
}
