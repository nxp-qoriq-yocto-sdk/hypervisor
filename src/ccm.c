/** @file
 * CoreNet Coherency Manager
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
#include <ccm.h>
#include <devtree.h>
#include <percpu.h>
#include <errors.h>

static law_t *laws;
static uint32_t *csdids;
static uint32_t csdid_map, ccm_lock;

static void disable_law(unsigned int lawidx)
{
	uint32_t val;
	assert(lawidx < NUMLAWS);

	val = in32(&laws[lawidx].attr);
	val &= ~LAW_ENABLE;
	out32(&laws[lawidx].attr, val);
}

static void enable_law(unsigned int lawidx)
{
	uint32_t val;
	assert(lawidx < NUMLAWS);

	val = in32(&laws[lawidx].attr);
	val |= LAW_ENABLE;
	out32(&laws[lawidx].attr, val);
}

void add_cpus_to_csd(guest_t *guest, dt_node_t *node)
{
	uint32_t val, i, core, base, num;
	uint32_t cpus = 0;

	if (guest == NULL) {
		cpus = ((1 << MAX_CORES) - 1) << (32 - MAX_CORES);
		goto hv;
	}

	for (i = 0; i < guest->cpulist_len / 4; i += 2) {
		base = guest->cpulist[i];
		num = guest->cpulist[i + 1];

		for (core = base; core < base + num; core++) {
			if (core >= MAX_CORES) {
				printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				         "%s: core %d doesn't match any valid ccm port\n",
				         __func__, core);

				/* Leave the LAW disabled so the PMA is global. */
				return;
			}

			cpus |= 1 << (31 - core);
		}
	}

hv:
	spin_lock(&ccm_lock);
	disable_law(node->csd->law_id);

	val = in32(&csdids[node->csd->csd_id]);
	val |= cpus;

	out32(&csdids[node->csd->csd_id], val);
	enable_law(node->csd->law_id);

	spin_unlock(&ccm_lock);
}

static int get_sizebit(phys_addr_t size)
{
	if (sizeof(long) == 4 && size >> 32)
		return count_lsb_zeroes((unsigned long)(size >> 32)) + 32;

	return count_lsb_zeroes((unsigned long)size);
}

/* Target ID should be DDR1, DDR2 or interleaved memory. It has to be non zero. */
static uint32_t get_law_target(pma_t *pma)
{
	phys_addr_t addr, size;
	uint32_t val;
	int lawidx;

	for (lawidx = 0; lawidx < NUMLAWS; lawidx++) {
		val = in32(&laws[lawidx].attr);
		if (val & LAW_ENABLE) {
			val = in32(&laws[lawidx].high);
			addr = ((phys_addr_t)val) << 32 |
			       in32(&laws[lawidx].low);

			val = in32(&laws[lawidx].attr);
			size = 1ULL << ((val & LAW_SIZE_MASK) + 1);

			if (pma->start >= addr &&
			    pma->start + pma->size - 1 <= addr + size - 1)
				return val & LAW_TARGETID_MASK;
		}
	}

	return 0;
}

static int set_law(dt_node_t *node, int csdid)
{
	int lawidx, sizebit;
	uint32_t val, tgt;
	pma_t *pma = node->pma;

	sizebit = get_sizebit(pma->size) - 1;
	if (sizebit < LAW_SIZE_4KB || sizebit > LAW_SIZE_64GB) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		         "%s: pma size out of LAW range\n", __func__);
		return ERR_BADTREE;
	}

	tgt = get_law_target(pma);
	if (!tgt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		         "%s: No existing LAW found for %s\n",
		         __func__, node->name);
		return ERR_BADTREE;
	}

	for (lawidx = 0; lawidx < NUMLAWS; lawidx++) {
		val = in32(&laws[lawidx].attr);

		if (!(val & LAW_ENABLE)) {
			val = in32(&laws[lawidx].high);
			/* set the lower 4 bits of the LAWBARHn */
			val |= ((uint32_t)(pma->start >> 32)) >> 28;
			out32(&laws->high, val);

			val = in32(&laws[lawidx].low);
			val |= (uint32_t)pma->start;
			out32(&laws[lawidx].low, val);
			printlog(LOGTYPE_CCM, LOGLEVEL_DEBUG,
			         "LAW[%d]->low = %x \n", lawidx, val);

			val = LAW_ENABLE | (csdid << LAWAR_CSDID_SHIFT)
				| sizebit | tgt;
			out32(&laws[lawidx].attr, val);

			printlog(LOGTYPE_CCM, LOGLEVEL_DEBUG,
			         "LAW[%d]->attr = %x\n", lawidx, val);

			return lawidx;
		}
	}

	printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
	         "%s: No free LAW found\n", __func__);
	return ERR_BUSY;
}

static int get_free_csd(void)
{
	int csdid;

	if ((~csdid_map & ((1ULL << NUMCSDS) - 1)) == 0) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		         "%s: out of CSDs\n", __func__);
		return ERR_BUSY;
	}

	csdid = count_lsb_zeroes(~csdid_map);
	assert(in32(&csdids[csdid]) == 0);
	csdid_map |= (1 << csdid);

	return csdid;
}

static inline void release_csd(int csdid)
{
	out32(&csdids[csdid], 0);
	csdid_map &= ~(1 << csdid);
}

static inline void release_law(int lawidx)
{
	uint32_t val = in32(&laws[lawidx].attr);
	out32(&laws[lawidx].attr, val & ~LAW_ENABLE);
}

static pma_t *read_pma(dt_node_t *node)
{
	pma_t *pma;
	dt_prop_t *prop;
	
	spin_lock(&ccm_lock);

	pma = node->pma;
	if (pma)
		goto out;

	pma = alloc_type(pma_t);
	if (!pma) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		goto out;
	}

	prop = dt_get_prop(node, "addr", 0);
	if (!prop || prop->len != 8) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: phys-mem-area %s has bad/missing addr\n",
		         __func__, node->name);

		goto out_free;
	}
	pma->start = *(const uint64_t *)prop->data;

	prop = dt_get_prop(node, "size", 0);
	if (!prop || prop->len != 8) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: phys-mem-area %s has bad/missing size\n",
		         __func__, node->name);

		goto out_free;
	}
	pma->size = *(const uint64_t *)prop->data;

	if (pma->size & (pma->size - 1)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: phys-mem-area %s is not a power of two\n",
		         __func__, node->name);

		goto out_free;
	}

	if (pma->start & (pma->size - 1)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: phys-mem-area %s is not naturally aligned\n",
		         __func__, node->name);

		goto out_free;
	}

out:
	node->pma = pma;
	spin_unlock(&ccm_lock);
	return pma;

out_free:
	spin_unlock(&ccm_lock);
	free(pma);
	return NULL;
}

static int setup_csd(dt_node_t *node, void *arg)
{
	pma_t *pma;
	int csdid, lawid;
	int ret = ERR_BADTREE;

	pma = read_pma(node);
	if (!pma)
		return 0;

	spin_lock(&ccm_lock);

	ret = csdid = get_free_csd();
	if (ret < 0)
		goto fail;

	ret = lawid = set_law(node, csdid);
	if (ret < 0)
		goto fail_csd;

	node->csd = alloc_type(csd_info_t);
	if (!node->csd) {
		ret = ERR_NOMEM;
		goto fail_law;
	}

	node->csd->law_id = lawid;
	node->csd->csd_id = csdid;

	spin_unlock(&ccm_lock);
	return 0;

fail_law:
	release_law(lawid);
fail_csd:
	release_csd(csdid);
fail:
	spin_unlock(&ccm_lock);
	printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
	         "%s: error %d for %s\n", __func__, ret, node->name);
	return ret;
}

void ccm_init(void)
{
	dt_node_t *node;

	/* FIXME: Appropriate nodes need to be created in the master device tree. */
	laws = map(CCSRBAR_PA + 0xc00, sizeof(law_t) * NUMLAWS,
	           TLB_MAS2_IO, TLB_MAS3_KERN);

	csdids = map(CCSRBAR_PA + 0x18600, 4 * NUMCSDS,
	             TLB_MAS2_IO, TLB_MAS3_KERN);

	/* Mark the CSDs that are already in use */
	for (int i = 0; i < NUMCSDS; i++)
		if (in32(&csdids[i]))
			csdid_map |= 1 << i;

	/* Set up coherency sub domains based on the available pmas in the
	 * config tree
	 */
	dt_for_each_compatible(config_tree, "phys-mem-area",
	                       setup_csd, NULL);

	node = dt_get_first_compatible(config_tree, "hv-memory");
	node = get_pma_node(node);
	add_cpus_to_csd(NULL, node);
}

dt_node_t *get_pma_node(dt_node_t *node)
{
	dt_node_t *pma_node;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "phys-mem", 0);
	if (!prop || prop->len != 4 ||
	    !(pma_node = dt_lookup_phandle(config_tree,
	                                   *(const uint32_t *)prop->data))) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: %s has bad/missing phys-mem phandle\n",
		         __func__, node->name);

		return NULL;
	}

	if (!dt_node_is_compatible(pma_node, "phys-mem-area")) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: %s's phys-mem points to %s, which is not a phys-mem-area\n",
		         __func__, node->name, pma_node->name);

		return NULL;
	}

	read_pma(pma_node);
	return pma_node;
}
