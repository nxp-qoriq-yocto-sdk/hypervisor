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

#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/mpic.h>
#include <libos/interrupts.h>
#include <libos/alloc.h>

#include <vpic.h>
#include <hv.h>
#include <vmpic.h>
#include <percpu.h>
#include <devtree.h>
#include <errors.h>

#define VMPIC_ADDR_CELLS 0
#define VMPIC_INTR_CELLS 2

static uint32_t vmpic_lock;

static const uint8_t vmpic_intspec_to_config[4] = {
	IRQ_EDGE | IRQ_HIGH,
	IRQ_LEVEL | IRQ_LOW,
	IRQ_LEVEL | IRQ_HIGH,
	IRQ_EDGE | IRQ_LOW
};

static void vmpic_reset(vmpic_interrupt_t *vmirq)
{
	interrupt_t *irq = vmirq->irq;

	irq->ops->disable(irq);

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, 0);

	if (irq->ops == &vpic_ops)
		irq->ops->set_cpu_dest_mask(irq, 1);
	else if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq,
			1 << (vmirq->guest->gcpus[0]->cpu->coreid));

	if (irq->ops->set_config)
		irq->ops->set_config(irq, vmpic_intspec_to_config[vmirq->config]);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
}

static void vmpic_reset_handle(handle_t *h)
{
	vmpic_reset(h->intr);
}

static handle_ops_t vmpic_handle_ops = {
	.reset = vmpic_reset_handle,
};

int vmpic_alloc_handle(guest_t *guest, interrupt_t *irq, int config)
{
	assert(!irq->priv);

	vmpic_interrupt_t *vmirq = alloc_type(vmpic_interrupt_t);
	if (!vmirq) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return ERR_NOMEM;
	}

	vmirq->guest = guest;
	vmirq->irq = irq;
	irq->priv = vmirq;

	vmirq->user.intr = vmirq;
	vmirq->user.ops = &vmpic_handle_ops;

	vmirq->config = config;
	vmpic_reset(vmirq);

	vmirq->handle = alloc_guest_handle(guest, &vmirq->user);

	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
	         "vmpic: %p is handle %d in %s\n",
	         irq, vmirq->handle, guest->name);

	return vmirq->handle;
}

void vmpic_global_init(void)
{
}

int vmpic_alloc_mpic_handle(guest_t *guest, interrupt_t *irq)
{
	vmpic_interrupt_t *vmirq;
	register_t saved;
	int handle;

	if (irq->actions) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: cannot share IRQ with the hypervisor\n",
		         __func__);
		return ERR_BUSY;
	}

	saved = spin_lock_intsave(&vmpic_lock);

	vmirq = irq->priv;
	if (vmirq) {
		assert(vmirq->irq == irq);

		if (vmirq->guest != guest) {
			spin_unlock_intsave(&vmpic_lock, saved);

			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "%s: cannot share IRQ\n",
			         __func__);

			return ERR_BUSY;
		}

	 	handle = vmirq->handle;
	 	assert(handle < MAX_HANDLES);
		assert(guest->handles[handle]);
		assert(vmirq == guest->handles[handle]->intr);

		spin_unlock_intsave(&vmpic_lock, saved);
		return handle;
	}

	handle = vmpic_alloc_handle(guest, irq, irq->config);
	if (handle < 0)
		goto out;

	/*
	 * update mpic vector to return guest handle directly
	 * during interrupt acknowledge
	 *
	 * FIXME: don't assume MPIC, and handle cascaded IRQs
	 */
	mpic_irq_set_vector(irq, handle);

out:
	spin_unlock_intsave(&vmpic_lock, saved);
	return handle;
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

	if (dt_set_prop_string(vmpic, "compatible", "fsl,hv-vmpic") < 0)
		goto nomem;

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

//	dt_for_each_node(tree, &ctx, vmpic_init_one, NULL);
	return;

nomem:
	printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

void fh_vmpic_set_int_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int config = regs->gpregs[4];
	int priority = regs->gpregs[5];
	uint32_t lcpu_mask = regs->gpregs[6];
	int lcpu_dest = count_lsb_zeroes(lcpu_mask);
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	/* mask must have a valid bit set */
	if (!(lcpu_mask & ((1 << guest->cpucnt) - 1))) {
		regs->gpregs[3] = EINVAL;
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

void fh_vmpic_get_int_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int priority = 0, config = IRQ_LEVEL;
	uint32_t lcpu_mask = 1;
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	irq = vmirq->irq;

	if (irq->ops->get_priority)
		priority = irq->ops->get_priority(irq);

	if (irq->ops == &vpic_ops)
		lcpu_mask = irq->ops->get_cpu_dest_mask(irq);
	else if (irq->ops->get_cpu_dest_mask) {
		uint32_t pcpu_mask = irq->ops->get_cpu_dest_mask(irq);
		unsigned int pcpu_dest = count_lsb_zeroes(pcpu_mask);
		unsigned int i;

		/* Map physical cpu to logical cpu within partition */
		for (i = 0; i < guest->cpucnt; i++)
			if (guest->gcpus[i]->cpu->coreid == pcpu_dest)
				lcpu_mask = 1 << i;
	}

	if (irq->ops->get_config)
		config = irq->ops->get_config(irq);

	regs->gpregs[4] = config;
	regs->gpregs[5] = priority;
	regs->gpregs[6] = lcpu_mask;
	regs->gpregs[3] = 0;  /* success */
}

void fh_vmpic_set_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];
	int mask = regs->gpregs[4];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	printlog(LOGTYPE_IRQ, LOGLEVEL_VERBOSE, "vmpic %smask: %p %u\n",
	         mask ? "" : "un", vmirq->irq, handle);

	if (mask)
		vmirq->irq->ops->disable(vmirq->irq);
	else
		vmirq->irq->ops->enable(vmirq->irq);
	regs->gpregs[3] = 0;  /* success */
}

void fh_vmpic_get_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[4] = vmirq->irq->ops->is_disabled(vmirq->irq);
	regs->gpregs[3] = 0;  /* success */
}

void fh_vmpic_eoi(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmirq->irq->ops->eoi(vmirq->irq);
	regs->gpregs[3] = 0;  /* success */
}

void fh_vmpic_iack(trapframe_t *regs)
{
	uint16_t vector;
	int v = 0;

	if (mpic_coreint) {
		regs->gpregs[3] = FH_ERR_INVALID_STATE;
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

void fh_vmpic_get_activity(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	unsigned int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = EINVAL;
		return;
	}

	regs->gpregs[4] = vmirq->irq->ops->is_active(vmirq->irq);
	regs->gpregs[3] = 0;  /* success */
}
