/** @file
 * CoreNet Platform Cache
 */
/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#include <p4080.h>
#include <cpc.h>
#include <devtree.h>
#include <percpu.h>
#include <errors.h>

static cpc_dev_t cpcs[NUMCPCS];

static inline void reserve_partition_reg(int cpc_index, int pir_num)
{
	cpcs[cpc_index].cpc_reg_map |= 1 << pir_num;
}

static inline void remove_pir0_ways(int cpc_index, uint32_t way_mask)
{
	uint32_t val;

	val = in32(&cpcs[cpc_index].cpc_part_base[0].cpcpwr);
	val &= ~way_mask;
	out32(&cpcs[cpc_index].cpc_part_base[0].cpcpwr, val);
}

static void init_cpc_dev(int cpc_index, void *vaddr)
{
	cpcs[cpc_index].cpc_part_base = (cpc_part_reg_t *)((uint32_t)vaddr + CPCPIR0);
	/*cpc_reg_map requires 16 bits*/
	cpcs[cpc_index].cpc_reg_map = ~0xffffUL;

	out32(&cpcs[cpc_index].cpc_part_base[0].cpcpir, CPCPIR0_RESET_MASK);
	out32(&cpcs[cpc_index].cpc_part_base[0].cpcpar, CPCPAR0_RESET_MASK);
	out32(&cpcs[cpc_index].cpc_part_base[0].cpcpwr, CPCPIR0_RESET_MASK);

	reserve_partition_reg(cpc_index, 0);
}


static int cpc_probe(driver_t *drv, device_t *dev);

static driver_t __driver cpc = {
	.compatible = "fsl,p4080-l3-cache-controller",
	.probe = cpc_probe
};

static int cpc_probe(driver_t *drv, device_t *dev)
{
	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"CPC reg initialization failed\n");
		return ERR_INVALID;
	}

	/* FIXME : It's assumed that CPC start addr would be
	 * CCSRBASE + CPC[cpc_index] offset.
	 */
	if ((dev->regs[0].start & 0xfffff) == CPCADDR0)
		init_cpc_dev(0, dev->regs[0].virt);
	else
		init_cpc_dev(1, dev->regs[0].virt);

	dev->driver = &cpc;

	return 0;
}

static inline int get_free_cpcpir(int cpc_index)
{

	int val = count_lsb_zeroes(~cpcs[cpc_index].cpc_reg_map);

	return val;
}

static cpc_part_reg_t *allocate_ways(int cpc_index, dt_prop_t *prop, uint32_t csdid)
{
	const uint32_t *ways;
	uint32_t val = 0;
	int numways, pir_num, i;

	pir_num = get_free_cpcpir(cpc_index);
	if (pir_num == -1) {
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"Way allocation failed, all CPC partitioning registers taken\n");
		return NULL;
	}

	numways = prop->len / sizeof(uint32_t);
	ways = (const uint32_t *)prop->data;

	for (i = 0; i < numways; i++) {
		if (ways[i] > NUMCPCWAYS - 1) {
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
					"Invalid CPC way number %d\n", ways[i]);
			continue;
		}
		val |= (1 << (31 - ways[i]));
	}

	if (val == 0)
		return NULL;

	remove_pir0_ways(cpc_index, val);

	/* CPCPIRn is a bitmap of CSDIDs */
	out32(&cpcs[cpc_index].cpc_part_base[pir_num].cpcpir, 1 << (31 - csdid));
	reserve_partition_reg(cpc_index, pir_num);

	out32(&cpcs[cpc_index].cpc_part_base[pir_num].cpcpar, CPCPAR_MASK);
	out32(&cpcs[cpc_index].cpc_part_base[pir_num].cpcpwr, val);

	return &cpcs[cpc_index].cpc_part_base[pir_num];
}

void allocate_cpc_ways(dt_prop_t *prop, uint32_t tgt, uint32_t csdid, dt_node_t *node)
{

	/* FIXME: Hard coded binding of CPC1 to DDR1 and CPC2 to DDR2 */
	switch(tgt) {
	case LAW_TRGT_DDR1:
		node->cpc_reg[0] = allocate_ways(0, prop, csdid);
		node->cpc_reg[1] = NULL;
		break;
	case LAW_TRGT_DDR2:
		node->cpc_reg[1] = allocate_ways(1, prop, csdid);
		node->cpc_reg[0] = NULL;
		break;
	case LAW_TRGT_INTLV:
		node->cpc_reg[0] = allocate_ways(0, prop, csdid);
		node->cpc_reg[1] = allocate_ways(1, prop, csdid);
		break;
	default:
		printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
				"Invalid target for CPC\n");
	}
}
