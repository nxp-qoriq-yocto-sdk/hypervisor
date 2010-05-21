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
#include <libos/bitops.h>
#include <libos/mpic.h>
#include <libos/interrupts.h>
#include <libos/alloc.h>
#include <libos/hcall-errors.h>

#include <vpic.h>
#include <hv.h>
#include <vmpic.h>
#include <percpu.h>
#include <devtree.h>
#include <errors.h>

#define VMPIC_ADDR_CELLS 0
#define VMPIC_INTR_CELLS 2

static uint32_t vmpic_lock;

static void vmpic_reset(vmpic_interrupt_t *vmirq)
{
	interrupt_t *irq = vmirq->irq;

	interrupt_reset(irq);

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, 0);

	if (irq->ops == &vpic_ops)
		irq->ops->set_cpu_dest_mask(irq, 1);
	else if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq,
			1 << (vmirq->guest->gcpus[0]->cpu->coreid));

	if (irq->ops->set_config)
		irq->ops->set_config(irq, vmirq->config);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
}

static void vmpic_reset_handle(handle_t *h)
{
	if (vmpic_is_claimed(h->intr))
		vmpic_reset(h->intr);
}

static handle_ops_t vmpic_handle_ops = {
	.reset = vmpic_reset_handle,
};

vmpic_interrupt_t *vmpic_alloc_handle(guest_t *guest, interrupt_t *irq,
                                      int config, int standby)
{
	assert(!irq->priv || standby);

	vmpic_interrupt_t *vmirq = alloc_type(vmpic_interrupt_t);
	if (!vmirq) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return NULL;
	}

	vmirq->guest = guest;
	vmirq->irq = irq;

	vmirq->user.intr = vmirq;
	vmirq->user.ops = &vmpic_handle_ops;

	vmirq->config = config;

	if (!standby) {
		irq->priv = vmirq;
		vmpic_reset(vmirq);
		vmpic_set_claimed(vmirq, 1);
	}

	vmirq->handle = alloc_guest_handle(guest, &vmirq->user);
	if (vmirq->handle < 0) {
		free(vmirq);
		return NULL;
	}

	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
	         "vmpic: %p is handle %d in %s%s\n",
	         irq, vmirq->handle, guest->name,
	         standby ? " (standby)" : "");

	return vmirq;
}

void vmpic_global_init(void)
{
}

#ifdef CONFIG_CLAIMABLE_DEVICES
static int claim_int(claim_action_t *action, dev_owner_t *owner,
                     dev_owner_t *prev)
{
	vmpic_interrupt_t *vmirq = to_container(action, vmpic_interrupt_t,
	                                        claim_action);
	interrupt_t *irq = vmirq->irq;
	
	/* Arbitrary 100ms timeout for active bit to deassert */
	uint32_t timeout = dt_get_timebase_freq() / 10;
	uint32_t time;

	/* NOTE: This sequence is designed around what the MPIC
	 * expects.  It assumes that get_activity will return
	 * zero at some point after the interrupt is masked, and that
	 * it is OK to change the interrupt config at that point.
	 */

	/* Should be masked since partition must be stopped to have
	 * devices claimed away from it.
	 */
	assert(irq->ops->is_disabled(irq));

	/* Wait until active bit clears -- has most likely already
	 * happened.
	 */
	time = mfspr(SPR_TBL);
	while (irq->ops->is_active(irq)) {
		if (mfspr(SPR_TBL) - time > timeout) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "%s: %s: IRQ failed to become inactive\n",
			         __func__, owner->hwnode->name);
			return EV_EIO;
		}
	}

	/* FIXME: make this a libos callback */
	mpic_irq_set_vector(irq, vmirq->handle);
	vmpic_reset(vmirq);

	assert(vmpic_is_claimed(irq->priv));
	vmpic_set_claimed(irq->priv, 0);
	irq->priv = vmirq;
	smp_lwsync();
	vmpic_set_claimed(vmirq, 1);
	return 0;
}
#endif

int vmpic_alloc_mpic_handle(dev_owner_t *owner, interrupt_t *irq, int standby)
{
	guest_t *guest = owner->guest;
	vmpic_interrupt_t *vmirq;
	register_t saved;

	if (irq->actions) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: cannot share IRQ with the hypervisor\n",
		         __func__);
		return ERR_BUSY;
	}

	saved = spin_lock_intsave(&vmpic_lock);

	vmirq = irq->priv;
	if (vmirq && !standby) {
		assert(vmirq->irq == irq);

		if (vmirq->guest != guest) {
			spin_unlock_intsave(&vmpic_lock, saved);

			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "%s: cannot share IRQ\n",
			         __func__);

			return ERR_BUSY;
		}

	 	assert(vmirq->handle < MAX_HANDLES);
		assert(guest->handles[vmirq->handle]);
		assert(vmirq == guest->handles[vmirq->handle]->intr);

		spin_unlock_intsave(&vmpic_lock, saved);
		return vmirq->handle;
	}

	vmirq = vmpic_alloc_handle(guest, irq, irq->config, standby);
	if (!vmirq)
		goto out;

#ifdef CONFIG_CLAIMABLE_DEVICES
	vmirq->claim_action.claim = claim_int;
	vmirq->claim_action.next = owner->claim_actions;
	owner->claim_actions = &vmirq->claim_action;
#endif

	/*
	 * update mpic vector to return guest handle directly
	 * during interrupt acknowledge
	 *
	 * FIXME: don't assume MPIC, and handle cascaded IRQs
	 */
	if (!standby)
		mpic_irq_set_vector(irq, vmirq->handle);

out:
	spin_unlock_intsave(&vmpic_lock, saved);
	return vmirq->handle;
}

/**
 * @param[in] pointer to guest device tree
 *
 * This function fixes up mpic interrupt sources with
 * interrupt handles.
 *
 * TODO: support fix up of interrupt-maps
 *
 */
void vmpic_partition_init(guest_t *guest)
{
	dt_node_t *vmpic;
	uint32_t propdata;
#if 0
	vmpic_init_ctx_t ctx = {
		.guest = guest
	};
#endif

	/* find the vmpic node */
	vmpic = dt_get_subnode(guest->devices, "vmpic", 1);
	if (!vmpic)
		goto nomem;

	if (guest->mpic_direct_eoi) {
		char s[64];
		int len;

		len = snprintf(s, sizeof(s),
				"fsl,hv-mpic-per-cpu%cfsl,hv-vmpic", 0);
		if (dt_set_prop(vmpic, "compatible", s, len + 1) < 0)
			goto nomem;
	} else {
		if (dt_set_prop_string(vmpic, "compatible", "fsl,hv-vmpic") < 0)
			goto nomem;
	}

	propdata = 2;
	if (dt_set_prop(vmpic, "#interrupt-cells",
	                &propdata, sizeof(propdata)) < 0)
		goto nomem;

	propdata = 0;
	if (dt_set_prop(vmpic, "#address-cells",
	                &propdata, sizeof(propdata)) < 0)
		goto nomem;

	if (dt_set_prop(vmpic, "interrupt-controller", NULL, 0) < 0)
		goto nomem;

	/* FIXME: properly allocate a phandle */
	propdata = guest->vmpic_phandle = 0x564d5043;
	if (dt_set_prop(vmpic, "phandle",
	                &propdata, sizeof(propdata)) < 0)
		goto nomem;
	if (dt_set_prop(vmpic, "linux,phandle",
	                &propdata, sizeof(propdata)) < 0)
		goto nomem;

	if (guest->mpic_direct_eoi) {
		uint32_t pd[4];
		phys_addr_t addr;
		uint32_t naddr, nsize;

		dt_node_t *node = dt_get_first_compatible(hw_devtree,
							 "chrp,open-pic");
		if (!node) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
				"%s: no open-pic node in hw_devtree\n",
				__func__);
			return;
		}

		if (node->dev.regs)
			addr = node->dev.regs->start;
		else {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
				"%s: open-pic address not found\n",
				__func__);
			return;
		}

		if (get_addr_format(vmpic->parent, &naddr, &nsize) < 0) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
				"%s: Bad address format\n",
				__func__);
			return;
		}

		int i = 0;
		while (naddr)
			pd[i++] = (uint32_t)(addr >> (32 * --naddr));

		while (nsize) {
			pd[i++] = 0;
			nsize--;
		}

		pd[i - 1] = PAGE_SIZE;

		if (dt_set_prop(vmpic, "reg", &pd, i * 4) < 0)
			goto nomem;

		map_dev_range(guest, addr, (phys_addr_t)PAGE_SIZE);
	}

	return;

nomem:
	printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

void hcall_int_set_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int config = regs->gpregs[4];
	int priority = regs->gpregs[5];
	uint32_t lcpu_dest = regs->gpregs[6];
	uint32_t lcpu_mask = 1 << lcpu_dest;
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	/* destination CPU number must be valid */
	if (lcpu_dest >= guest->cpucnt) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}
	
	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	irq = vmirq->irq;

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, priority);

	if (irq->ops == &vpic_ops)
		irq->ops->set_cpu_dest_mask(irq, lcpu_mask);
	else if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq, 1 << guest->gcpus[lcpu_dest]->cpu->coreid);

	if (irq->ops->set_config)
		irq->ops->set_config(irq, config);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_get_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int priority = 0, config = IRQ_LEVEL;
	uint32_t lcpu_dest = 0;
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	irq = vmirq->irq;

	if (irq->ops->get_priority)
		priority = irq->ops->get_priority(irq);

	if (irq->ops == &vpic_ops) {
		uint32_t lcpu_mask = irq->ops->get_cpu_dest_mask(irq);
		lcpu_dest = count_lsb_zeroes(lcpu_mask);
	} else if (irq->ops->get_cpu_dest_mask) {
		uint32_t pcpu_mask = irq->ops->get_cpu_dest_mask(irq);
		unsigned int pcpu_dest = count_lsb_zeroes(pcpu_mask);
		unsigned int i;

		/* Map physical cpu to logical cpu within partition */
		for (i = 0; i < guest->cpucnt; i++)
			if (guest->gcpus[i]->cpu->coreid == pcpu_dest) {
				lcpu_dest = i;
				break;
			}
	}

	if (irq->ops->get_config)
		config = irq->ops->get_config(irq);

	regs->gpregs[4] = config;
	regs->gpregs[5] = priority;
	regs->gpregs[6] = lcpu_dest;
	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_set_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int mask = regs->gpregs[4];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	interrupt_t *irq = vmirq->irq;
	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vmpic %smask: %p %u\n",
	         mask ? "" : "un", irq, handle);

	/* For taking care of shared interrupts between partitions */
	register_t save = spin_lock_intsave(&vmpic_lock);
	if (mask != irq->oldmask) {
		irq->oldmask = mask;
		if (mask)
			interrupt_mask(irq);
		else
			interrupt_unmask(irq);
	}
	spin_unlock_intsave(&vmpic_lock, save);

	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_get_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	interrupt_t *irq = vmirq->irq;
	regs->gpregs[4] = irq->ops->is_disabled(irq);
	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_eoi(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	set_stat(bm_stat_vmpic_eoi, regs);

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	interrupt_t *irq = vmirq->irq;
	irq->ops->eoi(irq);
	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_iack(trapframe_t *regs)
{
	uint16_t vector;
	int v = 0;
	unsigned int handle = regs->gpregs[3];

	assert(handle == 0);

	if (mpic_coreint) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	vector = mpic_iack();
	if (vector == 0xFFFF) {  /* spurious */
		interrupt_t *irq = vpic_iack();
		if (irq) {
			vmpic_interrupt_t *vmirq = irq->priv;
			vector = vmirq->handle;
			v = 1;
		}
	}

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE,
	         "iack %x %s\n", vector, v ? "HW" : "Virt");
	regs->gpregs[4] = vector;
	regs->gpregs[3] = 0;  /* success */
}

void hcall_int_get_activity(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	interrupt_t *irq = vmirq->irq;
	regs->gpregs[4] = irq->ops->is_active(irq);
	regs->gpregs[3] = 0;  /* success */
}

void hcall_vmpic_get_msir(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	if (!vmpic_is_claimed(vmirq)) {
		regs->gpregs[3] = EV_INVALID_STATE;
		return;
	}

	interrupt_t *irq = vmirq->irq;
	regs->gpregs[4] = irq->ops->get_msir(irq);
	regs->gpregs[3] = 0;  /* success */
}
