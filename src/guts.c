/** @file
 * Global Utilities driver code
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

#include <libos/printlog.h>
#include <libos/io.h>
#include <libos/cpu_caps.h>
#include <libos/trap_booke.h>

#include <guts.h>
#include <devtree.h>
#include <errors.h>
#include <percpu.h>
#include <guestmemio.h>

static uint32_t *guts_base; /* base address of guts registers */
static uint32_t *guts_rstcr;  /* reset control register */
static uint32_t *guts_crstr0;  /* core reset status register */
static uint32_t *guts_rstrqmr;  /* reset request mask register */
static uint32_t *guts_tp_cluster; /* topology registers */
static uint32_t *guts_tp_init; /* topology registers */

static int guts_devconfig_probe(device_t *dev, const dev_compat_t *compat_id);

static const dev_compat_t guts_devconfig_compats[] = {
	{
		.compatible = "fsl,qoriq-device-config-1.0"
	},
	{
		.compatible = "fsl,qoriq-device-config-2.0"
	},
	{}
};

static driver_t __driver guts_devconfig = {
	.compatibles = guts_devconfig_compats,
	.probe = guts_devconfig_probe
};

static int guts_devconfig_probe(device_t *dev, const dev_compat_t *compat_id)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"guts device config initialization failed\n");
		return ERR_INVALID;
	}

	guts_base = dev->regs[0].virt;
	guts_rstcr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_RSTCR);
	guts_crstr0 = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_CRSTR0);
	guts_rstrqmr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + GUTS_RSTRQMR);
	guts_tp_cluster = (uint32_t *)((uintptr_t)dev->regs[0].virt + GUTS_TP_CLUSTER);
	guts_tp_init = (uint32_t *)((uintptr_t)dev->regs[0].virt + GUTS_TP_INIT);

	return 0;
}

int get_cluster_for_cpu_id(int cpu_id)
{
	int cpu_count = 0, cluster_id = 0;
	uint32_t cluster;

	do {
		cluster = in32(guts_tp_cluster + 2 * cluster_id);
		for (int i = 0; i < TP_ITYPE_PER_CLUSTER; i++) {
			uint32_t index = (cluster >> (i * 8)) & TP_CLUSTER_ITYPE_MASK;
			uint32_t type = in32(guts_tp_init + index);
			if (type & TP_IDX_CPU) {
				if (cpu_id / cpu_caps.threads_per_core == cpu_count)
					return cluster_id;
				cpu_count++;
			}

		}
		cluster_id++;
	} while (!(cluster & TP_CLUSTER_EOC));

	return -1;
}

/**
 * returns the system hardware reset status
 */
int get_sys_reset_status(void)
{
	if (!guts_crstr0) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
		        "%s: reset status reg not found\n", __func__);
		return -EV_ENODEV;
	}

	uint32_t val = in32(guts_crstr0);

	// FIXME: simics always returns 0
	// for CRSTR0, so for now consider
	// that POR
	if (val & CRSTR0_RST_PORST || val == 0) {
		return SYS_RESET_STATUS_POR;
	} else if (val & CRSTR0_RST_HRST) {
		return SYS_RESET_STATUS_HARD;
	} else {
		return -EV_EIO;
	}
}

int system_reset(void) __attribute__ ((weak));
int system_reset(void)
{
	if (!guts_rstcr) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
		        "%s: reset control reg not found\n", __func__);
		return EV_ENODEV;
	}

	out32(guts_rstcr, RSTCR_RESET_REQ);

	// FIXME: timeout here if the reset doesn't happen
	// and return an error
	while (1);
}

int set_reset_mask(uint32_t mask)
{
	uint32_t val;

	if (!guts_rstrqmr) {
		printlog(LOGTYPE_GUTS, LOGLEVEL_ERROR,
			 "%s: reset request mask reg not found\n", __func__);
		return -EV_ENODEV;
	}

	val = in32(guts_rstrqmr);

	out32(guts_rstrqmr, val | mask);

	return 0;
}

struct dt_node *get_guts_node(void)
{
	const dev_compat_t *dev_compat;
	dt_node_t *guts_node = NULL;

	for (dev_compat = guts_devconfig_compats; dev_compat->compatible; dev_compat++) {
		guts_node = dt_get_first_compatible(hw_devtree, dev_compat->compatible);
		if (guts_node)
			break;
	}

	return guts_node;
}

static void guts_callback(vf_range_t *vf, trapframe_t *regs, phys_addr_t paddr)
{
	void *vaddr;
	uint32_t insn;
	int ret;
	int store;
	unsigned int reg, ofs;

	vaddr = vf->vaddr + (paddr & 0x0FFF);

	guestmem_set_insn(regs);
	ret = guestmem_in32((uint32_t *)regs->srr0, &insn);
	if (ret != GUESTMEM_OK) {
		if (ret == GUESTMEM_TLBMISS) {
			regs->exc = EXC_ITLB;
		} else {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				 "%s: guestmem_in32() returned %d\n",
				 __func__, ret);
			regs->exc = EXC_ISI;
		}
		reflect_trap(regs);
		return;
	}

	register_t saved = disable_int_save();

	ret = emu_load_store(regs, insn, vaddr, &store, &reg);
	if (unlikely(ret)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "%s: emu_load_store() returned %d\n", __func__, ret);

		restore_int(saved);
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PIL);
		reflect_trap(regs);
		return;
	}

	ofs = paddr - GUTS_PHYS_BASE;

	if (!store) {
		if (ofs == GUTS_SVR) {
			regs->gpregs[reg] = guts_base[ofs / 4];
			goto done;
		} else if (ofs >= GUTS_RCWSR_BASE &&
			   ofs < (GUTS_RCWSR_BASE + GUTS_RCWSR_CNT * 4)) {
			regs->gpregs[reg] = guts_base[ofs / 4];
			goto done;
		}
	}
	printlog(LOGTYPE_EMU, LOGLEVEL_WARN,
		 "%s@0x%lx (lr: 0x%lx): unsupported %s of reg %d, offset 0x%x\n",
		 __func__, regs->srr0, regs->lr, store ? "store" : "load",
		 ofs / 4, ofs);
done:
	restore_int(saved);
	regs->srr0 += 4;
}

void virtualized_guts_init(guest_t *guest)
{
	dt_node_t *guts, *node;
	dt_prop_t *prop;
	static const uint32_t guts_reg[] = {
		GUTS_PHYS_BASE >> 32, GUTS_PHYS_BASE & 0xffffffff,
		0x0, GUTS_PHYS_SIZE
	};

	guts = dt_get_subnode(guest->devices, "guts", 1);
	if (!guts)
		goto fail;

	/* copy compatible prop value from the hardware device tree */
	node = get_guts_node();
	if (node) {
		prop = dt_get_prop(node, "compatible", 0);
		if (prop) {
			if (dt_set_prop(guts, "compatible", prop->data, prop->len))
				goto fail;
		} else {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				 "%s: failed to get guts compatible\n", __func__);
			goto fail;
		}
	} else {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "%s: guts node not found\n", __func__);
		goto fail;
	}
	if (dt_set_prop(guts, "reg", guts_reg, sizeof(guts_reg)) < 0)
		goto fail;

	if (!register_vf_handler(guest, GUTS_PHYS_BASE, GUTS_PHYS_SIZE,
	    GUTS_PHYS_BASE, guts_callback, NULL)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "%s: register_vf_handler() failed\n", __func__);
	}

	return;
fail:
	printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		 "%s: virtualized guts initialization failed\n", __func__);
}

