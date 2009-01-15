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

static inline void set_cpc_reg_index(int num, int index)
{
	cpcs[num].cpc_reg_map |= 1 << index;
}

static inline void set_cpc_way_map(int num, uint32_t val)
{
	cpcs[num].cpc_way_map |= val >> count_lsb_zeroes(val);
}

static void init_cpc_dev(int num, void *vaddr)
{
	int i;

	cpcs[num].cpc_part_base = (cpc_part_reg_t *)((uint32_t)vaddr + CPCPIR0);
	/*cpc_reg_map requires 16 bits*/
	cpcs[num].cpc_reg_map = ~0xffffUL;

	for (i = 0; i < NUM_PART_REGS; i++) {
		uint32_t val;

		val = in32(&cpcs[num].cpc_part_base[i].cpcpir);
		if (val == CPCPIR0_RESET_MASK && i == 0)
			continue;

		if (val) {
			set_cpc_reg_index(num, i);
			val = in32(&cpcs[num].cpc_part_base[i].cpcpwr);
			set_cpc_way_map(num, val);
		}
	}
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
	 * CCSRBASE + CPC[num] offset.
	 */
	if ((dev->regs[0].start & 0xfffff) == CPCADDR0)
		init_cpc_dev(0, dev->regs[0].virt);
	else
		init_cpc_dev(1, dev->regs[0].virt);

	dev->driver = &cpc;

	return 0;
}

static inline int get_free_cpcpir(int num)
{

	int val = count_lsb_zeroes(~cpcs[num].cpc_reg_map);

	return val;
}

static cpc_part_reg_t *allocate_ways(int num, dt_prop_t *prop, uint32_t csdid)
{
	const uint32_t *ways;
	uint32_t val = 0;
	int numways, index, i;

	spin_lock(&cpcs[num].cpc_dev_lock);

	index = get_free_cpcpir(num);
	if (index == -1) {
		spin_unlock(&cpcs[num].cpc_dev_lock);
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
		if (!(cpcs[num].cpc_way_map & (1 << ways[i])))
			val |= (1 << (31 - ways[i]));
		else
			printlog(LOGTYPE_CPC, LOGLEVEL_ERROR,
					"CPC way %d already allocated\n", ways[i]);
	}

	if (val == 0) {
		spin_unlock(&cpcs[num].cpc_dev_lock);
		return NULL;
	}

	/* CPCPIRn is a bitmap of CSDIDs */
	out32(&cpcs[num].cpc_part_base[index].cpcpir, 1 << (31 - csdid));
	set_cpc_reg_index(num, index);

	out32(&cpcs[num].cpc_part_base[index].cpcpar, CPCPAR_MASK);
	out32(&cpcs[num].cpc_part_base[index].cpcpwr, val);
	set_cpc_way_map(num, val);

	spin_unlock(&cpcs[num].cpc_dev_lock);

	return &cpcs[num].cpc_part_base[index];
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
