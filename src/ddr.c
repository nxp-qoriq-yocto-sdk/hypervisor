/** @file
 * Memory controller
 */
/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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

#include <libos/platform_error.h>

#include <p4080.h>
#include <percpu.h>
#include <errors.h>
#include <error_log.h>
#include <error_mgmt.h>
#include <ddr.h>

struct ddr_error_info {
	const char *policy;
	uint32_t reg_val;
};

static struct ddr_error_info ddr_err[DDR_ERROR_COUNT] = {
	[ddr_multiple_errors] = {NULL, DDR_ERR_DET_MME},
	[ddr_memory_select] = {NULL, DDR_ERR_DET_MSE},
	[ddr_single_bit_ecc] = {NULL, DDR_ERR_DET_SBE},
	[ddr_multi_bit_ecc] = {NULL, DDR_ERR_DET_MBE},
	[ddr_corrupted_data] = {NULL, DDR_ERR_DET_CDE},
	[ddr_auto_calibration] = {NULL, DDR_ERR_DET_ACE},
	[ddr_address_parity] = {NULL, DDR_ERR_DET_APE},
};

static int ddr_probe(driver_t *drv, device_t *dev);

static driver_t __driver ddr = {
	.compatible = "fsl,p4080-memory-controller",
	.probe = ddr_probe
};

static void dump_ddr_error(hv_error_t *err)
{
	ddr_error_t *ddr = &err->ddr;

	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "device path:%s\n", err->hdev_tree_path);
	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "ddr errdet : %x, ddr errinten : %x\n",
		ddr->ddrerrdet, ddr->ddrerrinten);
	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "ddr errdis : %x\n",
		ddr->ddrerrdis);
	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "ddr errattr : %x, ddr captecc : %x\n",
		ddr->ddrerrattr, ddr->ddrcaptecc);
	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "ddr singlebit ecc mgmt : %x\n",
		ddr->ddrsbeccmgmt);
	printlog(LOGTYPE_DDR, LOGLEVEL_ERROR, "ddr error address : %llx\n",
		ddr->ddrerraddr);
}

static void ddr_error_attr_cap(ddr_err_reg_t *ddr_err_regs, ddr_error_t *ddr,
		uint32_t errattr)
{
	ddr->ddrerrattr = errattr;
	ddr->ddrerraddr =
		((uint64_t) in32(&ddr_err_regs->ddr_ext_err_addr) << 32) |
		in32(&ddr_err_regs->ddr_err_addr);
	ddr->ddrcaptecc =
		in32(&ddr_err_regs->ddr_capt_ecc);
	ddr->ddrsbeccmgmt =
		in32(&ddr_err_regs->ddr_sbecc_err_mgmt);
}

static int ddr_error_isr(void *arg)
{
	hv_error_t err = { };
	device_t *dev = arg;
	dt_node_t *ddr_node;
	ddr_err_reg_t *ddr_err_regs;
	uint32_t val;
	uint32_t errattr = 0;

	ddr_err_regs = (ddr_err_reg_t *)((uintptr_t)dev->regs[0].virt + DDR_ERR_CAPT_ECC);

	val = in32(&ddr_err_regs->ddr_err_det);

	strncpy(err.domain, get_domain_str(error_ddr), sizeof(err.domain));
	ddr_node = to_container(dev, dt_node_t, dev);
	dt_get_path(NULL, ddr_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));

	for (int i = ddr_multiple_errors; i < DDR_ERROR_COUNT; i++) {
		const char *policy = ddr_err[i].policy;
		uint32_t err_val = ddr_err[i].reg_val;

		if (val & err_val) {
			ddr_error_t *ddr = &err.ddr;

			strncpy(err.error, get_error_str(error_ddr, i), sizeof(err.error));

			ddr->ddrerrdet = val;
			ddr->ddrerrinten =
				in32(&ddr_err_regs->ddr_err_int_en);
			ddr->ddrerrdis =
				in32(&ddr_err_regs->ddr_err_dis);
			errattr = in32(&ddr_err_regs->ddr_err_attr);
			if (errattr & DDR_ERR_ATTR_VALID)
				ddr_error_attr_cap(ddr_err_regs, ddr, errattr);

			error_policy_action(&err, error_ddr, policy);
		}

	}

	out32(&ddr_err_regs->ddr_err_det, val);

	return 0;
}

static int ddr_probe(driver_t *drv, device_t *dev)
{
	ddr_err_reg_t *ddr_err_regs;

	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_DDR, LOGLEVEL_ERROR,
				"DDR reg initialization failed\n");
		return ERR_INVALID;
	}

	ddr_err_regs = (ddr_err_reg_t *)((uintptr_t)dev->regs[0].virt + DDR_ERR_CAPT_ECC);

	out32(&ddr_err_regs->ddr_err_int_en, 0);
	out32(&ddr_err_regs->ddr_err_dis, DDR_ERR_MASK);

	if (dev->num_irqs >= 1) {
		uint32_t val = 0;
		uint32_t threshold = 0;

		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq) {
			for (int i = ddr_memory_select; i < DDR_ERROR_COUNT; i++) {
				ddr_err[i].policy = get_error_policy(error_ddr, i);

				if (!strcmp(ddr_err[i].policy, "disable"))
					continue;

				if (i == ddr_single_bit_ecc)
					threshold = get_error_threshold(error_ddr, i);

				val |= ddr_err[i].reg_val;
			}

			register_error_dump_callback(error_ddr, dump_ddr_error);

			irq->ops->register_irq(irq, ddr_error_isr, dev, TYPE_MCHK);

			if (threshold)
				out32(&ddr_err_regs->ddr_sbecc_err_mgmt, threshold << DDR_SB_THRESH_SHIFT);

			out32(&ddr_err_regs->ddr_err_dis, ~val & DDR_ERR_MASK);
			out32(&ddr_err_regs->ddr_err_int_en, val);
		}
	}

	dev->driver = &ddr;

	return 0;
}
