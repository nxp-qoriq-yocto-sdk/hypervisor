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

static cpc_dev_t cpcs[num_mem_tgts];
static uint32_t num_cpc_ways;

int cpcs_enabled(void)
{
	for (int i = 0; i < num_mem_tgts; i++){
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

	poll_reg_bit_clear(cpcs[mem_tgt].cpccsr0, CPCCSR0_CPCFL);
	poll_reg_bit_clear(cpcs[mem_tgt].cpccsr0, CPCCSR0_CPCLFC);

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
	cpcs[mem_tgt].cpc_part_base =
		(cpc_part_reg_t *)((uintptr_t)vaddr + CPCPIR0);
	/*cpc_reg_map requires 16 bits*/
	cpcs[mem_tgt].cpc_reg_map = ~0xffffUL;

	reconfigure_cpc(mem_tgt);

	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpir, CPCPIR0_RESET_MASK);
	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpar, CPCPAR0_RESET_MASK);
	out32(&cpcs[mem_tgt].cpc_part_base[0].cpcpwr, CPCPIR0_RESET_MASK);

	reserve_partition_reg(mem_tgt, 0);
}


static int cpc_probe(driver_t *drv, device_t *dev);

static driver_t __driver cpc = {
	.compatible = "fsl,p4080-l3-cache-controller",
	.probe = cpc_probe
};

static int cpc_error_isr(void *arg)
{
	hv_error_t err = { };
	device_t *dev = arg;
	interrupt_t *irq;

	irq = dev->irqs[0];
	irq->ops->disable(irq);

	strncpy(err.domain, get_domain_str(error_cpc), sizeof(err.domain));
	strcpy(err.error, get_domain_str(error_cpc));

	error_log(&hv_global_event_queue, &err, &hv_queue_prod_lock);

	return 0;
}

static int cpc_probe(driver_t *drv, device_t *dev)
{
	dt_node_t *cpc_node;
	dt_prop_t *prop;
	uint32_t num_sets, cache_size, block_size;

	/* The hardware device tree has a single l3-cache node.
	 * This node would contain reg properties corresponding
	 *  to both cpc0 and cpc1.
	 *  Number of CPCs should be equal to number of associated
	 *  memory targets, excluding the interleaved target.
	 *  In case of P4080 we have two targets in form of
	 */

	if (dev->num_regs != num_mem_tgts || !dev->regs[0].virt) {
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
		if ((dev->num_regs > i) && (dev->regs[i].virt != NULL))
			init_cpc_dev(i, dev->regs[i].virt);

		if (dev->num_irqs > i) {
			interrupt_t *irq = dev->irqs[i];
			if (irq && irq->ops->register_irq)
				irq->ops->register_irq(irq, cpc_error_isr, dev, TYPE_MCHK);
		}
	}

	dev->driver = &cpc;

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

	pir_num = get_free_cpcpir(mem_tgt);
	if (pir_num == -1) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"Way allocation failed, all CPC partitioning registers taken\n");
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
		break;
	case LAW_TRGT_DDR2:
		node->cpc_reg[mem_tgt_ddr1] = NULL;
		node->cpc_reg[mem_tgt_ddr2] = allocate_ways(mem_tgt_ddr2, prop, csdid);
		break;
	case LAW_TRGT_INTLV:
		node->cpc_reg[mem_tgt_ddr1] = allocate_ways(mem_tgt_ddr1, prop, csdid);
		node->cpc_reg[mem_tgt_ddr2] = allocate_ways(mem_tgt_ddr2, prop, csdid);
		break;
	default:
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"Invalid target for CPC\n");
	}
}
