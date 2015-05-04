/** peripheral access management unit (PAMU) support
 *
 * @file
 *
 */

/*
 * Copyright (C) 2008-2012 Freescale Semiconductor, Inc.
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

#include <libos/pamu.h>
#include <libos/bitops.h>
#include <libos/percpu.h>
#include <libos/console.h>
#include <libos/alloc.h>
#include <libos/platform_error.h>
#include <libos/fsl_hcalls.h>

#include <pamu.h>
#include <percpu.h>
#include <errors.h>
#include <devtree.h>
#include <paging.h>
#include <events.h>
#include <ccm.h>
#include <cpc.h>
#include <limits.h>
#include <error_log.h>
#include <error_mgmt.h>
#include <guts.h>
#include <devtree.h>

static const char *pamu_err_policy[PAMU_ERROR_COUNT];


/* mcheck-safe lock, used to ensure atomicity of reassignment */
static uint32_t pamu_error_lock;

#ifdef CONFIG_ERRATUM_PAMU_A_004510
/*
 * The work-around says that we cannot have multiple writes to the PAACT
 * in flight simultaneously. To prevent that, we wrap the write in a mutex,
 * which will force the cores to perform their updates in sequence.
 */
static uint32_t pamu_lock;
#endif

static struct {
	pamu_handle_t *pamu_handle;
	guest_t *guest;
#ifdef CONFIG_ERRATUM_PAMU_A_003638
#define PAMU_STAT (PAMU_ACCESS_VIOLATION_STAT | PAMU_OPERATION_ERROR_INT_STAT)
	void *pamu_reg_disabled;
	unsigned int stash_vcpu;  // Target CPU for stashing for this LIODN
	unsigned int stash_cache; // Cache level (1, 2, or 3) for stashing,
				  // or 0 for no stashing
#endif
} liodn_data[PAACE_NUMBER_ENTRIES];

static int is_subwindow_count_valid(int subwindow_cnt)
{
	if (subwindow_cnt <= 1 || subwindow_cnt > pamu_get_max_subwindow_count())
		return 0;
	if (subwindow_cnt & (subwindow_cnt - 1))
		return 0;
	return 1;
}

#define L1 1
#define L2 2
#define L3 3

static unsigned int apply_a007907_workaround(unsigned int cache_level)
{
	static register_t svrs[] = {
		0x86800010, /* B4860 rev1 */
		0x86800020, /* B4860 rev2 */
		0x86810210, /* B4420 rev1 */
		0x86810220, /* B4420 rev2 */
		0x82400010, /* T4240 rev1 */
		0x82400020, /* T4240 rev2 */
		0x82410010, /* T4160 rev1 */
		0x82410020, /* T4160 rev2 */

		0x85300010, /* T2080 rev1 */
		0x85310010, /* T2081 rev1 */
		0
	};

	if (cache_level == L1) {
		int i;
		register_t svr = mfspr(SPR_SVR);

		for (i = 0; svrs[i]; i++)
			if ((svr & 0xfff7ffff) == svrs[i])
				return L2;
	}

	return cache_level;
}

/*
 * Find the stash ID for the given cache level of the given CPU node
 * @cpu_node: pointer to the CPU node
 * @cache_level: L1, L2, or L3
 */
static uint32_t get_stash_id(dt_node_t *cpu_node, unsigned int cache_level)
{
	dt_node_t *node = cpu_node;	// The cache node
	dt_prop_t *prop;

	cache_level = apply_a007907_workaround(cache_level);

	/*
	 * On the P4080, the L3 is the CPC and needs to be handled
	 * separately.
	 */
	if (cache_level == L3) {
		node = dt_get_first_compatible(hw_devtree,
				"fsl,p4080-l3-cache-controller");
		if (!node)
			// There's no CPC node
			return ~(uint32_t)0;

		if (!cpcs_enabled()) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
				"%s: %s not enabled\n",	__func__, node->name);
			return ~(uint32_t)0;
		}
		prop = dt_get_prop(node, "cache-stash-id", 0);
		if (!prop) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
				"%s: missing cache-stash-id in %s\n",
				__func__, node->name);
			return ~(uint32_t)0;
		}
		return *(const uint32_t *)prop->data;
	}

	// Iterate through the cache nodes until we get to the one we want.
	for (unsigned int i = L1; i < cache_level; i++) {
		uint32_t phandle;

		prop = dt_get_prop(node, "next-level-cache", 0);
		if (!prop || (prop->len != sizeof(uint32_t))) {
			// 'next-level-cache' property is missing or malformed
			// If the property is missing, then it means that
			// we're trying to find a cache level that does not
			// exist for this CPU.
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
				 "%s: 'next-level-cache' property for node %s is missing or invalid\n",
				__func__, node->name);
			return ~(uint32_t)0;
		}

		phandle = *(const uint32_t *)prop->data;

		node = dt_lookup_phandle(hw_devtree, phandle);
		if (!node) {
			// Invalid phandle
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
				 "%s: 'next-level-cache' property for node %s is an invalid phandle\n",
				__func__, node->name);
			return ~(uint32_t)0;
		}
	}

	// We've found a matching cache node.  Get the stash ID and exit.

	prop = dt_get_prop(node, "cache-stash-id", 0);
	if (!prop || (prop->len != sizeof(uint32_t))) {
		// Missing or invalid cache-stash-id property
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
			 "%s: 'cache-stash-id' property for node %s is missing or invalid\n",
			__func__, node->name);
		return ~(uint32_t)0;
	}

	return *(const uint32_t *)prop->data;
}

/*
 * Given the stash-dest enumeration value and a hw node of the device
 * being configured, return the cache-stash-id property value for
 * the associated cpu.  Assumption is that a cpu-handle property
 * points to that cpu.
 *
 * stash-dest values in the config tree are defined as:
 *    1 : L1 cache
 *    2 : L2 cache
 *    3 : L3/CPC cache
 *
 */
static uint32_t get_stash_dest(uint32_t stash_dest, dt_node_t *hwnode,
			       unsigned int *pcpu)
{
	dt_prop_t *prop;
	dt_node_t *node;

	prop = dt_get_prop(hwnode, "cpu-handle", 0);
	if (!prop || prop->len != 4)
		return ~(uint32_t)0;  /* if no cpu-phandle assume that this is
			      not a per-cpu portal */

	node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
	if (!node) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad cpu phandle reference in %s \n",
		          __func__, hwnode->name);
		return ~(uint32_t)0;
	}

	// Return the physical CPU ID that we found
	prop = dt_get_prop(node, "reg", 0);
	if (prop)
		*pcpu = *(const uint32_t *)prop->data;

	/* find the hwnode that represents the cache */
	return get_stash_id(node, stash_dest);
}

static uint32_t get_snoop_id(dt_node_t *gnode, guest_t *guest)
{
	dt_prop_t *prop;
	dt_node_t *node;

	prop = dt_get_prop(gnode, "cpu-handle", 0);
	if (!prop || prop->len != 4)
		return ~(uint32_t)0;

	node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
	if (!node) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad cpu phandle reference in %s \n",
		          __func__, gnode->name);
		return ~(uint32_t)0;
	}
	prop = dt_get_prop(node, "reg", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad reg in cpu node %s \n",
		          __func__, node->name);
		return ~(uint32_t)0;
	}
	return ccf_get_snoop_id(*(const uint32_t *)prop->data);
}

#define PEXIWBAR 0xDA8
#define PEXIWBEAR 0xDAC
#define PEXIWAR 0xDB0
#define PEXI_EN 0x80000000
#define PEXI_IWS 0x3F

static unsigned long setup_pcie_msi_subwin(guest_t *guest, dt_node_t *cfgnode,
					   dt_node_t *node, dt_node_t *gnode,
					   uint64_t gaddr, uint64_t *size)
{
	int ret;
	dt_prop_t *prop;
	uint32_t phandle;
	uint32_t msiir_offset;
	char buf[32];
	uint32_t reg[2];
	dt_prop_t *regprop;
	uint64_t msiir_addr = 0;
	dt_node_t *msi_gnode = NULL, *msi_node = NULL, *up_node;
	dev_owner_t *owner;
	unsigned long rpn = ULONG_MAX;
	uint8_t *pci_ctrl = NULL;
	phys_addr_t pcie_addr, pcie_size;
	int i;
	const char *mpic_msi_compats[] = {
		"fsl,mpic-msi",
		"fsl,mpic-msi-v4.3",
		NULL
	};

	prop = dt_get_prop(node, "device_type", 0);
	if (!prop || strcmp(prop->data, "pci"))
		return rpn;

	/* delete the fsl,msi property from the guest device tree. If the
	   hv config contains a node-update-phandle for the fsl,msi property,
	   the new property will be added to the guest device tree */
	prop = dt_get_prop(gnode, "fsl,msi", 0);
	if (prop)
		dt_delete_prop(prop);

	up_node = dt_get_subnode(cfgnode, "node-update-phandle", 0);

	if (up_node) {
		prop = dt_get_prop(up_node, "fsl,msi", 0);
		if (prop && prop->len == 4) {
			phandle = *(const uint32_t *)prop->data;
			list_for_each(&guest->dev_list, i) {
				owner = to_container(i, dev_owner_t, guest_node);
				if (dt_get_phandle(owner->cfgnode, 0) == phandle) {
					msi_node = owner->hwnode;
					msi_gnode = owner->gnode;
					break;
				}
			}
		}
	}
	/* if no node-update-phandle for fsl,msi property is found, the pci is 
	 * associated with the first msi node assigned to this guest */
	if (!msi_gnode)
		list_for_each(&guest->dev_list, i) {
			dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);
			if (dt_node_is_compatible_list(owner->hwnode,
			                               mpic_msi_compats)) {
				msi_node = owner->hwnode;
				msi_gnode = owner->gnode;
				dt_set_prop(gnode, "fsl,msi", &owner->cfgnode->guest_phandle, 4);
				break;
			}
		}

	if (!msi_gnode)
		return ULONG_MAX;

	if (!msi_node ||
	    !dt_node_is_compatible_list(msi_node, mpic_msi_compats)) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				 "%s: bad fsl,msi phandle in %s\n",
				 __func__, node->name);
		return ULONG_MAX;
	}

	/* read the aliased msiir register address. if there is no
	   alias, read the msiir address from the msi bank */
	ret = dt_get_reg(msi_node, 1, &msiir_addr, NULL);
	if (ret < 0) {
		uint64_t msi_addr;
		ret = dt_get_reg(msi_node, 0, &msi_addr, NULL);
		if (ret < 0) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
					 "%s: could not get reg in %s\n",
					 __func__, msi_node->name);
			return ULONG_MAX;
		}
		// fixme: this needs to be done via u-boot
		msiir_addr = msi_addr + 0x140;
	}
	msiir_offset = msiir_addr & (PAGE_SIZE - 1);
	rpn = msiir_addr >> PAGE_SHIFT;
	msiir_addr = gaddr + msiir_offset;

	if (*size > PAGE_SIZE)
		*size = PAGE_SIZE;

	if (dt_node_is_compatible(msi_node, "fsl,mpic-msi-v4.3"))
		ret = snprintf(buf, sizeof(buf), "fsl,vmpic-msi-v4.3");
	else
		ret = snprintf(buf, sizeof(buf), "fsl,vmpic-msi");
	ret = dt_set_prop(msi_gnode, "compatible", buf, ret + 1);
	if (ret < 0)
		return ULONG_MAX;

	regprop = dt_get_prop(msi_gnode, "reg", 0);
	if (regprop)
		dt_delete_prop(regprop);

	reg[0] = msiir_addr >> 32;
	reg[1] = msiir_addr & 0xffffffff;

	ret = dt_set_prop(gnode, "msi-address-64", reg, rootnaddr * 4);
	if (ret < 0)
		goto nomem;

	ret = dt_get_reg(node, 0, &pcie_addr, &pcie_size);
	if (ret < 0 || pcie_size < 0x1000) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				 "%s: bad/missing reg\n", __func__);
		return ULONG_MAX;
	}

	size_t len = 0x1000;
#if 1
	pci_ctrl = map_phys(TEMPTLB1, pcie_addr, TEMP_MAPPING1,
						&len, TLB_TSIZE_4K, TLB_MAS2_IO,
						TLB_MAS3_KDATA);
#else
/* needed if pcie virtualization is enabled */
	if (node->vf)
		pci_ctrl = node->vf->vaddr;
#endif
	if (!pci_ctrl || len < 0x1000) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				 "%s: couldn't map reg %llx (guest phys) of %s\n",
				 __func__, pcie_addr, msi_node->name);
		return ULONG_MAX;
	}

	for (i = 0; i <= 2; i++) {
		uint32_t piwar, piwbear, piwbar;

		piwar = in32((uint32_t *)
			(pci_ctrl + PEXIWAR + i * 0x20));

		if (piwar & PEXI_EN) {
			piwbar = in32((uint32_t *)
				(pci_ctrl + PEXIWBAR + i * 0x20));
			piwbear = in32((uint32_t *) (pci_ctrl +
					PEXIWBEAR + i * 0x20));

			/* logic works for undefined PEXIWBEAR1 */
			uint64_t inb_win_addr = ((uint64_t)
				((piwbear & 0xfffff) << 12 |
					  piwbar >> 20)  << 32) |
				(piwbar & 0xfffff) << 12;

			uint32_t inb_win_size = 1 <<
				((piwar & PEXI_IWS) + 1);

			if (msiir_addr >= inb_win_addr &&
				msiir_addr <= inb_win_addr + inb_win_size - 1)
				break;
		}
	}

	tlb1_clear_entry(TEMPTLB1);

	if (i > 2) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				 "%s: %s: msiir-address 0x%llx outside inbound memory windows\n",
				 __func__, node->name, msiir_addr);

		return ULONG_MAX;
	}

	return rpn;

nomem:
	printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
	return ULONG_MAX;
}

#define MAX_SUBWIN_CNT 16

#ifdef CONFIG_CLAIMABLE_DEVICES

static int reconfig_subwins(guest_t *guest, dt_node_t *parent,
			    uint32_t liodn,
			    phys_addr_t primary_base,
			    phys_addr_t primary_size,
			    uint32_t subwindow_cnt)
{
	paace_t *ppaace;
	list_t *ele = parent->children.next;
	dt_node_t *node;
	uint64_t gaddr = primary_base;
	uint64_t size = primary_size / subwindow_cnt;
	unsigned long rpn;
	int subwin;
	int ret = 0;

	ppaace = pamu_get_ppaace(liodn);

	for (subwin = 0;
	     subwin < subwindow_cnt;
	     ++subwin, gaddr += size, ele = ele->next) {
		node = to_container(ele, dt_node_t, child_node);

		/*
		 * Ignore PCIe MSI subwindows as their RPN should be constant,
		 * even if primary/standby partitions are mapped to different
		 * MSI banks, as MSI banks exist in the same physical page.
		 */
		if (dt_get_prop(node, "pcie-msi-subwindow", 0))
			continue;

		rpn = get_rpn(guest, gaddr >> PAGE_SHIFT, size >> PAGE_SHIFT);

		if (rpn == ULONG_MAX)
			continue;

		ret = pamu_reconfig_subwin(liodn, subwin, rpn);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int hv_pamu_reconfig_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode)
{
	paace_t *ppaace;
	unsigned long rpn;
	dt_node_t *dma_window;
	phys_addr_t window_addr = ~(phys_addr_t)0;
	phys_addr_t window_size = 0;
	uint32_t subwindow_cnt;
	int ret = 0;

	ppaace = pamu_get_ppaace(liodn);
	subwindow_cnt = 1 << (get_bf(ppaace->impl_attr, PAACE_IA_WCE) + 1);
	window_addr = ((phys_addr_t) ppaace->wbah << 32) |
		       ((uint32_t) get_bf(ppaace->addr_bitfields, PPAACE_AF_WBAL)
				<< PAGE_SHIFT);
	window_size = 1 << (get_bf(ppaace->addr_bitfields, PPAACE_AF_WSE) + 1);
	dma_window = hwnode->dma_window;
	if (!dma_window) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: bad dma-window phandle ref during reconfig\n",
		         __func__);
		return ERR_BADTREE;
	}

	if (get_bf(ppaace->addr_bitfields, PPAACE_AF_MW)) {
		ret = reconfig_subwins(guest, dma_window, liodn,
				       window_addr, window_size, subwindow_cnt);
	} else {
		rpn = get_rpn(guest, window_addr >> PAGE_SHIFT, window_size >> PAGE_SHIFT);
		if (rpn == ULONG_MAX)
			return ERR_NOTRANS;

		ret = pamu_reconfig_liodn(liodn, rpn);
	}


	return ret;
}
#endif

static int setup_subwins(guest_t *guest, dt_node_t *parent,
			 uint32_t liodn, phys_addr_t primary_base,
			 phys_addr_t primary_size, uint32_t subwindow_cnt,
			 uint32_t omi, uint32_t snoopid, uint32_t stash_dest,
			 dt_node_t *cfgnode, dt_node_t *hwnode, dt_node_t *gnode)
{
	phys_addr_t subwindow_size = primary_size / subwindow_cnt;
	int32_t ret;

	list_for_each(&parent->children, i) {
		dt_node_t *node = to_container(i, dt_node_t, child_node);
		dt_prop_t *prop;
		uint64_t gaddr, size;
		unsigned long rpn;
		int subwin;

		prop = dt_get_prop(node, "guest-addr", 0);
		if (!prop || prop->len != 8) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: bad/missing guest-addr in %s/%s\n",
			         __func__, parent->name, node->name);
			continue;
		}

		gaddr = *(uint64_t *)prop->data;
		if (gaddr < primary_base ||
		    gaddr > primary_base + primary_size) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: guest-addr %llx in %s/%s out of bounds\n",
			         __func__, gaddr, parent->name, node->name);
			continue;
		}

		if (gaddr & (subwindow_size - 1)) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: guest-addr %llx in %s/%s misaligned\n"
			         "subwindow size %llx\n",
			         __func__, gaddr, parent->name,
			         node->name, subwindow_size);
			continue;
		}

		subwin = (gaddr - primary_base) / subwindow_size;
		assert(primary_base + subwin * subwindow_size == gaddr);

		prop = dt_get_prop(node, "size", 0);
		if (!prop || prop->len != 8) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_WARN,
			         "%s: warning: missing/bad size prop at %s/%s\n",
			         __func__, parent->name,
			         node->name);
			size = subwindow_size;
		} else {
			size = *(uint64_t *)prop->data;
		}

		if (size & (size - 1) || size > subwindow_size ||
		    size < PAGE_SIZE) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: size %llx in %s/%s "
			         "out of range, or not a power of 2\n",
			         __func__, size, parent->name, node->name);
			continue;
		}

		if (dt_get_prop(node, "pcie-msi-subwindow", 0)) {
			rpn = setup_pcie_msi_subwin(guest, cfgnode, hwnode,
				gnode, gaddr, &size);

		} else if (dt_get_prop(node, "srio-ccsr-subwindow", 0)) {
			rpn = gaddr >> PAGE_SHIFT;
		}
		else {
			rpn = get_rpn(guest, gaddr >> PAGE_SHIFT,
				size >> PAGE_SHIFT);
		}
		if (rpn == ULONG_MAX)
			continue;

		ret = pamu_config_spaace(liodn, subwindow_cnt, subwin, size, omi,
					 rpn, snoopid, stash_dest);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * Given a device liodn and a device config node setup
 * the paace entry for this device.
 *
 * Before this function is called the caller should have
 * established whether there is an associated dma-window
 * so it is an error if a dma-window prop is not found.
 */
int hv_pamu_config_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode, dt_node_t *cfgnode, dt_node_t *gnode)
{
	uint32_t snpid = ~(uint32_t)0;
	dt_prop_t *prop, *stash_prop;
	dt_prop_t *gaddr;
	dt_prop_t *size;
	dt_node_t *dma_window;
	phys_addr_t window_addr = ~(phys_addr_t)0;
	phys_addr_t window_size = 0;
	uint32_t stash_dest = ~(uint32_t)0;
	uint32_t omi = ~(uint32_t)0;
	uint32_t subwindow_cnt = 0;
	unsigned long rpn = ULONG_MAX;
	int32_t ret;

	prop = dt_get_prop(cfgnode, "dma-window", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_WARN,
		         "%s: warning: missing dma-window at %s\n",
		         __func__, cfgnode->name);
		return ERR_BADTREE;
	}

	dma_window = dt_lookup_phandle(config_tree, *(const uint32_t *)prop->data);
	if (!dma_window) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: bad dma-window phandle ref at %s\n",
		         __func__, cfgnode->name);
		return ERR_BADTREE;
	}

#ifdef CONFIG_CLAIMABLE_DEVICES
	hwnode->dma_window = dma_window;
#endif

	gaddr = dt_get_prop(dma_window, "guest-addr", 0);
	if (!gaddr || gaddr->len != 8) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: warning: missing/bad guest-addr at %s\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}

	window_addr = *(const uint64_t *)gaddr->data;

	size = dt_get_prop(dma_window, "size", 0);
	if (!size || size->len != 8) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: missing/bad size prop at %s\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}

	window_size = *(const uint64_t *)size->data;
	if ((window_size & (window_size - 1)) || window_size < PAGE_SIZE) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: %s size too small or not a power of two\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}

	if (window_addr & (window_size - 1)) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: %s is not aligned with window size\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}

	liodn_data[liodn].guest = guest;

#ifdef CONFIG_WARM_REBOOT
	if (warm_reboot)
		return 0; /* Skip the remaining tasks as PAMU tables are set */
#endif
	/* set up operation mapping if it's configured */
	prop = dt_get_prop(cfgnode, "operation-mapping", 0);
	if (prop) {
		if (prop->len == 4) {
			omi = *(const uint32_t *)prop->data;
			if (omi > OMI_MAX) {
				printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				         "%s: bad operation mapping index at %s\n",
				         __func__, cfgnode->name);
			}
		} else {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: bad operation mapping index at %s\n",
			         __func__, cfgnode->name);
		}
	}

	/*
	 * Determine the stash ID, if specified.  We also want to remember
	 * the stash target CPU and cache level.
	 */
	stash_prop = dt_get_prop(cfgnode, "stash-dest", 0);
	if (stash_prop && stash_prop->len == 4) {
		unsigned int pcpu = 0;

		liodn_data[liodn].stash_cache = *(const uint32_t *)stash_prop->data;

		stash_dest = get_stash_dest(liodn_data[liodn].stash_cache, gnode, &pcpu);

		// Convert the physical CPU number to a guest CPU number
		for (unsigned i = 0; i < guest->cpucnt; i++) {
			if (vcpu_to_cpu(guest->cpulist, guest->cpulist_len, i) == pcpu) {
				liodn_data[liodn].stash_vcpu = i;
				break;
			}
		}
	}

	/* configure snoop-id if needed */
	prop = dt_get_prop(cfgnode, "snoop-cpu-only", 0);
	if (prop && ~stash_dest != 0) {
		if ((*(const uint32_t *)stash_prop->data) >= L3) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"%s: %s snoop-cpu-only property must have stash-dest as L1 or L2 cache\n",
				 __func__, cfgnode->name);
			goto skip_snoop_id;
		}

		if (!dt_get_prop(cfgnode->parent, "vcpu", 0)) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"%s: missing vcpu property in %s node corresponding to snoop-cpu-only property defined in %s node\n",
				__func__, cfgnode->parent->name, cfgnode->name);
			goto skip_snoop_id;
		}

		snpid = get_snoop_id(gnode, guest);

	}
skip_snoop_id:

	prop = dt_get_prop(dma_window, "subwindow-count", 0);
	if (prop) {
		if (prop->len != 4) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"%s: bad subwindow-count length %zu in %s\n",
				 __func__, prop->len, cfgnode->name);
			return ERR_BADTREE;
		}

		subwindow_cnt = *(const uint32_t *)prop->data;

		if (!is_subwindow_count_valid(subwindow_cnt)) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"%s: bad subwindow-count %d in %s\n",
				 __func__, subwindow_cnt, cfgnode->name);
			return ERR_BADTREE;
		}
	}

	if (!subwindow_cnt) {
		rpn = get_rpn(guest, window_addr >> PAGE_SHIFT, window_size >> PAGE_SHIFT);
		if (rpn == ULONG_MAX) {
			return ERR_NOTRANS;
		}
	}

	ret = pamu_config_ppaace(liodn, window_addr, window_size, omi, rpn, snpid,
				 stash_dest, subwindow_cnt);
	if (ret < 0) {
		if (ret == ERR_BUSY) {
			/* QMan portal devices have multiple nodes sharing
			 * an liodn.
			 */
			if (dt_node_is_compatible(hwnode->parent, "fsl,qman-portal"))
				return 0;
		}
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
	             "%s: liodn %d or device in use\n", __func__, liodn);
	        return ret;
	}

	ret = setup_subwins(guest, dma_window, liodn, window_addr,
			    window_size, subwindow_cnt, omi,
			    snpid, stash_dest, cfgnode, hwnode, gnode);
	if (ret < 0)
		return ret;

	lwsync();

	/* PAACE is invalid, validated by enable hcall, unless
	 * the user has requested dma-continues-after-partition-stop
	 */
	return 0;
}

int hv_pamu_enable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;
	int ret;
	uint32_t saved;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle])
		return EV_EINVAL;

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle)
		return EV_EINVAL;

	liodn = pamu_handle->assigned_liodn;

#ifdef CONFIG_CLAIMABLE_DEVICES
	if (liodn_data[liodn].guest != guest)
		return EV_INVALID_STATE;
#endif

#ifdef CONFIG_ERRATUM_PAMU_A_003638
	/* re-enable access violations if necessary */
	if (liodn_data[liodn].pamu_reg_disabled) {
		uint32_t pics;
		void *reg = liodn_data[liodn].pamu_reg_disabled;

		/* access violations are now enabled for this PAMU */
		liodn_data[liodn].pamu_reg_disabled = NULL;

		lwsync();

		/* re-enable access violations if necessary */
		pics = in32(reg + PAMU_PICS);
		out32(reg + PAMU_PICS, (pics & ~PAMU_STAT) | PAMU_ACCESS_VIOLATION_ENABLE);
	}
#endif

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	saved = spin_lock_intsave(&pamu_lock);
#endif

	ret = pamu_enable_liodn(liodn);

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	spin_unlock_intsave(&pamu_lock, saved);
#endif

	if (ret)
		return EV_EINVAL;

	return 0;
}

int hv_pamu_disable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;
	int ret;
	uint32_t saved;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle])
		return EV_EINVAL;

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle)
		return EV_EINVAL;

	liodn = pamu_handle->assigned_liodn;

#ifdef CONFIG_CLAIMABLE_DEVICES
	if (liodn_data[liodn].guest != guest)
		return EV_INVALID_STATE;
#endif

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	saved = spin_lock_intsave(&pamu_lock);
#endif

	ret = pamu_disable_liodn(liodn);

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	spin_unlock_intsave(&pamu_lock, saved);
#endif

	if (ret)
		return EV_EINVAL;

	return 0;
}

static void setup_omt(void)
{
	ome_t *ome;

	/* Configure OMI_QMAN */
	ome = pamu_get_ome(OMI_QMAN);
	if (!ome) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: failed to read operation mapping table\n", __func__);
		return;
	}

	ome->moe[IOE_READ_IDX] = EOE_VALID | EOE_READ;
	ome->moe[IOE_EREAD0_IDX] = EOE_VALID | EOE_RSA;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
	ome->moe[IOE_EWRITE0_IDX] = EOE_VALID | EOE_WWSAO;

	/*
	 * When it comes to stashing DIRECTIVEs, the QMan BG says
	 * (1.5.6.7.1:  FQD Context_A field used for dequeued etc.
	 * etc. stashing control):
	 * - AE/DE/CE == 0:  don't stash exclusive.  Use DIRECT0,
	 *                   which should be a non-PE LOADEC.
	 * - AE/DE/CE == 1:  stash exclusive via DIRECT1, i.e.
	 *                   LOADEC-PE
	 * If one desires to alter how the three different types of
	 * stashing are done, please alter rx_conf.exclusive in
	 * ipfwd_a.c (that specifies the 3-bit AE/DE/CE field), and
	 * do not alter the settings here.  - bgrayson
	 */
	ome->moe[IOE_DIRECT0_IDX] = EOE_VALID | EOE_LDEC;
	ome->moe[IOE_DIRECT1_IDX] = EOE_VALID | EOE_LDECPE;

	/* Configure OMI_FMAN */
	ome = pamu_get_ome(OMI_FMAN);
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READI;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;

	/* Configure OMI_QMAN private */
	ome = pamu_get_ome(OMI_QMAN_PRIV);
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READ;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
	ome->moe[IOE_EREAD0_IDX] = EOE_VALID | EOE_RSA;
	ome->moe[IOE_EWRITE0_IDX] = EOE_VALID | EOE_WWSA;

	/* Configure OMI_CAAM */
	ome = pamu_get_ome(OMI_CAAM);
	ome->moe[IOE_READ_IDX]  = EOE_VALID | EOE_READI;
	ome->moe[IOE_WRITE_IDX] = EOE_VALID | EOE_WRITE;
}

static void pamu_error_log(hv_error_t *err, guest_t *guest)
{
	if (guest)
		error_log(&guest->error_event_queue, err, &guest->error_log_prod_lock);
}

static void dump_pamu_error(hv_error_t *err)
{
	pamu_error_t *pamu = &err->pamu;

	if (!strcmp(err->error, "access violation"))
		printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR,
			"PAMU access violation: dev=%s, liodn=%d, address=%llx\n",
		         err->gdev_tree_path,
			 pamu->avs1 >> PAMU_AVS1_LIODN_SHIFT,
			 pamu->access_violation_addr);

	if (!strcmp(err->error, "single-bit ecc") ||
		!strcmp(err->error, "multi-bit ecc"))
		printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR,
			"PAMU ecc error addr = %llx, data = %llx\n",
			pamu->eccaddr, pamu->eccdata);

	if (!strcmp(err->error, "operation"))
		printlog(LOGTYPE_ERRORQ, LOGLEVEL_ERROR,
			"PAMU operation error poes1 = %x, poeaddr = %llx\n",
			pamu->poes1, pamu->poeaddr);

	printlog(LOGTYPE_MISC, LOGLEVEL_EXTRA, "device path:%s\n",
		 err->hdev_tree_path);
	printlog(LOGTYPE_MISC, LOGLEVEL_EXTRA, "guest device path:%s\n",
		 err->gdev_tree_path);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA,
		"PAMU access violation avs1 = %x, avs2 = %x\n",
		 pamu->avs1, pamu->avs2);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA,
		"PAMU access violation lpid = %x, handle = %x\n",
		pamu->lpid, pamu->liodn_handle);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA,
		"PAMU ecc error eccctl = %x, eccdis = %x, eccinten = %x\n",
		pamu->eccctl, pamu->eccdis, pamu->eccinten);
	printlog(LOGTYPE_ERRORQ, LOGLEVEL_EXTRA,
		"PAMU ecc error eccdet  = %x, eccattr = %x\n",
		pamu->eccdet, pamu->eccattr);
}

static int pamu_error_isr(void *arg)
{
	device_t *dev = arg;
	void *reg_base = dev->regs[0].virt;
	phys_addr_t reg_size = dev->regs[0].size;
	unsigned long reg_off;
	int ret = -1;
	dt_node_t *pamu_node;
	hv_error_t err = { };

	pamu_node = to_container(dev, dt_node_t, dev);
	for (reg_off = 0; reg_off < reg_size; reg_off += PAMU_OFFSET) {
		void *reg = reg_base + reg_off;
		uint32_t pics, poes1;

		pics = in32(reg + PAMU_PICS);
		if (pics & PAMU_OPERATION_ERROR_INT_STAT) {
			snprintf(err.domain, sizeof(err.domain), "%s",
				 get_domain_str(error_pamu));
			snprintf(err.error, sizeof(err.error), "%s",
				 get_error_str(error_pamu, pamu_operation));
			dt_get_path(NULL, pamu_node, err.hdev_tree_path,
				    sizeof(err.hdev_tree_path));
			poes1 = in32(reg + PAMU_POES1);
			if (poes1 & PAMU_POES1_POED) {
				err.pamu.poes1 = poes1;
				err.pamu.poeaddr = (((phys_addr_t)in32(reg + PAMU_POEAH)) << 32) |
						   in32(reg + PAMU_POEAL);
			}
			error_policy_action(&err, error_pamu,
					    pamu_err_policy[pamu_operation]);
			out32(reg + PAMU_POES1, PAMU_POES1_POED);
			out32(reg + PAMU_PICS, pics);
		}
	}

	return 0;
}

static int node_found_callback(dt_node_t *node, void *arg)
{
	*(dt_node_t **)arg = node;

	return 1;
}

static dt_node_t *get_dev_node(uint32_t handle, guest_t *guest)
{
	dt_node_t *node;
	int ret;

	ret = dt_for_each_prop_value(guest->devtree, "fsl,hv-dma-handle", &handle,
				     4, node_found_callback, &node);
	if (ret)
		return node;

	return NULL;
}

static int handle_access_violation(void *reg, dt_node_t *pamu_node, uint32_t pics)
{
	uint32_t av_liodn, avs1;
	paace_t *ppaace;
	hv_error_t err = { };
	pamu_error_t *pamu = &err.pamu;
#ifdef CONFIG_P4080_ERRATUM_PAMU3
	int rev1;

	/*
	 * For rev1 silicon we have PAMU 3 erratum that says: The PAMU CCSR register
	 * PICS[AVICS] bit is not cleared when software writes the value 1 to clear.
	 * The workaround is for the interrupt handler to write PICS[AVICS] to the value
	 * 1 in two consecutive store instructions.
	 * In rev2 silicon, PAMU 3 was fixed, but we have PAMU-A001 that says: If the PAMU
	 * register bit value is 1, software writing 1 to the bit will clear the bit value,
	 * but if the register bit value is 0, software writing 1 to the bit will update
	 * the bit of the registers to a value of 1. In this case we have to make sure that
	 * we are not writing the register twice, so the interrupt clear handling will be
	 * different for rev1 and rev2.
	 */
	rev1 = (mfspr(SPR_PVR) & 0xfffffff0) == 0x80230010;
#endif

	snprintf(err.domain, sizeof(err.domain), "%s", get_domain_str(error_pamu));
	dt_get_path(NULL, pamu_node, err.hdev_tree_path,
		    sizeof(err.hdev_tree_path));

	avs1 = in32 (reg + PAMU_AVS1);
	av_liodn = avs1 >> PAMU_AVS1_LIODN_SHIFT;

#ifdef CONFIG_ERRATUM_PAMU_A_003638
	/* When a peripheral interface makes an invalid memory access, an access violation
	 * interrupt is triggered. The interrupt handler clears PICS[AVICS] to clear
	 * the interrupt and also clears the appropriate bit from the PAACE entry to stop
	 * further attempts from the peripheral interface that may cause further access
	 * violations.
	 * Due to errata, clearing the valid bit in PAACE entry will not stop further access
	 * violations from the interface. Furthermore, if access violations continue to occur,
	 * the PAMU can end up into a state where PICS[AVICS] is 0, but the access violation
	 * interrupt is still fired.
	 * Workaround:
	 * When the access violation occurs, the access violation reporting is disabled for
	 * this PAMU and it is the responsability of the guest to re-enable DMA when the
	 * device is in a valid state (when no more illegal memory accesses are performed).
	 * When the guest re-enables DMA, if that liodn caused the access violation interrupts
	 * to be disabled, they will be re-enabled
	 */

	/* disable the access violations for this PAMU
	 *- AVICS should be 0, do not clear it yet
	 *- POEICS should be 0, as 1 is changing the value
	 *- AVIE should be 0 to disable access violations for this PAMU
	 *- POEIE should be left as it is
	 */
	out32(reg + PAMU_PICS, (pics & ~PAMU_STAT) & ~PAMU_ACCESS_VIOLATION_ENABLE);
	liodn_data[av_liodn].pamu_reg_disabled = reg;
	pics = pics & ~PAMU_ACCESS_VIOLATION_ENABLE;
#endif

	ppaace = pamu_get_ppaace(av_liodn);
	/* We may get access violations for invalid LIODNs, just ignore them */
	if (!get_bf(ppaace->addr_bitfields, PAACE_AF_V))
	{
		/* Clear the write one to clear bits in AVS1, mask out the LIODN */
		out32(reg + PAMU_AVS1, (avs1 & PAMU_AV_MASK));

		/* De-assert access violation pin */
		out32(reg + PAMU_PICS, pics);
#ifdef CONFIG_P4080_ERRATUM_PAMU3
		if (rev1)
			/* erratum PAMU 3-- do it twice */
			out32(reg + PAMU_PICS, pics);
#endif
		return -1;
	}

	snprintf(err.error, sizeof(err.error), "%s",
		 get_error_str(error_pamu, pamu_access_violation));
	pamu->avs1 = avs1;
	pamu->access_violation_addr = ((phys_addr_t) in32(reg + PAMU_AVAH)) << 32 |
				       in32(reg + PAMU_AVAL);
	pamu->avs2 = in32 (reg + PAMU_AVS2);

	/*FIXME : LIODN index not in PPAACT table*/
	assert(!(avs1 & PAMU_LAV_LIODN_NOT_IN_PPAACT));

	spin_lock(&pamu_error_lock);

	guest_t *guest = liodn_data[av_liodn].guest;
	if (guest) {
		pamu->lpid = guest->id;
		pamu->liodn_handle = liodn_data[av_liodn].pamu_handle->user.id;
		dt_node_t *node = get_dev_node(pamu->liodn_handle, guest);
		if (node)
			dt_get_path(NULL, node, err.gdev_tree_path,
				    sizeof(err.gdev_tree_path));

	}

	spin_unlock(&pamu_error_lock);

	pamu_disable_liodn(av_liodn);
	pamu_error_log(&err, guest);
	error_policy_action(&err, error_pamu,
			    pamu_err_policy[pamu_access_violation]);

	/* Clear the write one to clear bits in AVS1, mask out the LIODN */
	out32(reg + PAMU_AVS1, (avs1 & PAMU_AV_MASK));
	/* De-assert access violation pin */
	out32(reg + PAMU_PICS, pics);
#ifdef CONFIG_P4080_ERRATUM_PAMU3
	if (rev1)
		/* erratum PAMU 3 -- do it twice */
		out32(reg + PAMU_PICS, pics);
#endif
	return 0;
}

static int handle_ecc_error(pamu_ecc_err_reg_t *ecc_regs, dt_node_t *pamu_node)
{
	uint32_t val = in32(&ecc_regs->eccdet);
	uint32_t errattr, ctlval;
	hv_error_t err = { };
	pamu_error_t *pamu = &err.pamu;

	snprintf(err.domain, sizeof(err.domain), "%s", get_domain_str(error_pamu));
	dt_get_path(NULL, pamu_node, err.hdev_tree_path,
		    sizeof(err.hdev_tree_path));

	ctlval = pamu->eccctl = in32(&ecc_regs->eccctl);
	pamu->eccdis = in32(&ecc_regs->eccdis);
	pamu->eccinten = in32(&ecc_regs->eccinten);
	pamu->eccdet = val;
	errattr = in32(&ecc_regs->eccattr);
	if (errattr & PAMU_ECC_ERR_ATTR_VAL) {
		pamu->eccattr = errattr;
		pamu->eccaddr = (((phys_addr_t)in32(&ecc_regs->eccaddrhi)) << 32) |
					in32(&ecc_regs->eccaddrlo);
		pamu->eccdata = (((phys_addr_t)in32(&ecc_regs->eccdatahi)) << 32) |
					in32(&ecc_regs->eccdatalo);
	}

	if (val & PAMU_SB_ECC_ERR) {
		snprintf(err.error, sizeof(err.error), "%s",
			 get_error_str(error_pamu, pamu_single_bit_ecc));
		out32(&ecc_regs->eccctl, ctlval & ~PAMU_EECTL_CNT_MASK);
		error_policy_action(&err, error_pamu,
				    pamu_err_policy[pamu_single_bit_ecc]);
	}

	if (val & PAMU_MB_ECC_ERR) {
		snprintf(err.error, sizeof(err.error), "%s",
			 get_error_str(error_pamu, pamu_multi_bit_ecc));
		error_policy_action(&err, error_pamu,
				    pamu_err_policy[pamu_multi_bit_ecc]);
	}

	out32(&ecc_regs->eccattr, PAMU_ECC_ERR_ATTR_VAL);
	out32(&ecc_regs->eccdet, val);

	return 0;
}

static int pamu_av_isr(void *arg)
{
	device_t *dev = arg;
	void *reg_base = dev->regs[0].virt;
	phys_addr_t reg_size = dev->regs[0].size;
	unsigned long reg_off;
	int ret = -1;
	dt_node_t *pamu_node;

	pamu_node = to_container(dev, dt_node_t, dev);
	for (reg_off = 0; reg_off < reg_size; reg_off += PAMU_OFFSET) {
		void *reg = reg_base + reg_off;
		uint32_t pics;
		pamu_ecc_err_reg_t *ecc_regs;

		pics = in32(reg + PAMU_PICS);
		if (pics & PAMU_ACCESS_VIOLATION_STAT)
			ret = handle_access_violation(reg, pamu_node, pics);

		ecc_regs = (pamu_ecc_err_reg_t *)(reg + PAMU_EECTL);
		if (in32(&ecc_regs->eccdet))
			ret = handle_ecc_error(ecc_regs, pamu_node);
	}

	return ret;
}

static int pamu_probe(device_t *dev, const dev_compat_t *compat_id);

static const dev_compat_t pamu_compat[] = {
	{
		.compatible = "fsl,p4080-pamu"
	},
	{
		.compatible = "fsl,pamu"
	},
	{}

};

static driver_t __driver pamu = {
	.compatibles = pamu_compat,
	.probe = pamu_probe
};

#define PAMUBYPENR 0x604
static int pamu_probe(device_t *dev, const dev_compat_t *compat_id)
{
	int ret;
	void *vaddr;
	phys_addr_t size;
	unsigned long pamu_reg_base, pamu_reg_off;
	unsigned long pamumem_size;
	void *pamumem;
	dt_node_t *guts_node, *pamu_node;
	phys_addr_t guts_addr, guts_size;
	uint32_t *pamubypenr_ptr;
	interrupt_t *irq;
	uint8_t pamu_enable_ints = 0;
	uint32_t error_threshold = 0;

	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them.
	 */

	if (dev->num_regs < 1 || !dev->regs[0].virt) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			"%s: PAMU reg initialization failed\n", __func__);
		return ERR_INVALID;
	}

	pamu_node = to_container(dev, dt_node_t, dev);
	vaddr = dev->regs[0].virt;
	size = dev->regs[0].size;

	guts_node = get_guts_node();
	if (!guts_node) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: pamu present, but no guts node found\n", __func__);
		return ERR_UNHANDLED;
	}

	ret = dt_get_reg(guts_node, 0, &guts_addr, &guts_size);
	if (ret < 0) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: no guts reg found\n", __func__);
		return ERR_UNHANDLED;
	}

	pamumem_size = align(PPAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) +
			 align(SPAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) + OMT_SIZE;

	pamumem_size = 1 << ilog2_roundup(pamumem_size);
#ifndef CONFIG_WARM_REBOOT
	pamumem = alloc(pamumem_size, pamumem_size);
#else
	if (!pamu_mem_size) {
		pamumem = alloc(pamumem_size, pamumem_size);
	} else {
		void *tmp = pamu_mem_header;
		void *end = tmp + pamu_mem_size;
		if (end > tmp) {
			tmp += sizeof(pamu_hv_mem_t);
			tmp = (void *)align((uintptr_t)tmp, pamumem_size);
			pamumem = tmp;
			tmp += pamumem_size;
			if (tmp > end || tmp < pamumem)
				pamumem = 0;
		} else {
			pamumem = 0;
		}
	}
#endif
	if (!pamumem) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: Unable to "
				"allocate space %#lxB for PAMU tables.\n",
				__func__, pamumem_size);
		return ERR_NOMEM;
	}

	pamu_node->pma = alloc_type(pma_t);
	if (!pamu_node->pma) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: out of memory\n",
			 __func__);
		goto fail_mem;
	}

	pamu_node->pma->start = virt_to_phys(pamumem);
	pamu_node->pma->size = pamumem_size;

	pamubypenr_ptr = map(guts_addr + PAMUBYPENR, 4,
	                     TLB_MAS2_IO, TLB_MAS3_KDATA);

	if (dev->num_irqs >= 1 && dev->irqs[0]) {
		const char *policy;

		irq = dev->irqs[0];
		if (irq && irq->ops->register_irq) {
			for(int i = pamu_single_bit_ecc; i < PAMU_ERROR_COUNT; i++) {
				const char *policy = get_error_policy(error_pamu, i);

				pamu_err_policy[i] = policy;
				if (!strcmp(policy, "disable"))
					continue;

				pamu_enable_ints |= 1 << i;

				if (i == pamu_single_bit_ecc) {
					error_threshold = get_error_threshold(error_pamu, i);
				}
			}

			irq->ops->register_irq(irq, pamu_av_isr, dev, TYPE_MCHK);
		}
	}

	if (dev->num_irqs >= 2 && dev->irqs[1]) {
		irq = dev->irqs[1];
		if (irq && irq->ops->register_irq) {
			const char *policy = get_error_policy(error_pamu, pamu_operation);

			pamu_err_policy[pamu_operation] = policy;
			if (strcmp(policy, "disable"))
				pamu_enable_ints |= 1 << pamu_operation;

			irq->ops->register_irq(irq, pamu_error_isr, dev, TYPE_MCHK);
		}
	}

	/* In case of warm-reboot call the Libos driver to setup internal vars */
	ret = pamu_hw_init(vaddr, size, pamubypenr_ptr, pamumem, pamumem_size,
#ifndef CONFIG_WARM_REBOOT
			   0);
#else
			   warm_reboot);
#endif
	if (ret < 0)
		goto fail_pma;

	if (pamu_enable_ints) {
		register_error_dump_callback(error_pamu, dump_pamu_error);
		pamu_enable_interrupts(vaddr, pamu_enable_ints, error_threshold);
	}

	setup_pamu_law(pamu_node);

#ifdef CONFIG_WARM_REBOOT
	if (!warm_reboot)
#endif
		setup_omt();

	return 0;

fail_pma:
	free(pamu_node->pma);

fail_mem:
#ifdef CONFIG_WARM_REBOOT
	if (!pamu_mem_size)
#endif
		free(pamumem);
	return ERR_NOMEM;
}

#ifdef CONFIG_CLAIMABLE_DEVICES
/* At this point, nothing else should be popping errors from the source
 * queue (we can only claim one thing at a time from the same partition).
 * The producer side can still be active.  We shouldn't receive any
 * additional errors from this liodn, though, because we've already switched
 * ownership.
 */
static void migrate_access_violations(guest_t *guest, queue_t *src,
				      uint32_t oldhandle, uint32_t newhandle)
{
	hv_error_t err;

	size_t avail = queue_get_avail(src);
	assert(avail % sizeof(hv_error_t) == 0);

	for (size_t pos = 0; pos < avail; pos += sizeof(hv_error_t)) {
		queue_read_at(src, (uint8_t *)&err, pos, sizeof(hv_error_t));

		/* The only per-partition PAMU errors should be access
		 * violations.
		 */
		if (!strcmp(err.domain, get_domain_str(error_pamu)) &&
		    err.pamu.liodn_handle == oldhandle) {
			err.pamu.liodn_handle = newhandle;

			error_log(&guest->error_event_queue, &err,
			          &guest->error_log_prod_lock);
		}
	}
}

static int claim_dma(claim_action_t *action, dev_owner_t *owner,
		     dev_owner_t *prev)
{
	pamu_handle_t *ph = to_container(action, pamu_handle_t, claim_action);
	uint32_t liodn = ph->assigned_liodn;
	guest_t *oldguest = liodn_data[liodn].guest;
	pamu_handle_t *oldhandle = liodn_data[liodn].pamu_handle;
	uint32_t saved;

	saved = spin_lock_mchksave(&pamu_error_lock);

	liodn_data[liodn].pamu_handle = ph;
	liodn_data[liodn].guest = owner->guest;

	spin_unlock_mchksave(&pamu_error_lock, saved);

	/*
	 * PAMU maintains coherent copies of PAACT/SPAACT in it's lookup
	 * cache with snoopy invalidations, but in-flight DMA operations will
	 * be an issue with regard to re-configuration. In such a case the
	 * guest is responsible for quiescing i/o devices, and HV disables
	 * access to the device's PAMU entry till re-configuration is done.
	 */
#ifdef CONFIG_ERRATUM_PAMU_A_004510
	saved = spin_lock_intsave(&pamu_lock);
#endif

	int ret = hv_pamu_reconfig_liodn(owner->guest, liodn, owner->hwnode);

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	spin_unlock_intsave(&pamu_lock, saved);
#endif

	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: re-config of liodn failed (rc=%d)\n",
			 __func__, ret);
	}

	migrate_access_violations(owner->guest, &oldguest->error_event_queue,
	                          oldhandle->user.id, ph->user.id);

	return 0;
}
#endif

static dt_node_t *find_cfgnode(dt_node_t *hwnode, dev_owner_t *owner)
{
	guest_t *guest = owner->guest;
	dt_prop_t *prop;

	if (!dt_node_is_compatible(hwnode->parent, "fsl,qman-portal")) {
		return owner->cfgnode;  /* normal case */
	}

	/* If parent of hwnode is fsl,qman-portal it's a special case.
	 *
	 * DMA config for portal devices should be described in the
	 * children of the partition's portal-devices node.  This config
	 * is shared among all the partition's portals.
	 *
	 * For legacy transition, if portal-devices is not present,
	 * we use the first set of children of the qman device config
	 * node that we find.  Regardless of whether portal-devices is
	 * present, if any portal has child device config nodes, warn.
	 */
	list_for_each(&owner->cfgnode->children, i) {
		dt_node_t *config_child = to_container(i, dt_node_t, child_node);

		/* look for nodes with a "device" property */
		const char *s = dt_get_prop_string(config_child, "device");
		if (!s)
			continue;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_WARN, "%s: warning: "
			"%s/%s should go under portal-devices node\n",
			 __func__, owner->cfgnode->name, config_child->name);
	}

	if (!guest->portal_devs) {
		guest->portal_devs =
			dt_get_subnode(guest->partition, "portal-devices", 0);
		if (!guest->portal_devs)
			guest->portal_devs = owner->cfgnode;
	}

	/* iterate over all children of the config node */
	list_for_each(&owner->guest->portal_devs->children, i) {
		dt_node_t *config_child = to_container(i, dt_node_t, child_node);

		/* look for nodes with a "device" property */
		const char *s = dt_get_prop_string(config_child, "device");
		if (!s)
			continue;

		dt_node_t *devnode = dt_lookup_alias(hw_devtree, s);
		if (!devnode) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: missing device (%s) reference at %s\n",
			         __func__, s, config_child->name);
			continue;
		}
		uint32_t hw_phandle_cfg = dt_get_phandle(devnode, 0);

		prop = dt_get_prop(hwnode, "dev-handle", 0);
		if (prop) {
			if (prop->len != 4) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: missing/bad dev-handle on %s\n",
				         __func__, owner->hwnode->name);
				continue;
			}
			uint32_t hw_phandle = *(const uint32_t *)prop->data;

			if (hw_phandle == hw_phandle_cfg)
				return config_child; /* found match */
		}
	}

	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: no config node found for %s, check portal-devices\n",
	         __func__, owner->hwnode->name);

	return NULL;
}

typedef struct search_liodn_ctx {
	dt_node_t *cfgnode;
	uint32_t liodn_index;
} search_liodn_ctx_t;

static int search_liodn_index(dt_node_t *cfgnode, void *arg)
{
	search_liodn_ctx_t *ctx = arg;
	dt_prop_t *prop;

	prop = dt_get_prop(cfgnode, "liodn-index", 0);
	if (prop) {
		if (prop->len != 4) {
			ctx->cfgnode = NULL;
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: bad liodn-index on %s\n",
			         __func__, cfgnode->name);
			return 0;
		}
		if (ctx->liodn_index == *(const uint32_t *)prop->data) {
			ctx->cfgnode = cfgnode;
			return 1; /* found match */
		}
	}

	return 0;
}

static int configure_liodn(dt_node_t *hwnode, dev_owner_t *owner,
			   uint32_t liodn, dt_node_t *cfgnode)
{
	int ret = hv_pamu_config_liodn(owner->guest, liodn,
				       hwnode, cfgnode, owner->gnode);
	if (ret < 0) {
		if (ret == ERR_NOMEM)
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "paact table not found-- pamu may not be"
			         " assigned to hypervisor\n");
		else
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		                 "%s: config of liodn failed (rc=%d)\n",
		                 __func__, ret);
	}

	return ret;
}

static void pamu_reset_handle(handle_t *h, int partition_stop)
{
	pamu_handle_t *ph = h->pamu;
	uint32_t liodn = ph->assigned_liodn;
	guest_t *guest = liodn_data[liodn].guest;
	uint32_t saved;

	if (guest == h->handle_owner) {
		if (ph->no_dma_disable || guest->no_dma_disable)
			return;
		if (partition_stop && guest->defer_dma_disable)
			return;

#ifdef CONFIG_ERRATUM_PAMU_A_004510
		saved = spin_lock_intsave(&pamu_lock);
#endif

		pamu_disable_liodn(liodn);

#ifdef CONFIG_ERRATUM_PAMU_A_004510
		spin_unlock_intsave(&pamu_lock, saved);
#endif
	}
}

static handle_ops_t pamu_handle_ops = {
	.postreset = pamu_reset_handle,
};

void hcall_partition_stop_dma(trapframe_t *regs)
{
	guest_t *guest = handle_to_guest(regs->gpregs[3]);
	if (!guest) {
		regs->gpregs[3] = EV_EINVAL;
		return;
	}

	spin_lock_int(&guest->state_lock);

	if (guest->state != guest_stopped) {
		regs->gpregs[3] = EV_INVALID_STATE;
		goto out;
	}

	for (int i = 0; i < MAX_HANDLES; i++) {
		handle_t *h = guest->handles[i];

		if (h && h->ops == &pamu_handle_ops)
			pamu_reset_handle(h, 0);
	}

	regs->gpregs[3] = 0;

out:
	spin_unlock_int(&guest->state_lock);
}

int configure_dma(dt_node_t *hwnode, dev_owner_t *owner)
{
	dt_node_t *cfgnode;
	dt_prop_t *liodn_prop = NULL;
	const uint32_t *liodn;
	int liodn_cnt, i;
	int ret;
	uint32_t *dma_handles = NULL;
	pamu_handle_t *pamu_handle;
	claim_action_t *claim_action;
	int standby = get_claimable(owner) == claimable_standby;

	cfgnode = find_cfgnode(hwnode, owner);
	if (!cfgnode)
		return 0;

	/* Get the liodn property from the gnode, so that
	 * we honor its removal via node-update if this guest
	 * isn't supposed to own DMA for this device.
	 */
	liodn_prop = dt_get_prop(owner->gnode, "fsl,liodn", 0);
	if (!liodn_prop)
		return 0;  /* continue */

	if (liodn_prop->len & 3 || liodn_prop->len == 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: bad liodn on %s\n",
		         __func__, hwnode->name);
		return 0;
	}
	liodn = (const uint32_t *)liodn_prop->data;

	/* get a count of LIODNs */
	liodn_cnt = liodn_prop->len / 4;

	dma_handles = malloc(liodn_cnt * sizeof(uint32_t));
	if (!dma_handles)
		goto nomem;

	for (i = 0; i < liodn_cnt; i++) {
		search_liodn_ctx_t ctx;

		if (liodn[i] >= PAACE_NUMBER_ENTRIES) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: Invalid LIODN value %d \n", __func__, liodn[i]);
			free(dma_handles);
			return 0;
		}

		/* search for an liodn-index that matches this liodn */
		ctx.liodn_index = i;
		ctx.cfgnode = cfgnode;  /* default */
		dt_for_each_node(cfgnode, &ctx, search_liodn_index, NULL);

		if (standby) {
			if (ctx.cfgnode &&
			    dt_get_prop(ctx.cfgnode, "dma-window", 0)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
				         "%s: %s: warning: standby owners "
				         "should not have a DMA config\n",
				         __func__, cfgnode->name);
			}

			/* We want a separate handle for each partition */
			pamu_handle = NULL;
		} else {
			if (!ctx.cfgnode) { /* error */
				free(dma_handles);
				return 0;
			}

			if (configure_liodn(hwnode, owner, liodn[i],
			                    ctx.cfgnode)) {
				free(dma_handles);
				return 0;
			}

			/* Don't allocate a new handle if it's a QMan
			 * portal device that's already been configured.
			 */
			pamu_handle = liodn_data[liodn[i]].pamu_handle;
		}

		if (!pamu_handle) {
			pamu_handle = alloc_type(pamu_handle_t);
			if (!pamu_handle)
				goto nomem;

			pamu_handle->assigned_liodn = liodn[i];
			pamu_handle->user.pamu = pamu_handle;
			pamu_handle->user.ops = &pamu_handle_ops;

			if (dt_get_prop(ctx.cfgnode, "no-dma-disable", 0))
				pamu_handle->no_dma_disable = 1;

			ret = alloc_guest_handle(owner->guest,
						 &pamu_handle->user);
			if (ret < 0) {
				free(dma_handles);
				return ret;
			}
		} else {
			assert(pamu_handle->user.handle_owner == owner->guest);
		}

		dma_handles[i] = pamu_handle->user.id;

#ifdef CONFIG_CLAIMABLE_DEVICES
		pamu_handle->claim_action.claim = claim_dma;
		pamu_handle->claim_action.next = owner->claim_actions;
		owner->claim_actions = &pamu_handle->claim_action;
#endif

		if (!standby) {
			liodn_data[liodn[i]].pamu_handle = pamu_handle;
			mbar(1);

#ifdef CONFIG_WARM_REBOOT
			if (!warm_reboot)
#endif
				if (pamu_handle->no_dma_disable ||
				    owner->guest->no_dma_disable)
					pamu_enable_liodn(liodn[i]);
		}
	}

	if (dt_set_prop(owner->gnode, "fsl,hv-dma-handle", dma_handles,
				i * sizeof(uint32_t)) < 0)
		goto nomem;

	free(dma_handles);
	return 0;

nomem:
	free(dma_handles);
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		 "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}

static register_t hv_pamu_set_stash_target(unsigned int handle, phys_addr_t addr)
{
	guest_t *guest = get_gcpu()->guest;
	struct fh_dma_attr_stash user;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;
	uint32_t stash_id = 0;
	paace_t *ppaace;
	size_t len;
	uint32_t saved;
	int ret = 0;

	if (handle >= MAX_HANDLES || !guest->handles[handle])
		// Invalid handle
		return EV_EINVAL;

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle)
		// Not a handle for an LIODN
		return EV_EINVAL;

	liodn = pamu_handle->assigned_liodn;

	len = copy_from_gphys(get_gcpu()->guest->gphys, &user, addr,
		sizeof(struct fh_dma_attr_stash));
	if (len != sizeof(struct fh_dma_attr_stash))
		return EV_EFAULT;

	// A non-zero value means the user wants to enable stashing.
	// Otherwise, we leave stash_id at 0, which will disable stashing.
	if (user.cache) {
		dt_node_t *node;
		int pcpu;

		pcpu = vcpu_to_cpu(guest->cpulist, guest->cpulist_len, user.vcpu);
		if (pcpu < 0)
			return EV_EINVAL;

		node = get_cpu_node(hw_devtree, pcpu);
		stash_id = get_stash_id(node, user.cache);
		if (stash_id == ~(uint32_t)0)
			return EV_EINVAL;
	}

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	saved = spin_lock_intsave(&pamu_lock);
#endif

	ppaace = pamu_get_ppaace(liodn);
	if (ppaace) {
		/*
		 * Although the PAMU supports different stash targets for each
		 * subwindow, we're not going to support this feature.  All
		 * subwindows will be set to the same stash target
		 */
		set_bf(ppaace->impl_attr, PAACE_IA_CID, stash_id);

		/* The MW bit tells us if there are any SPAACEs  */
		if (get_bf(ppaace->addr_bitfields, PPAACE_AF_MW)) {
			unsigned int count =
				(1 << (1 + get_bf(ppaace->impl_attr, PAACE_IA_WCE))) - 1;

			for (unsigned i = 0; i < count; i++) {
				paace_t *spaace = pamu_get_spaace(ppaace->fspi, i);
				set_bf(spaace->impl_attr, PAACE_IA_CID, stash_id);
			}
		}

		// Remember the settings
		liodn_data[liodn].stash_vcpu = user.vcpu;
		liodn_data[liodn].stash_cache = user.cache;
	} else {
		// The LIODN has not been initialized yet
		ret = EV_EINVAL;
	}

#ifdef CONFIG_ERRATUM_PAMU_A_004510
	spin_unlock_intsave(&pamu_lock, saved);
#endif

	return ret;
}

static register_t hv_pamu_get_stash_target(unsigned int handle, phys_addr_t addr)
{
	guest_t *guest = get_gcpu()->guest;
	struct fh_dma_attr_stash user;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;
	size_t len;

	if (handle >= MAX_HANDLES || !guest->handles[handle])
		// Invalid handle
		return EV_EINVAL;

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle)
		// Not a handle for an LIODN
		return EV_EINVAL;

	liodn = pamu_handle->assigned_liodn;

	user.vcpu = liodn_data[liodn].stash_vcpu;
	user.cache = liodn_data[liodn].stash_cache;

	len = copy_to_gphys(get_gcpu()->guest->gphys, addr, &user,
		sizeof(struct fh_dma_attr_stash), 0);
	if (len != sizeof(struct fh_dma_attr_stash))
		return EV_EFAULT;

	return 0;
}

void hcall_dma_attr_set(trapframe_t *regs)
{
	phys_addr_t addr = ((phys_addr_t)regs->gpregs[5] << 32) | regs->gpregs[6];

	switch (regs->gpregs[4]) {
	case FSL_PAMU_ATTR_STASH:
		regs->gpregs[3] = hv_pamu_set_stash_target(regs->gpregs[3], addr);
		break;
	default:
		regs->gpregs[3] = EV_EINVAL;
	}
}

void hcall_dma_attr_get(trapframe_t *regs)
{
	phys_addr_t addr = ((phys_addr_t)regs->gpregs[5] << 32) | regs->gpregs[6];

	switch (regs->gpregs[4]) {
	case FSL_PAMU_ATTR_STASH:
		regs->gpregs[3] = hv_pamu_get_stash_target(regs->gpregs[3], addr);
		break;
	default:
		regs->gpregs[3] = EV_EINVAL;
	}
}
