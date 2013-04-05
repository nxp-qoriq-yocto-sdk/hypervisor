/** @fil
 * CoreNet Platform Cache
 */
/*
 * Copyright (C) 2009, 2010 Freescale Semiconductor, Inc.
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
#include <libos/platform_error.h>

#include <p4080.h>
#include <cpc.h>
#include <devtree.h>
#include <percpu.h>
#include <errors.h>
#include <error_log.h>
#include <error_mgmt.h>

struct cpc_error_info {
	const char *policy;
	uint32_t reg_val;
};

static struct cpc_error_info cpc_err[CPC_ERROR_COUNT] = {
	[cpc_multiple_errors] = {NULL, CPC_ERRDET_MULLERR},
	[cpc_tag_multi_way_hit] = {NULL, CPC_ERRDET_TMHITERR},
	[cpc_tag_status_multi_bit_ecc] = {NULL, CPC_ERRDET_TMBECCERR},
	[cpc_tag_status_single_bit_ecc] = {NULL, CPC_ERRDET_TSBECCERR},
	[cpc_data_multi_bit_ecc] = {NULL, CPC_ERRDET_DMBECCERR},
	[cpc_data_single_bit_ecc] = {NULL, CPC_ERRDET_DSBECCERR},
};

static cpc_dev_t cpcs[max_num_mem_tgts];
static uint32_t num_cpc_ways;
static uint32_t num_cpcs;

int cpcs_enabled(void)
{
	for (int i = 0; i < num_cpcs; i++){
		if (!cpcs[i].cpccsr0 ||
			!(in32(cpcs[i].cpccsr0) & CPCCSR0_CPCE))
				return 0;
	}

	return 1;
}

static inline void poll_reg_bit_clear(void *reg, uint32_t reg_val)
{
	out32(reg, in32(reg) | reg_val);
	while (in32(reg) & reg_val);
}

static inline void reconfigure_cpc(mem_tgts_t mem_tgt)
{
	int i;

	for (i = 0; i < NUM_PART_REGS; i++)
		out32(&cpcs[mem_tgt].cpc_part_base[i].cpcpar, 0);

	/*
	 * Just for CPC repartitioning, no need for flush nor lock flash
	 * P4080 chip errata: CPC 10, CPC 8 (CPCFL, CPCLFC)
	 */
}

static inline void reserve_partition_reg(mem_tgts_t mem_tgt, int pir_num)
{
	cpcs[mem_tgt].cpc_reg_map |= 1 << pir_num;
}

static inline void remove_pir0_ways(mem_tgts_t mem_tgt, uint32_t way_mask)
{
	uint32_t val;

	val = in32(&cpcs[mem_tgt].cpc_part_base[0].cpcpwr);
	val &= ~way_mask;
	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpwr, val);
}

static void init_cpc_dev(mem_tgts_t mem_tgt, void *vaddr)
{
	if (!(in32(vaddr) & CPCCSR0_CPCE)) {
		printlog(LOGTYPE_CPC, LOGLEVEL_WARN,
		         "%s: WARNING: CPCs are not enabled\n",
		          __func__);
		return;
        }

	cpcs[mem_tgt].cpccsr0 = vaddr;
	cpcs[mem_tgt].cpc_err_base =
		(cpc_err_reg_t *) ((uintptr_t)vaddr + CPC_ERRDET);
	cpcs[mem_tgt].cpc_part_base =
		(cpc_part_reg_t *)((uintptr_t)vaddr + CPCPIR0);

	/* NOTE: In case of warm reboot, CPC can be reconfigured */
	reset_cpc(mem_tgt);

	reserve_partition_reg(mem_tgt, 0);
}

int reset_cpc(mem_tgts_t mem_tgt)
{
	if (!cpcs[mem_tgt].cpccsr0)
		return 0;	/* This CPC is not mapped */

	/*cpc_reg_map requires 16 bits*/
	cpcs[mem_tgt].cpc_reg_map = ~0xffffUL;

	reconfigure_cpc(mem_tgt);

	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpir, CPCPIR0_RESET_MASK);
	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpar, CPCPAR0_RESET_MASK);
	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpwr, CPCPIR0_RESET_MASK);

	return 1;
}


static int cpc_probe(device_t *dev, const dev_compat_t *compat_id);

static const dev_compat_t cpc_compats[] = {
	{
		.compatible = "fsl,p4080-l3-cache-controller",
	},
	{
		.compatible = "fsl,t4240-l3-cache-controller"
	},
	{}
};

static driver_t __driver cpc = {
	.compatibles = cpc_compats,
	.probe = cpc_probe
};

static void dump_cpc_error(hv_error_t *err)
{
	cpc_error_t *cpc = &err->cpc;

	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "cpc %s error address : %llx\n",
		err->error, cpc->cpcerraddr);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "device path:%s\n", err->hdev_tree_path);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "cpc errdet : %x, cpc errinten : %x\n",
		cpc->cpcerrdet, cpc->cpcerrinten);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "cpc errdis : %x\n",
		cpc->cpcerrdis);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "cpc errattr : %x, cpc captecc : %x\n",
		cpc->cpcerrattr, cpc->cpccaptecc);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "cpc errctl : %x\n",
		cpc->cpcerrctl);
}

static int cpc_error_isr(void *arg)
{
	hv_error_t err = { };
	cpc_dev_t *cpc = arg;
	uint32_t val;
	uint32_t errattr = 0;

	val = in32(&cpc->cpc_err_base->cpcerrdet);

	strncpy(err.domain, get_domain_str(error_cpc), sizeof(err.domain));

	dt_get_path(NULL, cpc->cpc_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));

	for (int i = cpc_multiple_errors; i < CPC_ERROR_COUNT; i++) {
		const char *policy = cpc_err[i].policy;
		uint32_t err_val = cpc_err[i].reg_val;

		if ( val & err_val) {
			cpc_error_t *hv_cpc_err = &err.cpc;
			uint32_t errctlval;

			strncpy(err.error, get_error_str(error_cpc, i), sizeof(err.error));

			hv_cpc_err->cpcerrdet = val;
			hv_cpc_err->cpcerrinten = in32(&cpc->cpc_err_base->cpcerrinten);
			hv_cpc_err->cpcerrdis = in32(&cpc->cpc_err_base->cpcerrdis);
			errattr = in32(&cpc->cpc_err_base->cpcerrattr);
			if (errattr & 0x1) {
				hv_cpc_err->cpcerrattr = errattr;
				hv_cpc_err->cpccaptecc =
					in32((uint32_t *)((uintptr_t)cpc->cpccsr0 + CPC_CAPTECC));
				hv_cpc_err->cpcerraddr =
					((phys_addr_t) in32(&cpc->cpc_err_base->cpcerreaddr) << 32) |
					in32(&cpc->cpc_err_base->cpcerraddr);
			}
			hv_cpc_err->cpcerrctl = errctlval =
				in32(&cpc->cpc_err_base->cpcerrctl);

			if (i == cpc_data_single_bit_ecc)
				errctlval &= ~CPC_ERRCTL_DATA_CNT_MASK;

			if (i == cpc_tag_status_single_bit_ecc)
				errctlval &= ~CPC_ERRCTL_TAG_CNT_MASK;

			out32(&cpc->cpc_err_base->cpcerrctl, errctlval);

			error_policy_action(&err, error_cpc, policy);
		}
	}

	if (errattr & 0x1)
		out32(&cpc->cpc_err_base->cpcerrattr, errattr & ~0x1);

	out32(&cpc->cpc_err_base->cpcerrdet, val);

	return 0;
}

static int cpc_probe(device_t *dev, const dev_compat_t *compat_id)
{
	dt_node_t *cpc_node;
	dt_prop_t *prop;
	uint32_t num_sets, cache_size, block_size;

	/* The hardware device tree has a single l3-cache node.
	 * The reg properties contains register ranges for the
	 * number of actual CPC controllers.
	 * The number of CPCs should be equal to the number of
	 * associated memory targets, excluding the interleaved target.
	 */

	num_cpcs = dev->num_regs;
	if (num_cpcs == 0 || !dev->regs[0].virt || num_cpcs > max_num_mem_tgts) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"CPC reg initialization failed\n");
		return ERR_INVALID;
	}

	cpc_node = to_container(dev, dt_node_t, dev);
	prop = dt_get_prop(cpc_node, "cache-sets", 0);
	if (prop && prop->len == 4) {
		num_sets = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		         "%s: bad/missing property cache-sets in %s\n",
		         __func__, cpc_node->name);
		return ERR_BADTREE;
	}

	prop = dt_get_prop(cpc_node, "cache-size", 0);
	if (prop && prop->len == 4) {
		cache_size = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		         "%s: bad/missing property cache-size in %s\n",
		         __func__, cpc_node->name);
		return ERR_BADTREE;
	}

	prop = dt_get_prop(cpc_node, "cache-block-size", 0);
	if (prop && prop->len == 4) {
		block_size = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		         "%s: bad/missing property cache-block-size in %s\n",
		         __func__, cpc_node->name);
		return ERR_BADTREE;
	}

	num_cpc_ways = cache_size/(num_sets * block_size);

	if (num_cpc_ways > 32) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		         "%s: Invalid CPC configuration, number of ways = %d > 32\n",
		         __func__, num_cpc_ways);
		return ERR_BADTREE;
	}

	for (int i = 0; i < dev->num_regs; i++) {
		uint32_t *errinten;
		uint32_t *errdis;
		uint32_t *errctl;

		if ((dev->num_regs > i) && (dev->regs[i].virt != NULL))
			init_cpc_dev(i, dev->regs[i].virt);

		cpcs[i].cpc_node = cpc_node;

		errinten = &cpcs[i].cpc_err_base->cpcerrinten;
		errdis = &cpcs[i].cpc_err_base->cpcerrdis;
		errctl = &cpcs[i].cpc_err_base->cpcerrctl;

		out32(errinten, 0);
		out32(errdis, CPC_ERR_MASK);

		if (dev->num_irqs > i) {
			uint32_t val = 0;
			uint32_t threshold = 0;

			interrupt_t *irq = dev->irqs[i];
			if (irq && irq->ops->register_irq) {
				for (int i = cpc_tag_multi_way_hit; i < CPC_ERROR_COUNT; i++) {
					cpc_err[i].policy = get_error_policy(error_cpc, i);

					if (!strcmp(cpc_err[i].policy, "disable"))
						continue;

					if ((i == cpc_data_single_bit_ecc) ||
						(i == cpc_tag_status_single_bit_ecc))
						threshold = get_error_threshold(error_cpc, i);

					val |= cpc_err[i].reg_val;
				}

				register_error_dump_callback(error_cpc, dump_cpc_error);

				irq->ops->register_irq(irq, cpc_error_isr, &cpcs[i], TYPE_MCHK);

				if (threshold)
					out32(errctl, threshold << CPC_ERRCTL_THRESH_SHIFT);

				out32(errdis, ~val & CPC_ERR_MASK);
				out32(errinten, val);
			}
		}
	}

	return 0;
}

static inline int get_free_cpcpir(mem_tgts_t mem_tgt)
{

	int val = count_lsb_zeroes(~cpcs[mem_tgt].cpc_reg_map);

	return val;
}

static cpc_part_reg_t *allocate_ways(mem_tgts_t mem_tgt, dt_prop_t *prop, uint32_t csdid)
{
	const uint32_t *ways;
	uint32_t val = 0;
	int numways, pir_num, i;


	if (!cpcs[mem_tgt].cpccsr0) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		         "%s: ERROR: CPCs do not appear to be assigned to the hypervisor\n",
		          __func__);
		return NULL;
	}

	numways = prop->len / sizeof(uint32_t);
	ways = (const uint32_t *)prop->data;

	for (i = 0; i < numways; i++) {
		if (ways[i] > num_cpc_ways - 1) {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				 "Invalid CPC way number %d\n", ways[i]);
			continue;
		}
		val |= (1 << (31 - ways[i]));
	}

	if (val == 0)
		return NULL;

	pir_num = get_free_cpcpir(mem_tgt);
	if (pir_num == -1) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
			 "Way allocation failed, all CPC partitioning "
			 "registers taken\n");
		return NULL;
	}

	remove_pir0_ways(mem_tgt, val);

	/* CPCPIRn is a bitmap of CSDIDs */
	out32(&cpcs[mem_tgt].cpc_part_base[pir_num].cpcpir, 1 << (31 - csdid));
	reserve_partition_reg(mem_tgt, pir_num);

	out32(&cpcs[mem_tgt].cpc_part_base[pir_num].cpcpar, CPCPAR_MASK);
	out32(&cpcs[mem_tgt].cpc_part_base[pir_num].cpcpwr, val);

	return &cpcs[mem_tgt].cpc_part_base[pir_num];
}

void allocate_cpc_ways(dt_prop_t *prop, uint32_t tgt, uint32_t csdid, dt_node_t *node)
{

	switch(tgt) {
	case LAW_TRGT_DDR1:
		node->cpc_reg[mem_tgt_ddr1] = allocate_ways(mem_tgt_ddr1, prop, csdid);
		node->cpc_reg[mem_tgt_ddr2] = NULL;
		node->cpc_reg[mem_tgt_ddr3] = NULL;
		break;
	case LAW_TRGT_DDR2:
		if (num_cpcs > 1) {
			node->cpc_reg[mem_tgt_ddr1] = NULL;
			node->cpc_reg[mem_tgt_ddr2] = allocate_ways(mem_tgt_ddr2, prop, csdid);
			node->cpc_reg[mem_tgt_ddr3] = NULL;
		} else {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
			         "%s: ERROR: only 1 CPC available\n",
			         __func__);
		}
		break;
	case LAW_TRGT_DDR3:
		if (num_cpcs > 2) {
			node->cpc_reg[mem_tgt_ddr1] = NULL;
			node->cpc_reg[mem_tgt_ddr2] = NULL;
			node->cpc_reg[mem_tgt_ddr3] = allocate_ways(mem_tgt_ddr3, prop, csdid);
		} else {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
			         "%s: ERROR: not enough CPCs available\n",
			         __func__);
		}
		break;
	case LAW_TRGT_INTLV_12:
		if (num_cpcs > 1) {
			node->cpc_reg[mem_tgt_ddr1] = allocate_ways(mem_tgt_ddr1, prop, csdid);
			node->cpc_reg[mem_tgt_ddr2] = allocate_ways(mem_tgt_ddr2, prop, csdid);
			node->cpc_reg[mem_tgt_ddr3] = NULL;
		} else {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
			         "%s: ERROR: only 1 CPC available\n",
			         __func__);
		}
		break;
	case LAW_TRGT_INTLV_123:
		if (num_cpcs > 2) {
			node->cpc_reg[mem_tgt_ddr1] = allocate_ways(mem_tgt_ddr1, prop, csdid);
			node->cpc_reg[mem_tgt_ddr2] = allocate_ways(mem_tgt_ddr2, prop, csdid);
			node->cpc_reg[mem_tgt_ddr3] = allocate_ways(mem_tgt_ddr3, prop, csdid);
		} else {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
			         "%s: ERROR: not enough CPCs available\n",
			         __func__);
		}
		break;

	default:
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
		        "Invalid target for CPC\n");
	}
}
