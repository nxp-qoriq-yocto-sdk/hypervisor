#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/8578.h>
#include <libos/mpic.h>
#include <libfdt.h>
#include <vpic.h>

#include <hv.h>
#include <vmpic.h>
#include <percpu.h>
#include <devtree.h>

static void vmpic_reset(handle_t *h)
{
	guest_t *guest = get_gcpu()->guest;
	h->intr->ops->irq_mask(h->intr->irq);

	if (h->intr->ops->set_priority)
		h->intr->ops->set_priority(h->intr->irq, 0);
	if (h->intr->ops->set_cpu_dest)
		h->intr->ops->set_cpu_dest(h->intr->irq,
		                               1 << (guest->gcpus[0]->cpu->coreid));
	/* FIXME: remember initial polarity from devtree? */
	if (h->intr->ops->set_polarity)
		h->intr->ops->set_polarity(h->intr->irq, 0);
	if (h->intr->ops->irq_set_inttype)
		h->intr->ops->irq_set_inttype(h->intr->irq, TYPE_NORM);
}

void vmpic_irq_set_destcpu_wrapper(int irq, uint8_t log_destcpu_mask)
{
	guest_t *guest = get_gcpu()->guest;
	uint8_t cpu_dest = count_lsb_zeroes(log_destcpu_mask);

	mpic_irq_set_destcpu(irq, guest->gcpus[cpu_dest]->cpu->coreid);
}

uint8_t vmpic_irq_get_destcpu_wrapper(int irq)
{
	guest_t *guest = get_gcpu()->guest;
	int i;
	uint8_t coreid = count_lsb_zeroes(mpic_irq_get_destcpu(irq));

	for (i=0; i < guest->cpucnt; i++)
		if (guest->gcpus[i]->cpu->coreid == coreid)
			return (1 << i);

	return 0;
}

pic_ops_t mpic_ops = {
	mpic_irq_set_priority,
	vmpic_irq_set_destcpu_wrapper,
	mpic_irq_set_polarity,
	mpic_irq_mask,
	mpic_irq_unmask,
	mpic_irq_set_inttype,
	mpic_irq_set_ctpr,
	mpic_irq_get_ctpr,
	mpic_eoi,
	mpic_irq_get_priority,
	mpic_irq_get_polarity,
	vmpic_irq_get_destcpu_wrapper,
	mpic_irq_get_mask,
	mpic_irq_get_activity
};

pic_ops_t vpic_ops = {
	0,
	vpic_irq_set_destcpu,
	0,  /* set polarity */
	vpic_irq_mask,
	vpic_irq_unmask,
	0,   /* set int type */
	0,   /* set ctpr */
	0,   /* get ctpr */
	vpic_eoi
};

handle_ops_t vmpic_handle_ops = {
	.reset = vmpic_reset,
};

int vmpic_alloc_vpic_handle(guest_t *guest)
{
	interrupt_t *interrupt;
	int handle;

	interrupt  = alloc(sizeof(interrupt_t), __alignof__(interrupt_t));
	if (!interrupt)
		return -1;

	interrupt->user.intr = interrupt;
	interrupt->user.ops = &vmpic_handle_ops;

	handle = alloc_guest_handle(guest, &interrupt->user);

	interrupt->ops = &vpic_ops;
	interrupt->irq = vpic_alloc_irq(guest);

	/*
	 * update vpic vector to return guest handle directly
	 * during interrupt acknowledge
	 */
	vpic_irq_set_vector(interrupt->irq, handle);

	printf("vpic handle allocated: vmpic handle %d, vpic irq %d\n", handle, interrupt->irq);

	return handle;
}


void vmpic_global_init(void)
{
}

int vmpic_alloc_mpic_handle(guest_t *guest, int irq)
{
	interrupt_t *interrupt;
	int handle, vector;

	// FIXME: we should probably range check the irq here 

	/*
	 * Handle shared interrupts, check if handle already allocated.
	 * Currently, the trick to find this is to get the MPIC vector
	 * and see if this MPIC vector is a valid guest handle & it's
	 * type is an interrupt type, then simply resuse it.
	 */

	/*
	 * FIXME : races between hype instances allocating handle for
	 * shared interrupts
	 */

	vector = mpic_irq_get_vector(irq);
	if (vector < MAX_HANDLES && guest->handles[vector]) {
		interrupt = guest->handles[vector]->intr;
		if (interrupt &&
		    interrupt->ops == &mpic_ops && interrupt->irq == irq) {
			printf("vmpic reusing shared ghandle %d\n", vector);
			return vector;
		}
	}

	interrupt  = alloc(sizeof(interrupt_t), __alignof__(interrupt_t));
	if (!interrupt)
		return -1;

	interrupt->user.intr = interrupt;

	handle = alloc_guest_handle(guest, &interrupt->user);

	interrupt->ops = &mpic_ops;
	interrupt->irq = irq;

	printf("vmpic allocated guest handle %d\n", handle);

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
	void *fdt = guest->devtree;
	int len;
	int intlen;
	int node;
	uint32_t vmpic_phandle;

	/* find the vmpic node */
	node = fdt_node_offset_by_compatible(fdt, -1, "fsl,hv-vmpic");
	if (node >= 0) {
		vmpic_phandle = fdt_get_phandle(fdt, node);
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
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		uint32_t *interrupts = fdt_getprop_w(fdt, node, "interrupts", &intlen);
		if (interrupts != NULL) {
			int intctrl = -1;
			intctrl = get_interrupt_controller(fdt, node);

			/* identify interrupt sources to transform */
			if (fdt_getprop(fdt, intctrl, "fsl,hv-interrupt-controller", &len)) {
				int i;

				/* allocate handle and update irq for each int specifier */
				for (i=0; i < (intlen / CELL_SIZE); i += 2) {  // FIXME-- get #cells from devtree
					int handle = vmpic_alloc_mpic_handle(guest, interrupts[i]);
					if (handle >= 0)
						interrupts[i] = handle;
						// FIXME config mpic level/sense here
					else
						break;  /* error occured */
				}

				/* set the interrupt parent to the vmpic */
				fdt_setprop(fdt, node, "interrupt-parent", &vmpic_phandle, sizeof(vmpic_phandle));
			}
		}
	}

	/* delete the real mpic node(s) so guests don't get confused */
	node = -1;
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		const int *prop = fdt_getprop(fdt, node, "fsl,hv-interrupt-controller", &len);
		if (prop)
			fdt_del_node(fdt, node);
	}
}

void fh_vmpic_set_int_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t config = regs->gpregs[4];
	uint8_t priority = regs->gpregs[5];
	uint8_t log_cpu_dest_mask = regs->gpregs[6];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (!log_cpu_dest_mask) {  /* mask must have a bit set */
		regs->gpregs[3] = -2;  /* bad parameter */
		return;
	}

	if (int_handle->ops->set_priority)
		int_handle->ops->set_priority(int_handle->irq, priority);
	if (int_handle->ops->set_cpu_dest)
		int_handle->ops->set_cpu_dest(int_handle->irq,
							log_cpu_dest_mask);
	if (int_handle->ops->set_polarity)
		int_handle->ops->set_polarity(int_handle->irq, config & 0x01);
	if (int_handle->ops->irq_set_inttype)
		int_handle->ops->irq_set_inttype(int_handle->irq, TYPE_NORM);
}

void fh_vmpic_get_int_config(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t priority=0, cpu_dest=0, config=0;

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (int_handle->ops->get_priority)
		priority = int_handle->ops->get_priority(int_handle->irq);
	if (int_handle->ops->get_cpu_dest)
		cpu_dest = int_handle->ops->get_cpu_dest(int_handle->irq);
	if (int_handle->ops->get_polarity)
		config = int_handle->ops->get_polarity(int_handle->irq);

	regs->gpregs[4] = config;
	regs->gpregs[5] = priority;
	regs->gpregs[6] = cpu_dest;
}

void fh_vmpic_set_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t mask = regs->gpregs[4];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (mask)
		int_handle->ops->irq_mask(int_handle->irq);
	else
		int_handle->ops->irq_unmask(int_handle->irq);
}

void fh_vmpic_get_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t mask=0;

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (int_handle->ops->irq_get_mask)
		mask = int_handle->ops->irq_get_mask(int_handle->irq);

	regs->gpregs[4] = mask;
}

void fh_vmpic_eoi(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	int_handle->ops->eoi();
}

void fh_vmpic_set_priority(trapframe_t *regs)
{
	uint8_t priority = regs->gpregs[3];

	mpic_irq_set_ctpr(priority);
}

void fh_vmpic_get_priority(trapframe_t *regs)
{
	int32_t current_ctpr = mpic_irq_get_ctpr();

	regs->gpregs[4] = current_ctpr;
}

void fh_vmpic_iack(trapframe_t *regs)
{
	uint16_t vector;

	vector = mpic_iack();
	if (vector == 0xFFFF) {  /* spurious */
		vector = vpic_iack();
	}

	regs->gpregs[4] = vector;
}

void fh_vmpic_get_activity(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t active=0;

	// FIXME: race against handle closure
	if (handle < 0 || handle >= MAX_HANDLES || !guest->handles[handle]) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	interrupt_t *int_handle = guest->handles[handle]->intr;
	if (!int_handle) {
		regs->gpregs[3] = -1;  /* bad handle */
		return;
	}

	if (int_handle->ops->irq_get_activity)
		active  = int_handle->ops->irq_get_activity(int_handle->irq);

	regs->gpregs[4] = active;
}
