/** @file
 * CoreNet Coherency Manager
 */

/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <libos/alloc.h>
#include <libos/platform_error.h>

#include <p4080.h>
#include <ccm.h>
#include <cpc.h>
#include <devtree.h>
#include <percpu.h>
#include <errors.h>
#include <error_log.h>
#include <error_mgmt.h>

struct ccf_error_info {
	const char *policy;
	uint32_t reg_val;
};

static struct ccf_error_info ccf_err[CCF_ERROR_COUNT] = {
	[ccf_multiple_intervention] = {NULL, CCF_CEDR_MINT},
	[ccf_local_access] = {NULL, CCF_CEDR_LAE},
};

#define HV_CPUS         (((1 << MAX_CORES) - 1) << (32 - MAX_CORES))
#define PAMU_CSD_PORTS  0xfff80000

static law_t *laws;
static uint32_t *csdids;
static uint32_t *err_det_reg, *err_enb_reg;
static uint32_t csdid_map, pma_lock;
static uint32_t numcsds, numlaws;

static int ccm_probe(driver_t *drv, device_t *dev);
static int law_probe(driver_t *drv, device_t *dev);

static driver_t __driver ccm = {
	.compatible = "fsl,corenet-cf",
	.probe = ccm_probe
};

static driver_t __driver law = {
	.compatible = "fsl,corenet-law",
	.probe = law_probe
};

static int ccm_error_isr(void *arg)
{
	device_t *dev = arg;
	void *reg_base = dev->regs[0].virt;
	interrupt_t *irq = dev->irqs[0];
	uint32_t val;
	uint32_t errattr = 0;
	hv_error_t err = { };
	dt_node_t *ccf_node;

	val = in32(err_det_reg);

	strncpy(err.domain, get_domain_str(error_ccf), sizeof(err.domain));
	ccf_node = to_container(dev, dt_node_t, dev);
	dt_get_path(NULL, ccf_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));

	for (int i = ccf_multiple_intervention; i < CCF_ERROR_COUNT; i++) {
		const char *policy = ccf_err[i].policy;
		uint32_t err_val = ccf_err[i].reg_val;

		if ( val & err_val) {
			ccf_error_t *ccf = &err.ccf;

			strncpy(err.error, get_error_str(error_ccf, i), sizeof(err.error));

			ccf->cedr = val;
			ccf->ceer = in32(err_enb_reg);
			errattr = in32((uint32_t *)(reg_base + CCF_CECAR));
			if (errattr & CCF_CECAR_VAL) {
				ccf->cecar = errattr;
				ccf->cecaddr = ((phys_addr_t) in32((uint32_t *)(reg_base + CCF_CECADRH)) << 32) |
						in32((uint32_t *)(reg_base + CCF_CECADRL));
				out32((uint32_t *)(reg_base + CCF_CECADRH), (uint32_t)(ccf->cecaddr >> 32));
				out32((uint32_t *)(reg_base + CCF_CECADRL), (uint32_t)ccf->cecaddr);
				ccf->cmecar = in32((uint32_t *)(reg_base + CCF_CMECAR));
				out32((uint32_t *)(reg_base + CCF_CMECAR), ccf->cmecar);
			}

			error_policy_action(&err, error_ccf, policy);
			break;
		}
	}

	if (errattr & CCF_CECAR_VAL)
		out32((uint32_t *) (reg_base + CCF_CECAR), errattr & CCF_CECAR_VAL);

	out32(err_det_reg, val);

	return 0;
}

static int ccm_probe(driver_t *drv, device_t *dev)
{
	dt_prop_t *prop;
	dt_node_t *node = to_container(dev, dt_node_t, dev);
	uint32_t *sidmr; /* Snoop ID Port mapping register */
	int num_snoopids;

	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"CCM reg initialization failed\n");
		return ERR_INVALID;
	}

	prop = dt_get_prop(node, "fsl,ccf-num-csdids", 0);
	if (prop && prop->len == 4) {
		numcsds = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		         "%s: bad/missing property fsl,ccf-num-csdids in %s\n",
		         __func__, node->name);
		return ERR_BADTREE;
	}

	csdids = (uint32_t *) ((uintptr_t)dev->regs[0].virt + 0x600);

	/* Mark the CSDs that are already in use */
	for (uint32_t i = 0; i < numcsds; i++)
		if (in32(&csdids[i]))
			csdid_map |= 1 << i;

	/*
	 * Snoop domains are meant specifically for (PAMU based) stashing
	 * optimization to restrict snooping traffic to only the targeted
	 * core/cache during stashing transactions. The logic here is to
	 * implement snoop domains representing a single physical core.
	 */
	prop = dt_get_prop(node, "fsl,ccf-num-snoopids", 0);
	if (prop && prop->len == 4) {
		num_snoopids = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		        "%s: bad/missing property fsl,ccf-num-snoopids in %s\n",
		         __func__, node->name);
		return ERR_BADTREE;
	}

	if (num_snoopids > NUM_SNOOP_IDS) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		        "%s: fsl,ccf-num-snoopids exceeds platform limit %s\n",
		         __func__, node->name);
		return ERR_BADTREE;
	}

	sidmr = (uint32_t *) ((uintptr_t)dev->regs[0].virt + 0x200);

	/*
	 * NOTE: Looks like SIDMR00 is used for snoop-id 0, i.e, to enable
	 * snooping on all cores, hence program SIDMR01 onwards
	 */
	for (int i = 1; i < num_snoopids; i++)
		out32(&sidmr[i], (0x80000000 >> (i - 1)));

	err_det_reg = (uint32_t *) ((uintptr_t)dev->regs[0].virt + CCF_CEDR);
	err_enb_reg = (uint32_t *) ((uintptr_t)dev->regs[0].virt + CCF_CEER);
	/* clear all error interrupts */
	out32(err_enb_reg, 0);

	if (dev->num_irqs >= 1) {
		uint32_t val = 0;

		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq) {
			for (int i = ccf_multiple_intervention; i < CCF_ERROR_COUNT; i++) {
				ccf_err[i].policy = get_error_policy(error_ccf, i);

				if (!strcmp(ccf_err[i].policy, "disable"))
					continue;

				val |= ccf_err[i].reg_val;
			}

			irq->ops->register_irq(irq, ccm_error_isr, dev, TYPE_MCHK);

			out32(err_enb_reg, val);
		}
	}

	dev->driver = &ccm;

	return 0;
}

static int law_probe(driver_t *drv, device_t *dev)
{
	dt_prop_t *prop;
	dt_node_t *node = to_container(dev, dt_node_t, dev);

	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"LAW reg initialization failed\n");
		return ERR_INVALID;
	}

	prop = dt_get_prop(node, "fsl,num-laws", 0);
	if (prop && prop->len == 4) {
		numlaws = *(const uint32_t *)prop->data;
	} else {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
		         "%s: bad/missing property fsl,num-laws in %s\n",
		         __func__, node->name);
		return ERR_BADTREE;
	}

	laws = (law_t *) ((uintptr_t)dev->regs[0].virt + 0xc00);

	dev->driver = &law;

	return 0;
}

static void disable_law(unsigned int lawidx)
{
	uint32_t val;
	assert(lawidx < numlaws);

	val = in32(&laws[lawidx].attr);
	val &= ~LAW_ENABLE;
	out32(&laws[lawidx].attr, val);
}

static void enable_law(unsigned int lawidx)
{
	uint32_t val;
	assert(lawidx < numlaws);

	val = in32(&laws[lawidx].attr);
	val |= LAW_ENABLE;
	out32(&laws[lawidx].attr, val);
}

/* allocate CPC ways for PMA here */
static void pma_setup_cpc(dt_node_t *node)
{
	dt_prop_t *prop = dt_get_prop(node, "allocate-cpc-ways", 0);
	if (prop) {
		uint32_t val, tgt;

		val = in32(&laws[node->csd->law_id].attr);
		tgt = (val & LAW_TARGETID_MASK) >> LAWAR_TARGETID_SHIFT;
		allocate_cpc_ways(prop, tgt, node->csd->csd_id, node);
	}
}

static void set_csd_cpus(csd_info_t *csd, uint32_t cpus)
{
	uint32_t val;

	disable_law(csd->law_id);

	val = in32(&csdids[csd->csd_id]);
	val |= cpus;
	out32(&csdids[csd->csd_id], val);

	enable_law(csd->law_id);
}

void add_all_cpus_to_csd(dt_node_t *node)
{
	if (!node->csd)
		return;

	set_csd_cpus(node->csd, HV_CPUS);
}

void add_cpus_to_csd(guest_t *guest, dt_node_t *node)
{
	uint32_t i, core, base, num;
	uint32_t cpus = 0;
	register_t saved;

	if (!node->csd)
		return;

	for (i = 0; i < guest->cpulist_len / 4; i += 2) {
		base = guest->cpulist[i];
		num = guest->cpulist[i + 1];

		for (core = base; core < base + num; core++) {
			assert(core < MAX_CORES);
			cpus |= 1 << (31 - core);
		}
	}

	saved = spin_lock_intsave(&node->csd->csd_lock);
	set_csd_cpus(node->csd, cpus);
	spin_unlock_intsave(&node->csd->csd_lock, saved);
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
	uint32_t lawidx;

	for (lawidx = 0; lawidx < numlaws; lawidx++) {
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
	uint32_t lawidx;
	unsigned int sizebit;
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

	for (lawidx = 0; lawidx < numlaws; lawidx++) {
		val = in32(&laws[lawidx].attr);

		if (!(val & LAW_ENABLE)) {
			/* set the lower 4 bits of the LAWBARHn */
			val = pma->start >> 32;
			out32(&laws[lawidx].high, val);
			printlog(LOGTYPE_CCM, LOGLEVEL_DEBUG,
			         "LAW[%d]->high = %x \n", lawidx, val);

			val = (uint32_t)pma->start;
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

	printlog(LOGTYPE_CCM, LOGLEVEL_WARN,
	         "%s: no free LAW found\n", __func__);
	return ERR_BUSY;
}

static int get_free_csd(void)
{
	int csdid;

	if ((~csdid_map & ((1ULL << numcsds) - 1)) == 0)
		return ERR_BUSY;

	csdid = count_lsb_zeroes_32(~csdid_map);
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

	prop = dt_get_prop(node, "use-no-law", 0);
	if (prop)
		pma->use_no_law = 1;
out:
	node->pma = pma;
	return pma;

out_free:
	free(pma);
	return NULL;
}

static int setup_csd_law(dt_node_t *node)
{
	int csdid, lawid, ret;

	ret = csdid = get_free_csd();
	if (csdid < 0) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
	                 "%s: warning : getting free csd for %s.  Got ret=%d\n",
		          __func__, node->name, ret);
		return ret;
	}

	ret = lawid = set_law(node, csdid);
	if (ret < 0) {
		if (ret == ERR_BUSY)
			printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				 "%s: warning: out of LAWs,  %s will use global"
				 " coherence domain\n",
				__func__, node->name);
		else
			printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
			         "%s: error (%d) setting LAW for %s\n",
			         __func__, ret, node->name);
		goto fail_csd;
	}

	node->csd = alloc_type(csd_info_t);
	if (!node->csd) {
		ret = ERR_NOMEM;
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
	                 "%s: memory allocation error for %s\n",
		          __func__, node->name);
		goto fail_law;
	}

	node->csd->law_id = lawid;
	node->csd->csd_id = csdid;

	return 0;

fail_law:
	release_law(lawid);
fail_csd:
	release_csd(csdid);
	return ret;
}

#ifdef CONFIG_PAMU
int setup_pamu_law(dt_node_t *node)
{
	int ret;

	ret = setup_csd_law(node);
	if (ret == 0)
		/* CSD contains all the cores and PAMUs */
		set_csd_cpus(node->csd, PAMU_CSD_PORTS);

	return ret;
}
#endif

static int setup_csd(dt_node_t *node, void *arg)
{
	pma_t *pma;
	int ret = 0;

	pma = read_pma(node);
	if (!pma) {
		ret = ERR_BADTREE;
		goto fail;
	}

	if (!pma->use_no_law) {
		ret = setup_csd_law(node);
		if (ret == 0)
			pma_setup_cpc(node);
	}

fail:
	return ret;
}

void ccm_init(void)
{
	if (!laws || !csdids) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
				"CCM initialization failed\n");
		return;
	}

	/* Set up coherency sub domains based on the available pmas in the
	 * config tree
	 */
	dt_for_each_compatible(config_tree, "phys-mem-area",
	                       setup_csd, NULL);
}

dt_node_t *get_pma_node(dt_node_t *node)
{
	dt_node_t *pma_node;
	dt_prop_t *prop;
	register_t saved;

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

	/* Don't depend on the ccm initialization to initialize pma */
	saved = spin_lock_intsave(&pma_lock);
	if (pma_node->pma == NULL)
		read_pma(pma_node);
	spin_unlock_intsave(&pma_lock, saved);

	return pma_node;
}
