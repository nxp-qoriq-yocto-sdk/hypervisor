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

#ifdef CONFIG_WARM_REBOOT
int *ddr_laws_found;
#endif

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

static void dump_ccf_error(hv_error_t *err)
{
	ccf_error_t *ccf = &err->ccf;

	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "device path : %s\n", err->hdev_tree_path);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "ccf cedr : %x, ccf ceer : %x, ccf cmecar : %x\n",
				ccf->cedr, ccf->ceer, ccf->cmecar);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR, "ccf %s cecar : %x, ccf cecaddr : %llx\n",
				err->error, ccf->cecar, ccf->cecaddr);
}

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
	uint32_t i, *sidmr; /* Snoop ID Port mapping register */
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
	for (i = 0; i < numcsds; i++) {
		if (in32(&csdids[i]))
			csdid_map |= 1 << i;
	}

#ifdef CONFIG_WARM_REBOOT
	/*
	 * In case of warm reboot, the HV LAWs are already disabled, meaning
	 * that at this point, the ongoing I/O to memory is made through LAW31,
	 * set by U-Boot using CSD_ID 0, containing all CoreNet ports. We should
	 * release CSD_IDs from previous cold boot, taking care of their usage
	 * in CPC.
	 */
	if (warm_reboot && ddr_laws_found) {
		for (i = 0; i < numlaws; i++) {
			int csdid_to_remove;

			if (!ddr_laws_found[i])
				continue;

			csdid_to_remove = ddr_laws_found[i] & LAWAR_CSDID_MASK;
			csdid_to_remove >>= LAWAR_CSDID_SHIFT;
			release_csd(csdid_to_remove);
		}
	}
#endif

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
#ifdef CONFIG_WARM_REBOOT
	if (!warm_reboot)
#endif
	{
		for (int i = 1; i < num_snoopids; i++)
			out32(&sidmr[i], (0x80000000 >> (i - 1)));
	}

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

			register_error_dump_callback(error_ccf, dump_ccf_error);

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

#ifndef CONFIG_WARM_REBOOT
	return 0;
#else
	phys_addr_t addr;
	int ddr1_pos, ddr2_pos, ddrint_pos;
	uint32_t tgt = 0, val;
	int lawidx, found_hv_laws = 0;

	if (!warm_reboot)
		return 0;

	ddr_laws_found = alloc(numlaws * sizeof(*ddr_laws_found),
			       sizeof(*ddr_laws_found));
	if (!ddr_laws_found) {
		printlog(LOGTYPE_CCM, LOGLEVEL_ERROR,
			 "%s: memory allocation failed\n", __func__);
		return ERR_NOMEM;
	}

	/* We can access VA of pamu_mem_header now, with HV relocated */
	if (pamu_mem_header->magic != HV_MEM_MAGIC ||
	    pamu_mem_header->version != 0) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			 "ignoring parameter \"warm-reboot\" because "
			 "hv-persistent-data content is invalid\n");
		goto warmreboot_fail;
	}

	/*
	 * Identify U-Boot DDR target and disable other DDR LAWs. It gets a bit
	 * complex if we aim not to use any information from a cold boot.
	 * The worst case scenario to setup LAWs is in case of controller
	 * interleaving: memory region A to DDR1, region B to DDR2, and region C
	 * using DDR interleaved.
	 * Alg: find the rightmost LAW for each of the three target IDs and
	 * delete any LAW to the left of that target.
	 */
	ddr1_pos = ddr2_pos = ddrint_pos = numlaws;

	for (lawidx = numlaws - 1; lawidx >= 0; lawidx--) {
		val = in32(&laws[lawidx].attr);
		if (val & LAW_ENABLE) {
			tgt = (val & LAW_TARGETID_MASK) >> LAWAR_TARGETID_SHIFT;

			/*
			 * In case of warm reboot, disable all HV LAWs, as this
			 * will not disturb in-flight transactions to DDR which
			 * will be guided through U-Boot LAWs.
			 */
			if ((ddr1_pos < numlaws && tgt == LAW_TRGT_DDR1) ||
			    (ddr2_pos < numlaws && tgt == LAW_TRGT_DDR2) ||
			    (ddrint_pos < numlaws && tgt == LAW_TRGT_INTLV)) {
				found_hv_laws = 1;
				disable_law(lawidx);
				ddr_laws_found[lawidx] = (val & LAWAR_CSDID_MASK)
							 | 1;
				printlog(LOGTYPE_DEV, LOGLEVEL_DEBUG,
					 "warm reboot: disable LAW %d\n", lawidx);
				continue;
			}

			if (ddr1_pos == numlaws && tgt == LAW_TRGT_DDR1)
				ddr1_pos = lawidx;
			else if (ddr2_pos == numlaws && tgt == LAW_TRGT_DDR2)
				ddr2_pos = lawidx;
			else if (ddrint_pos == numlaws && tgt == LAW_TRGT_INTLV)
				ddrint_pos = lawidx;
		}
	}

	if (!found_hv_laws) {
		/*
		 * A possible problem. HV uses at least a LAW for PAMU tables.
		 * If PAMU support is disabled, there should be no warm-reboot.
		 */
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			 "ignoring parameter \"warm-reboot\" because HV LAWs "
			 "were not found in CCSR\n");
		goto warmreboot_fail;
	}

	return 0;

warmreboot_fail:
	/*
	 * NOTE: Maybe it would be better to abort the init, and just crash,
	 * but in this way we help the testing, which does not clear the DDR
	 * between each unit-testing execution, so you may find PAMU mem
	 * leftovers from a previous run.
	 */
	warm_reboot = 0;
	memset(pamu_mem_header, 0, pamu_mem_size);
	pamu_mem_header->magic = HV_MEM_MAGIC;
	pamu_mem_header->version = 0;
	flush_core_caches();

	return 0;
#endif
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

	set_csd_cpus(node->csd, cpus_mask);
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

	return ~(uint32_t)0;
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
	if (!(~tgt)) {
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

	if (dt_get_prop(node, "use-no-law", 0))
		pma->use_no_law = 1;

#ifdef CONFIG_WARM_REBOOT
	if (!warm_reboot)
#endif
	{
		if (dt_get_prop(node, "zero-pma", 0)) {
			phys_addr_t ret = zero_to_phys(pma->start, pma->size);
			if (ret != pma->size) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
					 "%s: could not zero pma %s: "
					 "zeroed %llu bytes\n",
					 __func__, node->name,
					 (unsigned long long)ret);
			}
		}
	}

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

/*
 * Table of SVRs and the corresponding PORT_ID values.
 *
 * All future CoreNet-enabled SOCs will have erratum A-004510 fixed, so this
 * table should never need to be updated. SVRs are guaranteed to be unique,
 * so there is no worry that a future SOC will inadvertently have one of
 * these values.
 */
static const struct {
	uint32_t svr;
	uint32_t port_id;
} port_id_map[] = {
	{0x82100010, 0xFF000000},	/* P2040 1.0 */
	{0x82100011, 0xFF000000},	/* P2040 1.1 */
	{0x82100110, 0xFF000000},	/* P2041 1.0 */
	{0x82100111, 0xFF000000},	/* P2041 1.1 */
	{0x82110310, 0xFF000000},	/* P3041 1.0 */
	{0x82110311, 0xFF000000},	/* P3041 1.1 */
	{0x82010020, 0xFFF80000},	/* P4040 2.0 */
	{0x82000020, 0xFFF80000},	/* P4080 2.0 */
	{0x82210010, 0xFC000000},       /* P5010 1.0 */
	{0x82210020, 0xFC000000},       /* P5010 2.0 */
	{0x82200010, 0xFC000000},	/* P5020 1.0 */
	{0x82050010, 0xFF800000},	/* P5021 1.0 */
	{0x82040010, 0xFF800000},	/* P5040 1.0 */
	{0, 0}
};

#define	SVR_SECURITY	0x80000	/* The Security (E) bit */

int setup_pamu_law(dt_node_t *node)
{
	int ret, i;

	ret = setup_csd_law(node);
	if (ret == 0) {
		/* CSDID0 reset value defines a mask of all corenet ports
		 * which includes all the cores and CHBs with PAMU enabled.
		 */
		uint32_t csd_port_id = in32(&csdids[0]);

#ifdef CONFIG_ERRATUM_PAMU_A_004510
		/*
		 * Check to see if we need to implement the erratum A-004510
		 * work-around on this SoC
		 *
		 * Determine the Port ID for our coherence subdomain
		 */
		for (i = 0; port_id_map[i].svr; i++) {
			if (port_id_map[i].svr == (mfspr(SPR_SVR) & ~SVR_SECURITY)) {
				csd_port_id = port_id_map[i].port_id;
				printlog(LOGTYPE_CCM, LOGLEVEL_DEBUG,
					 "found matching SVR %08x\n",
					 port_id_map[i].svr);
				break;
			}
		}
#endif

		set_csd_cpus(node->csd, csd_port_id);
	}

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
