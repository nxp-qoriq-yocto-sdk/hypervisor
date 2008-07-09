
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
#include <libos/p4080.h>
#include <libos/mpic.h>
#include <libos/interrupts.h>

#include <libfdt.h>

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

 	irq = get_mpic_irq(irqspec, ncells);
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

static int calc_new_imaplen(void *tree, const uint32_t *imap, int imaplen, int cell_len)
{
	const uint32_t *prop, *addr, *intr;
	int len, par_len, new_addr_cell, new_intr_cell;
	int vmpic_parlen, node;
	int found = 0;
	int newmaplen = 0;

	vmpic_parlen = VMPIC_ADDR_CELLS +  VMPIC_INTR_CELLS + 1;

	while (imaplen > (cell_len + 1)) {
		imap += cell_len;
		imaplen -= cell_len;
		newmaplen += cell_len;
		node = fdt_node_offset_by_phandle(tree, *imap);
		if (node < 0) {
			printf("calc_new_imaplen : unable get phandle error %d\n", node);
			return -1;
		}
		addr = fdt_getprop(tree, node, "#address-cells", &len);
		intr = fdt_getprop(tree, node, "#interrupt-cells", &len);
		if (addr == NULL || intr == NULL) {
			printf("calc_new_imaplen : no address-cells and interrupt-cells property in  parent imap node error = %d \n", len);
			return -1;
		}
		new_addr_cell = *addr;
		new_intr_cell = *intr;
		par_len = new_addr_cell + new_intr_cell + 1;
		prop = fdt_getprop(tree, node,
					"fsl,hv-interrupt-controller", &len);
		if (prop) {
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

static uint32_t *create_new_imap(void *tree, const uint32_t *imap, int imaplen, int cell_len, int newmaplen, int vmpic_phandle, guest_t *guest)
{
	const uint32_t *prop, *addr, *intr;
	int len, par_len, new_addr_cell, new_intr_cell;
	int vmpic_addr_cell, vmpic_intr_cell, node;
	uint32_t *imap_ptr, *tmp_imap_ptr;

	if (newmaplen <= 0)
		return NULL;

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
		node = fdt_node_offset_by_phandle(tree, *imap);
		if (node < 0)
			return NULL;
		addr = fdt_getprop(tree, node, "#address-cells", &len);
		intr = fdt_getprop(tree, node, "#interrupt-cells", &len);
		if (addr == NULL || intr == NULL) {
			printf("no address-cells and interrupt-cells property in  parent imap node error = %d \n", len);
			return NULL;
		}
		new_addr_cell = *addr;
		new_intr_cell = *intr;
		par_len = new_addr_cell + new_intr_cell + 1;
		prop = fdt_getprop(tree, node,
					"fsl,hv-interrupt-controller", &len);
		if (prop) {
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
				printf("create_new_imap : unable to get a vmpic handle error %d\n", handle);
				return NULL;
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
		printf("create_new_imap : new interrupt-map corrupted new=%d,old=%d\n", newmaplen, imaplen);
		return NULL;
	}
	return imap_ptr;
}

static void patch_interrupt_map(void *tree, int node, int vmpic_node, int vmpic_phandle, guest_t *guest)
{
	const uint32_t *addr, *intr, *imap;
	uint32_t *map_ptr;
	int newmaplen = 0, imaplen;
	int ret, len, cell_len;

	imap = fdt_getprop(tree, node, "interrupt-map", &imaplen);
	if (imap == NULL)
		return;
	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG, "Found interrupt-map property in device tree node=%d\n", node);
	/* check for address-cells and interrupt-cells property */
	addr = fdt_getprop(tree, node, "#address-cells", &len);
	intr = fdt_getprop(tree, node, "#interrupt-cells", &len);
	if (addr == NULL || intr == NULL) {
		printf("no address-cells and interrupt-cells property in imap node error = %d\n", len);
		return;
	}
	cell_len = *addr + *intr;
	imaplen /= CELL_SIZE;

	addr = fdt_getprop(tree, vmpic_node, "#address-cells", &len);
	intr = fdt_getprop(tree, vmpic_node, "#interrupt-cells", &len);
	if (addr == NULL || intr == NULL) {
		printf("no address-cells and interrupt-cells property in vmpic node error =%d\n", len);
		return;
	}
	if ((*addr != VMPIC_ADDR_CELLS) || (*intr != VMPIC_INTR_CELLS)) {
		printf("illegal value for vmpic address cells or interrupt cells\n");
		return;
	}
	newmaplen = calc_new_imaplen(tree, imap, imaplen, cell_len);
	if (newmaplen < 0) {
		printf("Interrupt map not proper\n");
		return;
	}
	if (newmaplen == 0) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG, "Interrupt map doesn't have fsl,hv-interrupt-controller\n");
		return;
	}

	map_ptr = create_new_imap(tree, imap, imaplen, cell_len, newmaplen, vmpic_phandle, guest);
	if (map_ptr == NULL) {
		printf("New Interrupt map creation failed\n");
		return;
	}

	ret = fdt_setprop(tree, node, "interrupt-map",
				map_ptr, newmaplen * CELL_SIZE);
	if (ret < 0)
		printf("patch_interrupt_map: error %d setting interrupt-map\n", ret);
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
	void *tree = guest->devtree;
	int len, ret;
	int node, vmpic_node;
	int vmpic_phandle;

	/* find the vmpic node */
	vmpic_node = fdt_node_offset_by_compatible(tree, -1, "fsl,hv-vmpic");
	if (vmpic_node >= 0) {
		vmpic_phandle = fdt_get_phandle(tree, vmpic_node);
	} else {
		printf("ERROR: no vmpic node found\n");
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

	node = -1;
	while ((node = fdt_next_node(tree, node, NULL)) >= 0) {
		int i, intlen, domain;
		uint32_t *intspec;

		patch_interrupt_map(tree, node, vmpic_node, vmpic_phandle, guest);
		
		intspec = fdt_getprop_w(tree, node, "interrupts", &intlen);
		if (!intspec) {
			if (intlen != -FDT_ERR_NOTFOUND)
				printf("vmpic_partition_init: error %d getting interrupts\n",
				       intlen);

			continue;
		}
		
		domain = get_interrupt_domain(tree, node);
		if (domain < 0) {
			printf("vmpic_partition_init: error %d in get_interrupt_domain\n",
			       domain);
			continue;
		}
		
		/* identify interrupt sources to transform */
		if (!fdt_getprop(tree, domain, "fsl,hv-interrupt-controller", &len))
			continue;

		for (i = 0; i < intlen / 8; i++) {
			int handle;

			/* FIXME: support more than just MPIC */
			handle = vmpic_alloc_mpic_handle(guest, &intspec[i * 2], 2);
			if (handle < 0) {
				printf("vmpic_partition_init: error %d allocating handle\n",
				       handle);
				
				return;
			}
			
			intspec[i * 2] = handle;
		}
		
		/* set the interrupt parent to the vmpic */
		ret = fdt_setprop(tree, node, "interrupt-parent",
		                  &vmpic_phandle, sizeof(vmpic_phandle));
		if (ret < 0) {
			printf("vmpic_partition_init: error %d setting interrupts\n", ret);
			return;
		}
	}

	/* delete the real mpic node(s) so guests don't get confused */
	node = -1;
	while ((node = fdt_next_node(tree, node, NULL)) >= 0) {
		const int *prop = fdt_getprop(tree, node, "fsl,hv-interrupt-controller", &len);
		if (prop)
			fdt_del_node(tree, node);
	}
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

	regs->gpregs[4] = vmirq->irq->ops->is_enabled(vmirq->irq);
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
	         "cpu%d: iack %x %s\n", cpu->coreid, vector, v ? "HW" : "Virt");
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
