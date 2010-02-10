/** PCI-e controller virtualization
 *
 * @file
 *
 */

/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc.
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

static void pcie_callback(vf_range_t *vf, trapframe_t *regs, phys_addr_t paddr)
{
	uint32_t insn;
	void *vaddr;
	int ret;
	int store;
	unsigned int reg;

	// Get a hypervisor virtual address to this register
	vaddr = vf->vaddr + (paddr & 0x0FFF);

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

	register_t saved = disable_int_save();

	if (unlikely(emu_load_store(regs, insn, vaddr, &store, &reg))) {
		restore_int(saved);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PIL);
		reflect_trap(regs);
		return;
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

	vf = register_vf_handler(guest, paddr, size, gaddr, pcie_callback);
	if (!vf)
		return ERR_NOMEM;

	return 1;
}
