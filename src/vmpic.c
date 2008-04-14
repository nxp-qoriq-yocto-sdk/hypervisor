#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/8578.h>
#include <libos/mpic.h>
#include <libos/interrupts.h>

#include <libfdt.h>

#include <vpic.h>
#include <hv.h>
#include <vmpic.h>
#include <percpu.h>
#include <devtree.h>
#include <errors.h>

static uint32_t vmpic_lock;

static void vmpic_reset(handle_t *h)
{
	guest_t *guest = get_gcpu()->guest;
	interrupt_t *irq = h->intr->irq;

	irq->ops->disable(irq);

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, 0);
	if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq, 1 << (guest->gcpus[0]->cpu->coreid));
	/* FIXME: remember initial polarity from devtree? */
	if (irq->ops->set_config)
		irq->ops->set_config(irq, IRQ_LEVEL | IRQ_HIGH);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
}

handle_ops_t vmpic_handle_ops = {
	.reset = vmpic_reset,
};

int vmpic_alloc_handle(guest_t *guest, interrupt_t *irq)
{
	vmpic_interrupt_t *vmirq;

	vmirq = alloc_type(vmpic_interrupt_t);
	if (!vmirq)
		return -ERR_NOMEM;

	vmirq->irq = irq;
	irq->priv = vmirq;

	vmirq->user.intr = vmirq;
	vmirq->user.ops = &vmpic_handle_ops;

	return alloc_guest_handle(guest, &vmirq->user);
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
 		return -ERR_INVALID;

	saved = spin_lock_critsave(&vmpic_lock);
 	
 	handle = mpic_irq_get_vector(irq);
	if (handle < MAX_HANDLES && guest->handles[handle]) {
		vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
		if (vmirq && vmirq->irq == irq) {
			printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
			         "vmpic reusing shared ghandle %d\n", handle);
			return handle;
		}
	}

	spin_unlock_critsave(&vmpic_lock, saved);

	handle = vmpic_alloc_handle(guest, irq);
	if (handle < 0)
		return handle;

	printlog(LOGTYPE_IRQ, LOGLEVEL_DEBUG,
	         "vmpic allocated guest handle %d\n", handle);

	/*
	 * update mpic vector to return guest handle directly
	 * during interrupt acknowledge
	 */
	mpic_irq_set_vector(irq, handle);
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
	void *tree = guest->devtree;
	int len, ret;
	int node;
	int vmpic_phandle;

	/* find the vmpic node */
	node = fdt_node_offset_by_compatible(tree, -1, "fsl,hv-vmpic");
	if (node >= 0) {
		vmpic_phandle = fdt_get_phandle(tree, node);
	} else {
		printf("ERROR: no vmpic node found\n");
		return;
	}

	/* Identify all interrupt sources that have an interrupt
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
		static char buf[1024];

		fdt_get_path(guest->devtree, node, buf, sizeof(buf));
		
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
	int handle = regs->gpregs[3];
	int config = regs->gpregs[4];
	int priority = regs->gpregs[5];
	uint32_t lcpu_mask = regs->gpregs[6];
	int lcpu_dest = count_lsb_zeroes(lcpu_mask);
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (!lcpu_mask) {  /* mask must have a bit set */
		regs->gpregs[3] = -2;  /* bad parameter */
		return;
	}

	irq = vmirq->irq;

	if (irq->ops->set_priority)
		irq->ops->set_priority(irq, priority);
	if (irq->ops->set_cpu_dest_mask)
		irq->ops->set_cpu_dest_mask(irq, 1 << guest->gcpus[lcpu_dest]->cpu->coreid);
	if (irq->ops->set_config)
		irq->ops->set_config(irq, config);
	if (irq->ops->set_delivery_type)
		irq->ops->set_delivery_type(irq, TYPE_NORM);
}

void fh_vmpic_get_int_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];
	int priority = 0, config = IRQ_LEVEL;
	uint32_t lcpu_mask = 1;
	interrupt_t *irq;

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	irq = vmirq->irq;

	if (irq->ops->get_priority)
		priority = irq->ops->get_priority(irq);

	if (irq->ops->get_cpu_dest_mask) {
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
}

void fh_vmpic_set_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];
	int mask = regs->gpregs[4];
	
	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (mask)
		vmirq->irq->ops->disable(vmirq->irq);
	else
		vmirq->irq->ops->enable(vmirq->irq);
}

void fh_vmpic_get_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	regs->gpregs[4] = vmirq->irq->ops->is_enabled(vmirq->irq);
}

void fh_vmpic_eoi(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmirq->irq->ops->eoi(vmirq->irq);
}

void fh_vmpic_iack(trapframe_t *regs)
{
	uint16_t vector;

	vector = mpic_iack();
	if (vector == 0xFFFF) {  /* spurious */
		interrupt_t *irq = vpic_iack();
		if (irq) {
			vmpic_interrupt_t *vmirq = irq->priv;
			vector = vmirq->handle;
		}
	}

	regs->gpregs[4] = vector;
}

void fh_vmpic_get_activity(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	int handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	vmpic_interrupt_t *vmirq = guest->handles[handle]->intr;
	if (!vmirq) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	regs->gpregs[4] = vmirq->irq->ops->is_active(vmirq->irq);
}
