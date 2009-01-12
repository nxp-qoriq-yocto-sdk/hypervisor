/** @file
 * I2C device emulation
 */
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
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

#include <libos/trap_booke.h>
#include <libos/mpic.h>
#include <libos/errors.h>
#include <percpu.h>
#include <guestmemio.h>
#include <devtree.h>
#include <vmpic.h>

#define MPC_I2C_SR	0x0c	// SR register
#define CSR_MIF  	0x02	// MIF bit in SR register

/**
 * i2c_callback - I2C virtualization trap callback function
 *
 * This function is registered via register_vf_handler()
 */
void i2c_callback(vf_range_t *vf, trapframe_t *regs, phys_addr_t paddr)
{
	uint32_t insn;
	void *vaddr;
	int ret;
	int store;
	unsigned int reg;

	// Get a hypervisor virtual address to this register
	vaddr = vf->vaddr + (paddr & 0xFF);

	// Get the actual instruction that caused the trap
	// This uses the external pid load instruction, which needs the EPLC
	// SPR set up first.
	guestmem_set_insn(regs);
	ret = guestmem_in32((uint32_t *)regs->srr0, &insn);
	if (ret != GUESTMEM_OK) {
		if (ret == GUESTMEM_TLBMISS)
			regs->exc = EXC_ITLB;
		else {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				 "%s: guestmem_in32() returned %d\n", __func__, ret);
			regs->exc = EXC_ISI;
		}
		reflect_trap(regs);
		return;
	}

	// We need to disable critical interrupts while emulating the
	// load/store instruction, otherwise another I2C interrupt might occur
	// before we call vpic_deassert_vint(), and then the
	// vpic_deassert_vint() will de-assert the second interrupt instead of
	// the first.

	register_t saved = disable_int_save();

	if (unlikely(emu_load_store(regs, insn, vaddr, &store, &reg))) {
		restore_int(saved);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PIL);
		reflect_trap(regs);
		return;
	}

	// We emulate the MIF bit of the SR register.  If the guest writes a
	// '0', that tells the I2C device to deassert its interrupt.  Since we
	// already did that in the I2C ISR, what we really want to do is
	// deassert our virtual I2C interrupt, and set our virtualized SR.MIF
	// register to 0.  If the guest is reading SR, then we logical-or our
	// virtual SR.MIF register.
	if ((uint8_t) paddr == MPC_I2C_SR) {
		guest_t *guest = get_gcpu()->guest;

		// store != 0 means it was a store instruction
		if (store && ((regs->gpregs[reg] & CSR_MIF) == 0)) {
			// The guest wants to clear SR.MIF
			guest->i2c_sr = 0;
			vpic_deassert_vint(vf->data);
		} else
			// We don't ever expect the real SR.MIF bit to be 1
			// when it should be 0, so there's no need to clear that
			// bit before the logical-or.
			regs->gpregs[reg] |= guest->i2c_sr;
	}

	restore_int(saved);

	regs->srr0 += 4;
}

/**
 * i2c_isr - interrupt handler for I2C devices
 * @arg - pointer to vf_range_t object
 *
 * The two I2C controllers on an I2C block share an interrupt, which makes
 * it difficult to assign one controller to one guest and the other
 * controller to another.  We handle this by creating virtual interrupts for
 * each node.  Then we "steal" the I2C's interrupt by registering our own
 * interrupt handler and making the I2C interrupt a critical one (standard
 * operation for the hypervisor).
 *
 * For each virtualized I2C device, we register an interrupt handler.  This
 * means that we typically have two ISRs for the same interrupt number.  The
 * difference is in the 'arg' parameter that's passed to the ISR.  Each
 * virtualized device has its own vf_range_t object that contains the virq
 * pointer for that guest.
 *
 * @return int
 */
int i2c_isr(void *arg)
{
	vf_range_t *vf = (vf_range_t *) arg;
	uint8_t *i2c_sr = vf->vaddr + MPC_I2C_SR;

	uint8_t sr = in8(i2c_sr);

	// Is this interrupt for us
	if (sr & 0x2) {
		vpic_interrupt_t *virq = vf->data;
		guest_t *guest = virq->guest;

		// Clear the interrupt so we don't get it again
		out8(i2c_sr, sr & ~CSR_MIF);

		// Emulate the SR.MIF bit that we just cleared.  The guest
		// thinks it's still set.
		guest->i2c_sr = CSR_MIF;

		// Assert our virtual interrupt to the guest.  Its interrupt
		// handler will probably attempt to read the SR register.
		vpic_assert_vint(virq);
	}

	return 0;
}

/**
 * virtualize_device_interrupt - virtualize the interrupt for a device
 * @node - node in guest device tree that should be virtualized
 * @handler - interrupt handler to call for hardware interrupts
 *
 * This function virtualizes the interrupt for a given device in a guest
 * device tree.  It does this by registering a private ISR for that device
 * (so that the hypervisor is called instead of the guest), and then
 * creating a virtual interrupt for the node.  When the device generates an
 * interrupt, the hypervisor ISR will clear the interrupt in the device and
 * assert the virtual interrupt.  The device register that handles the
 * interrupt pending status needs to be emulated.
 *
 * Returns 0 for success or a negative number for failure.
 */
int virtualize_device_interrupt(guest_t *guest, dt_node_t *node, vf_range_t *vf,
				int_handler_t handler)
{
	interrupt_t *irq = NULL;
	vpic_interrupt_t *virq;
	const uint32_t *prop;
	uint32_t irq_prop[2];
	int vmpic_handle;
	dt_node_t *controller;
	int ret;
	phys_addr_t paddr;
	int ncells;

	// Find out what the interrupt parent for this node is.  The return
	// value is the offset of the interrupt controller node, which should be
	// the vmpic node.  We also get the vmpic handle.
	controller = get_interrupt(guest->devtree, node, 0, &prop, &ncells);
	if (!controller) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "cannot get interrupt data for node %s\n", node->name);
		return ERR_BADTREE;
	}
	vmpic_handle = *prop;

	// Check to see if the vmpic initialization code has already processed
	// this node.  In other words, this node needs to have the vmpic as its
	// interrupt parent.
	if (!dt_node_is_compatible(controller, "fsl,hv-vmpic")) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "node %s must have the VMPIC as an interrupt parent\n",
			 node->name);
		return ERR_BADTREE;
	}

	// Get the pointer to the hardware interrupt object behind this vmpic
	// handle.
	vmpic_interrupt_t *vmpic = guest->handles[vmpic_handle]->intr;
	irq = vmpic->irq;

	// Allocate a software (virtual) irq.  This is what we use to send an
	// interrupt to the guest when the I2C device sends us an interrupt.
	// Make it level-sensitive, like the real I2C.
	virq = vpic_alloc_irq(guest, IRQ_LEVEL);
	if (!virq)
		return ERR_NOMEM;

	// Now we need a guest handle for this virq.
	ret = vpic_alloc_handle(virq, irq_prop);
	if (ret < 0)
		return ret;

	// Overwrite the 'interrupts' property with our new virq handle.
	// The previous handle might be lost forever, but that's okay.
	ret = dt_set_prop(node, "interrupts", irq_prop, sizeof(irq_prop));
	if (ret < 0) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "cannot set interrupts property for node %s\n",
			 node->name);
		return ret;
	}

	// Configure the hardware interrupt so that it points to us
	irq->ops->set_priority(irq, 15);
	irq->ops->set_config(irq, irq_prop[1]);

	// Get the guest physical address for this device.  We assume that
	// guest physical is equal to real physical.  We need this to find the
	// vf_range_t object for this virq.
	ret = dt_get_reg(node, 0, &paddr, NULL);

	// Save our virq has client-specific data.
	vf->data = virq;

	// Register our ISR.
	ret = irq->ops->register_irq(irq, handler, vf);
	if (ret < 0)
		// Out of memory
		return ret;

	return 0;
}

typedef struct count_devices_ctx {
	phys_addr_t paddr;
	int count;
} count_devices_ctx_t;

static int count_devices_callback(dt_node_t *node, void *arg)
{
	count_devices_ctx_t *ctx = arg;
	phys_addr_t paddr;

	if (node->parent && node->parent->parent &&
	    !dt_node_is_compatible(node->parent, "simple-bus"))
		return 0;

	// Get the guest physical address for this device
	int ret = dt_get_reg(node, 0, &paddr, NULL);
	if (ret < 0)
		return 0;

	paddr &= ~(PAGE_SIZE - 1);

	if (paddr == ctx->paddr)
		ctx->count++;

	return 0;
}

/**
 * Determine if there's another device sharing this page
 *
 * This function scans the device tree to determine if there are other
 * devices that shares the same memory page as this one.  It returns a count
 * of these devices.
 *
 * We assume that if there are any two devices that share the same page,
 * the registers of all such devices completely fit within one page.  This
 * eliminates the need to be given a 'size' parameter.
 */
static int count_devices_in_page(dt_node_t *tree, phys_addr_t paddr)
{
	count_devices_ctx_t ctx = {
		// Round down to the nearest page
		.paddr = paddr & ~(PAGE_SIZE - 1)
	};

	dt_for_each_node(tree, &ctx, count_devices_callback, NULL);

	return ctx.count;
}

/**
 * process_i2c_node - if it's an I2C node, virtualize it
 *
 * If the given node is an I2C node, then virtualize its registers and
 * interrupt.
 *
 * We only virtualize an I2C node if it's the only I2C node in that memory
 * page.  If both I2C nodes for that page are present in the guest device
 * tree, then there's no point in virtualizing it.
 *
 * Returns:
 * 1) negative number if error,
 * 2) 0 if not an I2C node, or it is an I2C node but we don't want to
 * virtualize it
 * 3) 1 if it's an I2C node and it was virtualized.
 */
int virtualize_i2c_node(guest_t *guest, dt_node_t *node, phys_addr_t paddr,
			phys_addr_t size)
{
	vf_range_t *vf;
	int ret;

	// Check to see if this is an I2C node
	if (!dt_node_is_compatible(node, "fsl-i2c"))
		return 0;

	// We know that I2C devices come in pairs, so if we _don't_ find another
	// device on this page, then we know that we should restrict access in
	// this page.
	if (count_devices_in_page(guest->devtree, paddr) != 1)
		return 0;

	vf = register_vf_handler(guest, paddr, paddr + size - 1, i2c_callback);
	if (!vf)
		return ERR_NOMEM;

	ret = virtualize_device_interrupt(guest, node, vf, i2c_isr);
	if (ret < 0)
		return ret;

	// We virtualized it
	return 1;
}

