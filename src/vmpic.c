
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
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/mpic.h>
#include <libos/interrupts.h>

#include <vpic.h>
#include <hv.h>
#include <vmpic.h>
#include <percpu.h>
#include <devtree.h>
#include <errors.h>

#define VMPIC_ADDR_CELLS 0
#define VMPIC_INTR_CELLS 2

static uint32_t vmpic_lock;

static void vmpic_reset(interrupt_t *irq)
{
	guest_t *guest = get_gcpu()->guest;

	irq->ops->disable(irq);

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, 0);

	if (irq->ops == &vpic_ops)
		irq->ops->set_cpu_dest_mask(irq, 1);
	else if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq, 1 << (guest->gcpus[0]->cpu->coreid));

	/* FIXME: remember initial polarity from devtree? */
	if (irq->ops->set_config)
		irq->ops->set_config(irq, IRQ_LEVEL | IRQ_HIGH);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
}

static void vmpic_reset_handle(handle_t *h)
{
	vmpic_reset(h->intr->irq);
}

handle_ops_t vmpic_handle_ops = {
	.reset = vmpic_reset_handle,
};

int vmpic_alloc_handle(guest_t *guest, interrupt_t *irq)
{
	vmpic_interrupt_t *vmirq;

	vmirq = alloc_type(vmpic_interrupt_t);
	if (!vmirq)
		return ERR_NOMEM;

	vmirq->irq = irq;
	irq->priv = vmirq;

	vmirq->user.intr = vmirq;
	vmirq->user.ops = &vmpic_handle_ops;

	vmirq->handle = alloc_guest_handle(guest, &vmirq->user);

	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
	         "vmpic: %p is handle %d in %s\n",
	         irq, vmirq->handle, guest->name);

	return vmirq->handle;
}

void vmpic_global_init(void)
{
}

int vmpic_alloc_mpic_handle(guest_t *guest, const uint32_t *irqspec, int ncells)
{
	interrupt_t *irq;
	register_t saved;
	int handle;

	/*
	 * Handle shared interrupts, check if handle already allocated.
	 * Currently, the trick to find this is to get the MPIC vector
	 * and see if this MPIC vector is a valid guest handle & it's
	 * type is an interrupt type, then simply resuse it.
	 */
 	irq = NULL; //get_mpic_irq(irqspec, ncells);
 	if (!irq)
 		return ERR_INVALID;

	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
	         "vmpic: %p is MPIC %d\n",
	         irq, irqspec[0]);

	saved = spin_lock_critsave(&vmpic_lock);
 	
 	handle = mpic_irq_get_vector(irq);
	if (handle < MAX_HANDLES && guest->handles[handle]) {
		vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
		if (vmirq && vmirq->irq == irq) {
			spin_unlock_critsave(&vmpic_lock, saved);
			printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
			         "vmpic reusing shared ghandle %d\n", handle);
			return handle;
		}
	}

	vmpic_reset(irq);
	
	spin_unlock_critsave(&vmpic_lock, saved);

	handle = vmpic_alloc_handle(guest, irq);
	if (handle < 0)
		return handle;

	/*
	 * update mpic vector to return guest handle directly
	 * during interrupt acknowledge
	 */
	mpic_irq_set_vector(irq, handle);
	return handle;
}

static int calc_new_imaplen(dt_node_t *tree, const uint32_t *imap, int imaplen, int cell_len)
{
	uint32_t par_len, new_addr_cell, new_intr_cell;
	int ret;
	dt_node_t *node;
	int vmpic_parlen;
	int found = 0;
	int newmaplen = 0;

	vmpic_parlen = VMPIC_ADDR_CELLS +  VMPIC_INTR_CELLS + 1;

	while (imaplen > (cell_len + 1)) {
		imap += cell_len;
		imaplen -= cell_len;
		newmaplen += cell_len;
		node = dt_lookup_phandle(tree, *imap);
		if (!node) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "calc_new_imaplen: bad phandle %#x\n", *imap);
			return -1;
		}

		ret = dt_get_int_format(node, &new_intr_cell, &new_addr_cell);
		if (ret < 0)
			return ret;

		par_len = new_addr_cell + new_intr_cell + 1;
		if (dt_get_prop(node, "fsl,hv-interrupt-controller", 0)) {
			found = 1;
			newmaplen += vmpic_parlen;
		} else {
			newmaplen += par_len;
		}
		imap += par_len;
		imaplen -= par_len;
	}
	if (!found)
		newmaplen = 0;
	return newmaplen;
}

static uint32_t *create_new_imap(dt_node_t *tree, const uint32_t *imap, int imaplen, int cell_len, int newmaplen, int vmpic_phandle, guest_t *guest)
{
	dt_node_t *node;
	uint32_t par_len, new_addr_cell, new_intr_cell;
	uint32_t vmpic_addr_cell, vmpic_intr_cell;
	uint32_t *imap_ptr, *tmp_imap_ptr;
	int ret;

	imap_ptr = malloc(newmaplen * CELL_SIZE);
	if (imap_ptr == NULL)
		return NULL;

	tmp_imap_ptr = imap_ptr;

	vmpic_addr_cell = VMPIC_ADDR_CELLS;
	vmpic_intr_cell = VMPIC_INTR_CELLS;

	while (newmaplen > (cell_len + 1) && imaplen > (cell_len + 1)) {
		memcpy(tmp_imap_ptr, imap, cell_len * CELL_SIZE);
		tmp_imap_ptr += cell_len;
		newmaplen -= cell_len;
		imap += cell_len;
		imaplen -= cell_len;

	 	node = dt_lookup_phandle(tree, *imap);
		if (!node)
			goto err;

		ret = dt_get_int_format(node, &new_intr_cell, &new_addr_cell);
		if (ret < 0)
			goto err;

		par_len = new_addr_cell + new_intr_cell + 1;
		if (dt_get_prop(node, "fsl,hv-interrupt-controller", 0)) {
			*tmp_imap_ptr = vmpic_phandle;
			printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG, "patched the interrupt controller in intr-map\n");
			++tmp_imap_ptr;
			++imap;
			imap += new_addr_cell;
			tmp_imap_ptr += vmpic_addr_cell;
			newmaplen -= vmpic_addr_cell + 1;
			imaplen -= new_addr_cell + 1;
			int handle = vmpic_alloc_mpic_handle(guest, imap, new_intr_cell);
			if (handle >= 0) {
				printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG, "patched the interrupt value %d in intr-map with %d\n", *imap, handle);
				/* Level, Sense information would take up one cell */
				tmp_imap_ptr[0] = handle;
				/* FIXME: support more than just MPIC */
				if (new_intr_cell > 1)
					tmp_imap_ptr[1] = imap[1];
			} else {
				printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
				         "create_new_imap : unable to get a vmpic handle error %d\n", handle);
				goto err;
			}
			tmp_imap_ptr += vmpic_intr_cell;
			newmaplen -= vmpic_intr_cell;
			imap += new_intr_cell;
			imaplen -= new_intr_cell;
		} else {
			memcpy(tmp_imap_ptr, imap, par_len * CELL_SIZE);
			tmp_imap_ptr += par_len;
			newmaplen -= par_len;
			imap += par_len;
			imaplen -= par_len;
		}
	}
	if ((newmaplen != 0) || (imaplen != 0)) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "create_new_imap : new interrupt-map corrupted new=%d,old=%d\n", newmaplen, imaplen);
		goto err;
	}
	return imap_ptr;

err:
	free(imap_ptr);
	return NULL; 
}

static void patch_interrupt_map(guest_t *guest, dt_node_t *node,
                                dt_node_t *vmpic, int vmpic_phandle)
{
	dt_prop_t *addr, *intr, *imap;
	uint32_t *map_ptr;
	int newmaplen;
	int ret, cell_len;

	imap = dt_get_prop(node, "interrupt-map", 0);
	if (!imap)
		return;

	/* check for address-cells and interrupt-cells property */
	addr = dt_get_prop(node, "#address-cells", 0);
	intr = dt_get_prop(node, "#interrupt-cells", 0);
	if (addr == NULL || intr == NULL) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "patch_interrupt_map: %s missing #address-cells or "
		         "#interrupt-cells\n", node->name);
		return;
	}

	cell_len = *(const uint32_t *)addr->data + *(const uint32_t *)intr->data;

	addr = dt_get_prop(vmpic, "#address-cells", 0);
	intr = dt_get_prop(vmpic, "#interrupt-cells", 0);
	if (addr == NULL || intr == NULL) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "patch_interrupt_map: %s missing #address-cells or "
		         "#interrupt-cells\n", vmpic->name);
		return;
	}

	if ((*(const uint32_t *)addr->data != VMPIC_ADDR_CELLS) ||
	    (*(const uint32_t *)intr->data != VMPIC_INTR_CELLS)) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "patch_interrupt_map: illegal value for vmpic address "
		         "cells or interrupt cells\n");
		return;
	}

	newmaplen = calc_new_imaplen(guest->devtree, imap->data,
	                             imap->len / CELL_SIZE, cell_len);
	if (newmaplen < 0) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "Interrupt map not proper\n");
		return;
	}
	if (newmaplen == 0) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG, "Interrupt map doesn't have fsl,hv-interrupt-controller\n");
		return;
	}

	map_ptr = create_new_imap(guest->devtree, imap->data, imap->len / CELL_SIZE,
	                          cell_len, newmaplen, vmpic_phandle, guest);
	if (map_ptr == NULL) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "New Interrupt map creation failed\n");
		return;
	}

	ret = dt_set_prop(node, "interrupt-map", map_ptr, newmaplen * CELL_SIZE);
	if (ret < 0)
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "patch_interrupt_map: error %d setting interrupt-map\n", ret);
}

typedef struct vmpic_init_ctx {
	guest_t *guest;
	dt_node_t *vmpic;
	uint32_t vmpic_phandle;
} vmpic_init_ctx_t;

static int vmpic_init_one(dt_node_t *node, void *arg)
{
	vmpic_init_ctx_t *ctx = arg;

	int i, intlen, ret;
	dt_node_t *domain;
	uint32_t *intspec;
	dt_prop_t *prop;

	patch_interrupt_map(ctx->guest, node, ctx->vmpic, ctx->vmpic_phandle);
	
	prop = dt_get_prop(node, "interrupts", 0);
	if (!prop)
		return 0;
	intspec = prop->data;
	intlen = prop->len;

	domain = get_interrupt_domain(ctx->guest->devtree, node);
	if (!domain) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "vmpic_partition_init: no interrupt domain\n");
		return 0;
	}
	
	/* identify interrupt sources to transform */
	if (!dt_get_prop(domain, "fsl,hv-interrupt-controller", 0))
		return 0;

	for (i = 0; i < intlen / 8; i++) {
		int handle;

		/* FIXME: support more than just MPIC */
		handle = vmpic_alloc_mpic_handle(ctx->guest, &intspec[i * 2], 2);
		if (handle < 0) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "vmpic_partition_init: error %d allocating handle\n",
			         handle);
			
			return handle == ERR_NOMEM ? ERR_NOMEM : 0;
		}
		
		intspec[i * 2] = handle;
	}
	
	/* set the interrupt parent to the vmpic */
	ret = dt_set_prop(node, "interrupt-parent", &ctx->vmpic_phandle, 4);
	if (ret < 0) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "vmpic_partition_init: error %d setting interrupts\n", ret);
		return ret;
	}

	return 0;
}

static int vmpic_delete_ic(dt_node_t *node, void *arg)
{
	if (dt_get_prop(node, "fsl,hv-interrupt-controller", 0))
		dt_delete_node(node);

	return 0;
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
	vmpic_init_ctx_t ctx = {
		.guest = guest
	};

	dt_node_t *tree = guest->devtree;

	/* find the vmpic node */
	ctx.vmpic = dt_get_first_compatible(tree, "fsl,hv-vmpic");
	if (!ctx.vmpic) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "vmpic_partition_init: no vmpic node found\n");
		return;
	}

	ctx.vmpic_phandle = dt_get_phandle(ctx.vmpic);
	if (!ctx.vmpic_phandle) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "vmpic_partition_init: vmpic has no phandle\n");
		return;
	}

	/* Identify all interrupt nexus nodes and subsequently patch the
	 * interrupt-parent having the "fsl,hv-interrupt-controller"
	 * property.
	 * Identify all interrupt sources that have an interrupt
	 * parent with a "fsl,hv-interrupt-controller" property.  
	 * This indicates that the source is managed by the
	 * vmpic and needs to have a handle allocated.
	 *   -replace mpic as interrupt parent with vmpic
	 *   -for each irq #, alloc mpic handles for each & replace
	 *    property value
	 */

	dt_for_each_node(tree, &ctx, vmpic_init_one, NULL);

	/* delete the real mpic node(s) so guests don't get confused */
	dt_for_each_node(tree, NULL, NULL, vmpic_delete_ic);
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
		int pcpu_dest = count_lsb_zeroes(pcpu_mask);
		int i;

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
