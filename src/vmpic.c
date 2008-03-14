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

pic_ops_t mpic_ops = {
	mpic_irq_set_priority,
	mpic_irq_set_destcpu,
	mpic_irq_set_polarity,
	mpic_irq_mask,
	mpic_irq_unmask,
	mpic_irq_set_inttype,
	mpic_irq_set_ctpr,
	mpic_irq_get_ctpr,
	mpic_eoi
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


int vmpic_alloc_vpic_handle(guest_t *guest)
{
	interrupt_t *interrupt;
	int handle;

	interrupt  = alloc(sizeof(interrupt_t), __alignof__(interrupt_t));
	if (!interrupt)
		return -1;

	interrupt->user.int_handle = interrupt;

	handle = alloc_guest_handle(guest, &interrupt->user);

	interrupt->ops = &vpic_ops;
	interrupt->irq = vpic_alloc_irq(guest);

	printf("vpic handle allocated: vmpic handle %d, vpic irq %d\n", handle, interrupt->irq);

	return handle;
}


void vmpic_global_init(void)
{
}

int vmpic_alloc_mpic_handle(guest_t *guest, int irq)
{
	interrupt_t *interrupt;
	int handle;

	// FIXME: we should probably range check the irq here 

	interrupt  = alloc(sizeof(interrupt_t), __alignof__(interrupt_t));
	if (!interrupt)
		return -1;

	interrupt->user.int_handle = interrupt;

	handle = alloc_guest_handle(guest, &interrupt->user);

	interrupt->ops = &mpic_ops;
	interrupt->irq = irq;

	printf("vmpic allocated guest handle %d\n", handle);

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

	/* identify all "interrupts" properties that have
	 * compatible = fsl,mpc8578-mpic as an interrupt parent
	 *   -replace mpic as interrupt parent with vmpic
	 *   -for each irq #, alloc mpic handles for each & replace
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

				/* allocate handle and update irq */
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
	uint8_t cpu_dest = regs->gpregs[6];
	interrupt_t *int_handle = guest->handles[handle]->int_handle;

	int_handle->ops->ops_set_priority(int_handle->irq, priority);
	int_handle->ops->ops_set_cpu_dest(int_handle->irq, cpu_dest);
	int_handle->ops->ops_set_polarity(int_handle->irq, config & 0x01);

	int_handle->ops->ops_irq_set_inttype(int_handle->irq, TYPE_NORM);
}

void fh_vmpic_get_int_config(trapframe_t *regs)
{
}

void fh_vmpic_set_mask(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t mask = regs->gpregs[4];
	interrupt_t *int_handle = guest->handles[handle]->int_handle;

	if (mask)
		int_handle->ops->ops_irq_mask(int_handle->irq);
	else
		int_handle->ops->ops_irq_unmask(int_handle->irq);
}

void fh_vmpic_eoi(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	interrupt_t *int_handle = guest->handles[handle]->int_handle;

	int_handle->ops->ops_eoi(get_gcpu()->cpu->coreid);
}

void fh_vmpic_set_priority(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	uint8_t priority = regs->gpregs[4];
	interrupt_t *int_handle = guest->handles[handle]->int_handle;

	int_handle->ops->ops_set_ctpr(priority);
}

void fh_vmpic_get_priority(trapframe_t *regs)
{
	guest_t *guest = get_gcpu()->guest;
	uint32_t handle = regs->gpregs[3];
	int32_t current_ctpr;
	interrupt_t *int_handle = guest->handles[handle]->int_handle;

	current_ctpr = int_handle->ops->ops_get_ctpr();

	regs->gpregs[4] = current_ctpr;
}
