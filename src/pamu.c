/** peripheral access management unit (PAMU) support
 *
 * @file
 *
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

#include <libos/pamu.h>
#include <libos/bitops.h>
#include <libos/percpu.h>
#include <libos/console.h>
#include <libos/alloc.h>
#include <libos/platform_error.h>

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

static const char *pamu_err_policy[PAMU_ERROR_COUNT];


/* mcheck-safe lock, used to ensure atomicity of reassignment */
static uint32_t pamu_error_lock;

static pamu_handle_t *liodn_to_handle[PAACE_NUMBER_ENTRIES];
static guest_t *liodn_to_guest[PAACE_NUMBER_ENTRIES];
static uint32_t pamu_lock;

static unsigned int map_addrspace_size_to_wse(phys_addr_t addrspace_size)
{
	assert(!(addrspace_size & (addrspace_size - 1)));

	/* window size is 2^(WSE+1) bytes */
	return count_lsb_zeroes(addrspace_size >> PAGE_SHIFT) + 11;
}

static unsigned int map_subwindow_cnt_to_wce(uint32_t subwindow_cnt)
{
	/* window count is 2^(WCE+1) bytes */
	return count_lsb_zeroes_32(subwindow_cnt) - 1;
}

static int is_subwindow_count_valid(int subwindow_cnt)
{
	if (subwindow_cnt <= 1 || subwindow_cnt > 16)
		return 0;
	if (subwindow_cnt & (subwindow_cnt - 1))
		return 0;
	return 1;
}

#define L1 1
#define L2 2
#define L3 3

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
static uint32_t get_stash_dest(uint32_t stash_dest, dt_node_t *hwnode)
{
	dt_prop_t *prop;
	dt_node_t *node;

	/* Fastpath, exit early if L3/CPC cache is target for stashing */
	if (stash_dest == L3) {
		node = dt_get_first_compatible(hw_devtree,
				"fsl,p4080-l3-cache-controller");
		if (node) {
			if (!cpcs_enabled()) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_WARN,
					"%s: %s not enabled\n",
					__func__, node->name);
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
		return ~(uint32_t)0;
	}

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

	/* find the hwnode that represents the cache */
	for (uint32_t cache_level = L1; cache_level <= L3; cache_level++) {
		if (stash_dest == cache_level) {
			prop = dt_get_prop(node, "cache-stash-id", 0);
			if (!prop || prop->len != 4) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: missing/bad cache-stash-id at %s \n",
				          __func__, node->name);
				return ~(uint32_t)0;
			}
			return *(const uint32_t *)prop->data;
		}

		prop = dt_get_prop(node, "next-level-cache", 0);
		if (!prop || prop->len != 4) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: can't find next-level-cache at %s \n",
			          __func__, node->name);
			return ~(uint32_t)0;  /* can't traverse any further */
		}

		/* advance to next node in cache hierarchy */
		node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
		if (!node) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: bad cpu phandle reference in %s \n",
			          __func__, hwnode->name);
			return ~(uint32_t)0;
		}
	}

	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: stash dest not found for %d on %s \n",
	          __func__, stash_dest, hwnode->name);
	return ~(uint32_t)0;
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
	return *(const uint32_t *)prop->data;
}

#define PEXIWBAR 0xDA8
#define PEXIWBEAR 0xDAC
#define PEXIWAR 0xDB0
#define PEXI_EN 0x80000000
#define PEXI_IWS 0x3F

static unsigned long setup_pcie_msi_subwin(guest_t *guest, dt_node_t *cfgnode,
                             dt_node_t *node, dt_node_t *gnode, uint64_t gaddr,
                             uint64_t *size)
{
	int ret;
	dt_prop_t *prop;
	uint32_t phandle;
	uint32_t msi_bank_addr;
	char buf[32];
	uint32_t reg[2];
	dt_prop_t *regprop;
	uint64_t msi_addr = 0;
	dt_node_t *msi_gnode, *msi_node;
	unsigned long rpn = ULONG_MAX;
	uint8_t *pci_ctrl = NULL;
	phys_addr_t pcie_addr, pcie_size;

	prop = dt_get_prop(node, "fsl,msi", 0);
	if (prop) {
		int i;

		phandle = *(const uint32_t *)prop->data;
		msi_gnode = dt_lookup_phandle(guest->devtree, phandle);
		msi_node = dt_lookup_phandle(hw_devtree, phandle);

		if (!msi_gnode)
			return ULONG_MAX;
		
		if (!msi_node ||
		    !dt_node_is_compatible(msi_node, "fsl,mpic-msi")) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: Bad fsl,msi phandle in %s\n",
			         __func__, node->name);
			return ULONG_MAX;
		}

		ret = dt_get_reg(msi_node, 0, &msi_addr, NULL);
		if (ret < 0) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: Could not get reg in %s\n",
			         __func__, msi_node->name);
			return ULONG_MAX;
		}

		msi_bank_addr = msi_addr & (PAGE_SIZE - 1);
		rpn = msi_addr >> PAGE_SHIFT;
		msi_addr = gaddr + msi_bank_addr;
		if (*size > PAGE_SIZE)
			*size = PAGE_SIZE;

		ret = snprintf(buf, sizeof(buf), "fsl,vmpic-msi");
		ret = dt_set_prop(msi_gnode, "compatible", buf, ret + 1);
		if (ret < 0)
			return ULONG_MAX;
		regprop = dt_get_prop(msi_gnode, "reg", 0);
		dt_delete_prop(regprop);

		reg[0] = msi_addr >> 32;
		reg[1] = msi_addr & 0xffffffff;

		// FIXME: This needs to be done via u-boot
		reg[1] += 0x140;

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
		pci_ctrl = map_phys(TEMPTLB1, pcie_addr, temp_mapping[0],
		                    &len, TLB_TSIZE_4K, TLB_MAS2_IO,
		                    TLB_MAS3_KDATA);
#else
/* Needed if PCIe virtualization is enabled */
		if (node->vf)
			pci_ctrl = node->vf->vaddr;
#endif
		if (!pci_ctrl || len < 0x1000) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: Couldn't map reg %llx (guest phys) of %s\n",
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
					((piwbear & 0xFFFFF) << 12 |
				          piwbar >> 20)  << 32) |
					(piwbar & 0xFFFFF) << 12;

				uint32_t inb_win_size = 1 <<
					((piwar & PEXI_IWS) + 1);

				if (msi_addr >= inb_win_addr &&
				    msi_addr <= inb_win_addr + inb_win_size - 1)
					break;
			}
		}

		tlb1_clear_entry(TEMPTLB1);

		if (i > 2) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: %s: msi-address 0x%llx outside inbound memory windows\n",
			         __func__, node->name, msi_addr);

			return ULONG_MAX;
		}
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
                            uint32_t subwindow_cnt,
                            ppaace_t *ppaace)
{
	unsigned long fspi = ppaace->fspi; /* re-use spaace(s) */
	phys_addr_t subwindow_size = primary_size / subwindow_cnt;
	int subwin;
	uint64_t gaddr = primary_base;
	uint64_t size = subwindow_size;
	list_t *ele = parent->children.next;

	for (subwin = 0;
	     subwin < subwindow_cnt;
	     ++subwin, gaddr += size, ele = ele->next) {
		unsigned long rpn;
		dt_node_t *node = to_container(ele, dt_node_t, child_node);

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

		if (subwin == 0) {
			unsigned long curr_rpn = ppaace->twbah << 20 |
					ppaace->twbal;
			if (curr_rpn == rpn)
				continue;
			ppaace->ap = PAACE_AP_PERMS_DENIED;
			lwsync();
			ppaace->atm = PAACE_ATM_WINDOW_XLATE;
			ppaace->twbah = rpn >> 20;
			ppaace->twbal = rpn & 0xfffff;
			lwsync();
			ppaace->ap = PAACE_AP_PERMS_ALL; /* FIXME read-only gpmas */
		} else {
			spaace_t *spaace = pamu_get_spaace(fspi, subwin - 1);
			unsigned long curr_rpn = spaace->twbah << 20 |
					spaace->twbal;
			if (curr_rpn == rpn)
				continue;
			spaace->ap = PAACE_AP_PERMS_DENIED;
			lwsync();
			spaace->atm = PAACE_ATM_WINDOW_XLATE;
			spaace->twbah = rpn >> 20;
			spaace->twbal = rpn & 0xfffff;
			lwsync();
			spaace->ap = PAACE_AP_PERMS_ALL; /* FIXME read-only gpmas */
		}
	}

	return 0;
}

int pamu_reconfig_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode)
{
	struct ppaace_t *ppaace;
	unsigned long rpn;
	dt_node_t *dma_window;
	phys_addr_t window_addr = ~(phys_addr_t)0;
	phys_addr_t window_size = 0;
	uint32_t subwindow_cnt = 0;
	int ret = 0;

	ppaace = pamu_get_ppaace(liodn);

	subwindow_cnt = 1 << (ppaace->wce + 1);
	window_addr = ((phys_addr_t) ppaace->wbah << 32) |
			ppaace->wbal;
	window_addr <<= PAGE_SHIFT;
	window_size = 1 << (ppaace->wse + 1);

	dma_window = hwnode->dma_window;
	if (!dma_window) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: bad dma-window phandle ref during reconfig\n",
		         __func__);
		return ERR_BADTREE;
	}

	if (ppaace->mw)
		ret = reconfig_subwins(guest, dma_window, liodn,
		                       window_addr, window_size, subwindow_cnt,
		                       ppaace);
	else {
		unsigned long curr_rpn = ppaace->twbah << 20 |
					ppaace->twbal;

		rpn = get_rpn(guest, window_addr >> PAGE_SHIFT,
		              window_size >> PAGE_SHIFT);
		if (rpn == ULONG_MAX)
			return ERR_NOTRANS;

		if (curr_rpn != rpn) {
			ppaace->ap = PAACE_AP_PERMS_DENIED;
			lwsync();
			ppaace->atm = PAACE_ATM_WINDOW_XLATE;
			ppaace->twbah = rpn >> 20;
			ppaace->twbal = rpn;
			lwsync();
			/* FIXME read-only gpmas */
			ppaace->ap = PAACE_AP_PERMS_ALL;
		}
	}

	return ret;
}
#endif

static int setup_subwins(guest_t *guest, dt_node_t *parent,
                         uint32_t liodn, phys_addr_t primary_base,
                         phys_addr_t primary_size,
                         uint32_t subwindow_cnt,
                         uint32_t omi, uint32_t stash_dest,
                         ppaace_t *ppaace, dt_node_t *cfgnode,
                         dt_node_t *hwnode, dt_node_t *gnode)
{
	unsigned long fspi;
	phys_addr_t subwindow_size = primary_size / subwindow_cnt;

	/* The first entry is in the primary PAACE instead */
	fspi = get_fspi_and_increment(subwindow_cnt - 1);
	if (fspi == ULONG_MAX) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: spaace indexes exhausted %s\n",
		         __func__, parent->name);
		return ERR_BUSY;
	}

	list_for_each(&parent->children, i) {
		dt_node_t *node = to_container(i, dt_node_t, child_node);
		dt_prop_t *prop;
		uint64_t gaddr, size;
		unsigned long rpn;
		int subwin, swse;

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

		if (!dt_get_prop(node, "pcie-msi-subwindow", 0))
			rpn = get_rpn(guest, gaddr >> PAGE_SHIFT,
				size >> PAGE_SHIFT);
		else
			rpn = setup_pcie_msi_subwin(guest, cfgnode, hwnode,
					gnode, gaddr, &size);

		if (rpn == ULONG_MAX)
			continue;

		swse = map_addrspace_size_to_wse(size);

		/* If we merge ppaace_t and spaace_t, we could
		 * simplify this a bit.
		 */
		if (subwin == 0) {
			ppaace->swse = swse;
			ppaace->atm = PAACE_ATM_WINDOW_XLATE;
			ppaace->twbah = rpn >> 20;
			ppaace->twbal = rpn & 0xfffff;
			ppaace->ap = PAACE_AP_PERMS_ALL; /* FIXME read-only gpmas */
		} else {
			spaace_t *spaace = pamu_get_spaace(fspi, subwin - 1);
			setup_default_xfer_to_host_spaace(spaace);

			spaace->swse = swse;
			spaace->atm = PAACE_ATM_WINDOW_XLATE;
			spaace->twbah = rpn >> 20;
			spaace->twbal = rpn & 0xfffff;
			spaace->ap = PAACE_AP_PERMS_ALL; /* FIXME read-only gpmas */
			spaace->liodn = liodn;

			if (~omi != 0) {
				spaace->otm = PAACE_OTM_INDEXED;
				spaace->op_encode.index_ot.omi = omi;
				spaace->impl_attr.cid = stash_dest;
			}

			lwsync();
			spaace->v = 1;
		}
	}

	ppaace->wce = map_subwindow_cnt_to_wce(subwindow_cnt);
	ppaace->mw = 1;
	ppaace->fspi = fspi;
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
int pamu_config_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode, dt_node_t *cfgnode, dt_node_t *gnode)
{
	struct ppaace_t *ppaace;
	unsigned long rpn;
	dt_prop_t *prop, *stash_prop;
	dt_prop_t *gaddr;
	dt_prop_t *size;
	dt_node_t *dma_window;
	phys_addr_t window_addr = ~(phys_addr_t)0;
	phys_addr_t window_size = 0;
	uint32_t stash_dest = ~(uint32_t)0;
	uint32_t omi = ~(uint32_t)0;
	uint32_t subwindow_cnt = 0;
	register_t saved;
	int ret;

	prop = dt_get_prop(cfgnode, "dma-window", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_WARN,
		         "%s: warning: missing dma-window at %s\n",
		         __func__, cfgnode->name);
		return 0;
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

	saved = spin_lock_intsave(&pamu_lock);
	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace) {
		spin_unlock_intsave(&pamu_lock, saved);
		return ERR_NOMEM;
	}

	if (ppaace->wse) {
		spin_unlock_intsave(&pamu_lock, saved);

		/* QMan portal devices have multiple nodes sharing
		 * an liodn.
		 */
		if (dt_node_is_compatible(hwnode->parent, "fsl,qman-portal"))
			return 0;

		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: liodn %d or device in use\n", __func__, liodn);
		return ERR_BUSY;
	}

	ppaace->wse = map_addrspace_size_to_wse(window_size);
	spin_unlock_intsave(&pamu_lock, saved);

	liodn_to_guest[liodn] = guest;

	setup_default_xfer_to_host_ppaace(ppaace);

	ppaace->wbah = window_addr >> (PAGE_SHIFT + 20);
	ppaace->wbal = (window_addr >> PAGE_SHIFT) & 0xfffff;

	/* set up operation mapping if it's configured */
	prop = dt_get_prop(cfgnode, "operation-mapping", 0);
	if (prop) {
		if (prop->len == 4) {
			omi = *(const uint32_t *)prop->data;
			if (omi <= OMI_MAX) {
				ppaace->otm = PAACE_OTM_INDEXED;
				ppaace->op_encode.index_ot.omi = omi;
			} else {
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

	/* configure stash id */
	stash_prop = dt_get_prop(cfgnode, "stash-dest", 0);
	if (stash_prop && stash_prop->len == 4) {
		stash_dest = get_stash_dest( *(const uint32_t *)
				stash_prop->data , hwnode);

		if (~stash_dest != 0)
			ppaace->impl_attr.cid = stash_dest;
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

		ppaace->domain_attr.to_host.snpid =
				get_snoop_id(hwnode, guest) + 1;
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

		ret = setup_subwins(guest, dma_window, liodn, window_addr,
		                    window_size, subwindow_cnt, omi,
		                    stash_dest, ppaace, cfgnode, hwnode, gnode);
		if (ret < 0)
			return ret;
	} else {
		/* No subwindows */
		rpn = get_rpn(guest, window_addr >> PAGE_SHIFT,
		              window_size >> PAGE_SHIFT);
		if (rpn == ULONG_MAX)
			return ERR_NOTRANS;

		ppaace->atm = PAACE_ATM_WINDOW_XLATE;
		ppaace->twbah = rpn >> 20;
		ppaace->twbal = rpn;
		ppaace->ap = PAACE_AP_PERMS_ALL; /* FIXME read-only gpmas */
	}

	lwsync();

	/* PAACE is invalid, validated by enable hcall, unless
	 * the user has requested dma-continues-after-partition-stop
	 */
	return 0;
}

static void pamu_enable_liodn_raw(unsigned int liodn)
{
	pamu_get_ppaace(liodn)->v = 1;
	sync();
}

int pamu_enable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		return EV_EINVAL;
	}

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle) {
		return EV_EINVAL;
	}

	liodn = pamu_handle->assigned_liodn;

#ifdef CONFIG_CLAIMABLE_DEVICES
	if (liodn_to_guest[liodn] != guest)
		return EV_INVALID_STATE;
#endif

	pamu_enable_liodn_raw(liodn);
	return 0;
}

static void pamu_disable_liodn_raw(unsigned int liodn)
{
	pamu_get_ppaace(liodn)->v = 0;
	sync();

	/*
	 * FIXME: Need to wait or synchronize with any pending DMA flush
	 * operations on disabled liodns, as once we disable PAMU
	 * window here any pending DMA flushes will fail.
	 *
	 * Also, is there any way to wait until any transactions which
	 * the PAMU has already authorized complete before continuing?
	 */
}

int pamu_disable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		return EV_EINVAL;
	}

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle) {
		return EV_EINVAL;
	}

	liodn = pamu_handle->assigned_liodn;

#ifdef CONFIG_CLAIMABLE_DEVICES
	if (liodn_to_guest[liodn] != guest)
		return EV_INVALID_STATE;
#endif

	pamu_disable_liodn_raw(liodn);
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

		pics = in32((uint32_t *)(reg + PAMU_PICS));
		if (pics & PAMU_OPERATION_ERROR_INT_STAT) {
			strncpy(err.domain, get_domain_str(error_pamu), sizeof(err.domain));
			strncpy(err.error, get_error_str(error_pamu, pamu_operation),
				sizeof(err.error));
			dt_get_path(NULL, pamu_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));
			poes1 = in32((uint32_t *)(reg + PAMU_POES1));
			if (poes1 & PAMU_POES1_POED) {
				err.pamu.poes1 = poes1;
				err.pamu.poeaddr = (((phys_addr_t)in32((uint32_t *)(reg + PAMU_POEAH))) << 32) |
							in32((uint32_t *)(reg + PAMU_POEAL));
			}
			error_policy_action(&err, error_pamu, pamu_err_policy[pamu_operation]);
			out32((uint32_t *)(reg + PAMU_POES1), PAMU_POES1_POED);
			out32((uint32_t *)(reg + PAMU_PICS), pics);
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
					sizeof(dt_node_t *), node_found_callback, &node);
	if (ret)
		return node;

	return NULL;
}

static int handle_access_violation(void *reg, dt_node_t *pamu_node, uint32_t pics)
{
	uint32_t av_liodn, avs1;
	hv_error_t err = { };
	pamu_error_t *pamu = &err.pamu;

	strncpy(err.domain, get_domain_str(error_pamu), sizeof(err.domain));
	dt_get_path(NULL, pamu_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));

	avs1 = in32 ((uint32_t *) (reg + PAMU_AVS1));
	av_liodn = avs1 >> PAMU_AVS1_LIODN_SHIFT;
	ppaace_t *ppaace = pamu_get_ppaace(av_liodn);
	/* We may get access violations for invalid LIODNs, just ignore them */
	if (!ppaace->v)
		return -1;

	strncpy(err.error, get_error_str(error_pamu, pamu_access_violation),
			sizeof(err.error));
	pamu->avs1 = avs1;
	pamu->access_violation_addr = ((phys_addr_t) in32 ((uint32_t *) (reg + PAMU_AVAH)))
						 << 32 | in32 ((uint32_t *) (reg + PAMU_AVAL));
	pamu->avs2 = in32 ((uint32_t *) (reg + PAMU_AVS2));

	/*FIXME : LIODN index not in PPAACT table*/
	assert(!(avs1 & PAMU_LAV_LIODN_NOT_IN_PPAACT));

	spin_lock(&pamu_error_lock);

	guest_t *guest = liodn_to_guest[av_liodn];
	if (guest) {
		pamu->lpid = guest->lpid;
		pamu->liodn_handle = liodn_to_handle[av_liodn]->user.id;
		dt_node_t *node = get_dev_node(pamu->liodn_handle, guest);
		if (node)
			dt_get_path(NULL, node, err.gdev_tree_path, sizeof(err.gdev_tree_path));

	}

	spin_unlock(&pamu_error_lock);

	ppaace->v = 0;
	pamu_error_log(&err, guest);
	error_policy_action(&err, error_pamu, pamu_err_policy[pamu_access_violation]);

	/* Clear the write one to clear bits in AVS1, mask out the LIODN */
	out32((uint32_t *) (reg + PAMU_AVS1), (avs1 & PAMU_AV_MASK));
	/* De-assert access violation pin */
	out32((uint32_t *)(reg + PAMU_PICS), pics);

#ifdef CONFIG_P4080_ERRATUM_PAMU3
	/* erratum -- do it twice */
	out32((uint32_t *)(reg + PAMU_PICS), pics);
#endif

	return 0;
}

static int handle_ecc_error(pamu_ecc_err_reg_t *ecc_regs, dt_node_t *pamu_node)
{
	uint32_t val = in32(&ecc_regs->eccdet);
	uint32_t errattr, ctlval;
	hv_error_t err = { };
	pamu_error_t *pamu = &err.pamu;

	strncpy(err.domain, get_domain_str(error_pamu), sizeof(err.domain));
	dt_get_path(NULL, pamu_node, err.hdev_tree_path, sizeof(err.hdev_tree_path));

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
		strncpy(err.error, get_error_str(error_pamu, pamu_single_bit_ecc),
				sizeof(err.error));
		out32(&ecc_regs->eccctl, ctlval & ~PAMU_EECTL_CNT_MASK);
		error_policy_action(&err, error_pamu, pamu_err_policy[pamu_single_bit_ecc]);
	}

	if (val & PAMU_MB_ECC_ERR) {
		strncpy(err.error, get_error_str(error_pamu, pamu_multi_bit_ecc),
				sizeof(err.error));
		error_policy_action(&err, error_pamu, pamu_err_policy[pamu_multi_bit_ecc]);
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

		pics = in32((uint32_t *)(reg + PAMU_PICS));
		if (pics & PAMU_ACCESS_VIOLATION_STAT)
			ret = handle_access_violation(reg, pamu_node, pics);

		ecc_regs = (pamu_ecc_err_reg_t *)(reg + PAMU_EECTL);
		if (in32(&ecc_regs->eccdet))
			ret = handle_ecc_error(ecc_regs, pamu_node);
	}

	return ret;
}

static int pamu_probe(driver_t *drv, device_t *dev);

static driver_t __driver pamu = {
	.compatible = "fsl,p4080-pamu",
	.probe = pamu_probe
};

#define PAMUBYPENR 0x604
static int pamu_probe(driver_t *drv, device_t *dev)
{
	int ret;
	phys_addr_t addr, size;
	unsigned long pamu_reg_base, pamu_reg_off;
	unsigned long pamumem_size;
	void *pamumem;
	dt_node_t *guts_node, *pamu_node;
	phys_addr_t guts_addr, guts_size;
	uint32_t pamubypenr, pamu_counter, *pamubypenr_ptr;
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
	addr = dev->regs[0].start;
	size = dev->regs[0].size;

	guts_node = dt_get_first_compatible(hw_devtree, "fsl,qoriq-device-config-1.0");
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

	pamumem_size = align(PAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) +
			 align(SPAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) + OMT_SIZE;

	pamumem_size = 1 << ilog2_roundup(pamumem_size);
	pamumem = alloc(pamumem_size, pamumem_size);
	if (!pamumem) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: Unable to allocate space for PAMU tables.\n",
				__func__);
		return ERR_NOMEM;
	}

	pamu_node->pma = alloc_type(pma_t);
	if (!pamu_node->pma) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: out of memory\n", __func__);
		goto fail_mem;
	}

	pamu_node->pma->start = virt_to_phys(pamumem);
	pamu_node->pma->size = pamumem_size;

	pamubypenr_ptr = map(guts_addr + PAMUBYPENR, 4,
	                     TLB_MAS2_IO, TLB_MAS3_KDATA);
	pamubypenr = in32(pamubypenr_ptr);


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

	if (pamu_enable_ints)
		register_error_dump_callback(error_pamu, dump_pamu_error);

	for (pamu_reg_off = 0, pamu_counter = 0x80000000; pamu_reg_off < size;
	     pamu_reg_off += PAMU_OFFSET, pamu_counter >>= 1) {

		pamu_reg_base = (unsigned long) addr + pamu_reg_off;
		ret = pamu_hw_init(pamu_reg_base - CCSRBAR_PA, pamu_reg_off,
					pamumem, pamumem_size, pamu_enable_ints,
					error_threshold);
		if (ret < 0) {
			/* This can only fail for the first instance due to
			 * memory alignment issues, hence this failure
			 * implies global pamu init failure and let all
			 * PAMU(s) remain in bypass mode.
			 */
			goto fail_pma;
		}

		/* Disable PAMU bypass for this PAMU */
		pamubypenr &= ~pamu_counter;
	}

	setup_pamu_law(pamu_node);
	setup_omt();

	/* Enable all relevant PAMU(s) */
	out32(pamubypenr_ptr, pamubypenr);
	return 0;

fail_pma:
	free(pamu_node->pma);

fail_mem:
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
	guest_t *oldguest = liodn_to_guest[liodn];
	pamu_handle_t *oldhandle = liodn_to_handle[liodn];
	uint32_t saved;
	
	saved = spin_lock_mchksave(&pamu_error_lock);

	liodn_to_handle[liodn] = ph;
	liodn_to_guest[liodn] = owner->guest;

	spin_unlock_mchksave(&pamu_error_lock, saved);

	/*
	 * PAMU maintains coherent copies of PAACT/SPAACT in it's lookup
	 * cache with snoopy invalidations, but in-flight DMA operations will
	 * be an issue with regard to re-configuration. In such a case the
	 * guest is responsible for quiescing i/o devices, and HV disables
	 * access to the device's PAMU entry till re-configuration is done.
	 */

	int ret = pamu_reconfig_liodn(owner->guest, liodn, owner->hwnode);
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
	int ret = pamu_config_liodn(owner->guest, liodn,
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
	guest_t *guest = liodn_to_guest[liodn];

	if (guest == h->handle_owner) {
		if (ph->no_dma_disable || guest->no_dma_disable)
			return;
		if (partition_stop && guest->defer_dma_disable)
			return;
	
		pamu_disable_liodn_raw(liodn);
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

	/* get a count of liodns */
	liodn_cnt = liodn_prop->len / 4;

	dma_handles = malloc(liodn_cnt * sizeof(uint32_t));
	if (!dma_handles)
		goto nomem;

	for (i = 0; i < liodn_cnt; i++) {
		search_liodn_ctx_t ctx;

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
			pamu_handle = liodn_to_handle[liodn[i]];
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
			liodn_to_handle[liodn[i]] = pamu_handle;
			mbar(1);

			if (pamu_handle->no_dma_disable ||
			    owner->guest->no_dma_disable)
				pamu_enable_liodn_raw(liodn[i]);
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
