/** PCI-e controller virtualization
 *
 * @file
 *
 */

/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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
#include <libos/alloc.h>
#include <percpu.h>
#include <guestmemio.h>
#include <devtree.h>
#include <limits.h>

#define NUM_PEX_OUTBOUND_MEM_WINDOWS 5
#define PEXOTAR0_OFFSET 0xC00
#define PEXMSIITAR_OFFSET 0xD00
#define PEXOWBAR_REG_OFFSET 8
#define PEXOWAR_REG_OFFSET 0x10
#define PEXOWAR_OWS_MASK 0x3F
#define PEXOWAR_EN 0x80000000
#define PEX_OUTB_MEM_WINDOW_STRIDE 0x20
#define PEX_OUTB_MEM_WINDOW_SHIFT 5

typedef struct pcie_cntrl_priv {
	guest_t *guest;
	uint32_t gphys_owbar[4];
} pcie_cntrl_priv_t;

static void pcie_callback(vf_range_t *vf, trapframe_t *regs, phys_addr_t paddr)
{
	uint32_t insn;
	void *vaddr, *owin_vaddr;
	int ret;
	int store;
	unsigned int reg;
	pcie_cntrl_priv_t *priv = (pcie_cntrl_priv_t *)vf->data;
	uint32_t device_reg_range, device_reg, owar, gphys, phys,  npages;

	/* Get a hypervisor virtual address to this register */
	vaddr = vf->vaddr + (paddr & 0x0FFF);

	/* Get the actual instruction that caused the trap
	 * This uses the external pid load instruction, which needs the EPLC
	 * SPR set up first.
	 */
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

	register_t saved = disable_int_save();

	if (unlikely(emu_load_store(regs, insn, vaddr, &store, &reg))) {
		restore_int(saved);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PIL);
		reflect_trap(regs);
		return;
	}

	/*
	 * PEX controller ATMU(s) fixup from guest-phys to true-phys.
	 *
	 * We've written the guest programmed value out to
	 * the PCI controller.  Now we need to fix up the
	 * BAR to true physical.
	 *
	 * If the guest physical address is invalid, the outbound
	 * window is disabled.
	 *
	 * The callback is invoked for our device range, hence, we can
	 * directly refer to offsets in our device register space.
	 */

	device_reg_range = paddr & 0x0FFF;
	device_reg = device_reg_range & (PEX_OUTB_MEM_WINDOW_STRIDE - 1);

	if (device_reg_range >= PEXOTAR0_OFFSET &&
		device_reg_range < PEXMSIITAR_OFFSET &&
		(device_reg == PEXOWBAR_REG_OFFSET ||
		 device_reg == PEXOWAR_REG_OFFSET)) {

		int i = (device_reg_range & 0xFF) >> PEX_OUTB_MEM_WINDOW_SHIFT;

		if (store) {
			if (device_reg == PEXOWBAR_REG_OFFSET) {
				/* reg will have the guest physical */
				priv->gphys_owbar[i] = regs->gpregs[reg];
			}
			owin_vaddr = (void *)((uintptr_t) vaddr &
				~(PEX_OUTB_MEM_WINDOW_STRIDE - 1));
			owar = in32(owin_vaddr + PEXOWAR_REG_OFFSET);
			if (owar & PEXOWAR_EN) {
				owar &= PEXOWAR_OWS_MASK;
				/*
				 * output window size is 2^(OWS+1)
				 * byte size
				 */
				npages = 1 << (owar - 11);
				gphys = in32(owin_vaddr + PEXOWBAR_REG_OFFSET);
				phys = get_rpn(priv->guest, gphys, npages);

				if (phys == ULONG_MAX)
					out32(owin_vaddr + PEXOWAR_REG_OFFSET,
						~PEXOWAR_EN);

				/* Now, overwrite gphys w/t true-phys */
				out32(owin_vaddr + PEXOWBAR_REG_OFFSET, phys);

				printlog(LOGTYPE_DEV, LOGLEVEL_DEBUG,
					"%s: pex atmu %d gphys_owbar 0x%x xlat to phys_owbar 0x%x\n",
					__func__, i, gphys, phys);
			}
		} else {
			/* load, reg will have the destination register */
			if (device_reg == PEXOWBAR_REG_OFFSET) {
				regs->gpregs[reg] = priv->gphys_owbar[i];
			}
		}
	}

	restore_int(saved);

	regs->srr0 += 4;
}

static int virtualize_pcie_node(dev_owner_t *owner, dt_node_t *node);

static virtualizer_t __virtualized_device pcie = {
	.compatible = "fsl,p4080-pcie",
	.virtualize = virtualize_pcie_node,
};

int virtualize_pcie_node(dev_owner_t *owner, dt_node_t *node)
{
	dt_node_t *hwnode = owner->hwnode;
	guest_t *guest = owner->guest;
	vf_range_t *vf;
	uint32_t tmpaddr[MAX_ADDR_CELLS];
	int ret;
	phys_addr_t rangesize, size;
	phys_addr_t gaddr, paddr;
	dt_prop_t *prop;
	pcie_cntrl_priv_t *priv;
	int i;

	prop = dt_get_prop(owner->cfgnode, "atmu-guest-physical", 0);
	if (!prop)
		return ERR_UNHANDLED;

	dt_lookup_regs(hwnode);

	if (hwnode->dev.num_regs == 0)
		return ERR_BADTREE;

	ret = map_guest_ranges(owner);
	if (ret < 0)
		return ret;

	dt_lookup_irqs(hwnode);

	ret = map_guest_irqs(owner);
	if (ret < 0)
		return ret;

	ret = patch_guest_intmaps(owner);
	if (ret < 0)
		return ret;

	configure_dma(node, owner);

	gaddr = paddr = hwnode->dev.regs[0].start;
	size = hwnode->dev.regs[0].size;

	if (guest->devranges) {
		val_from_int(tmpaddr, paddr);

		ret = xlate_one(tmpaddr, guest->devranges->data,
			guest->devranges->len, 2, 2, 2, 2, &rangesize);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				"%s: 0x%llx does not translate through device-ranges\n", __func__, paddr);
			return ERR_BADTREE;
		}

		gaddr = ((phys_addr_t) tmpaddr[MAX_ADDR_CELLS - 2] << 32) |
				tmpaddr[MAX_ADDR_CELLS - 1];
	}

	priv = malloc(sizeof(pcie_cntrl_priv_t));
	if (!priv)
		return ERR_NOMEM;

	vf = register_vf_handler(guest, paddr, size, gaddr, pcie_callback,
				priv);
	if (!vf)
		return ERR_NOMEM;

	/*
	 * Initialize the PEX OUTBOUND ATMU BARs with u-boot values,
	 * which should be true-physical values
	 */
	for (i=0; i < NUM_PEX_OUTBOUND_MEM_WINDOWS; i++) {
		unsigned long grpn, attr;

		/*
		 * Make sure u-boot ATMU values are valid, i.e., mappable
		 * to guest physical
		 */
		priv->gphys_owbar[i] = in32(vf->vaddr + PEXOTAR0_OFFSET +
			PEXOWBAR_REG_OFFSET + i * PEX_OUTB_MEM_WINDOW_STRIDE);
		grpn = vptbl_xlate(guest->gphys_rev, priv->gphys_owbar[i],
					&attr, PTE_PHYS_LEVELS, 1);
		if (!grpn) {
			out32(vf->vaddr + PEXOTAR0_OFFSET +
				PEXOWAR_REG_OFFSET +
				i * PEX_OUTB_MEM_WINDOW_STRIDE, ~PEXOWAR_EN);
		}
	}
	priv->guest = guest;

	return 1;
}
