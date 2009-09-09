/** peripheral access management unit (PAMU) support
 *
 * @file
 *
 */

/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <limits.h>
#include <error_log.h>

uint32_t liodn_to_handle[PAACE_NUMBER_ENTRIES];
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
	return count_lsb_zeroes(subwindow_cnt) - 1;
}

static int is_subwindow_count_valid(int subwindow_cnt)
{
	if (subwindow_cnt <= 1 || subwindow_cnt > 16)
		return 0;
	if (subwindow_cnt & (subwindow_cnt - 1))
		return 0;
	return 1;
}

/*
 * Given a guest physical page number and size, return
 * the real (true physical) page number
 *
 * return values
 *    ULONG_MAX on failure
 *    rpn value on success
 */
static unsigned long get_rpn(guest_t *guest, unsigned long grpn,
                             unsigned long pages)
{
	unsigned long attr;
	unsigned long start_rpn = ULONG_MAX;
	unsigned long cur_pages;
	unsigned long next_rpn = ULONG_MAX;
	unsigned long end = grpn + pages;

	while (grpn < end) {
		/*
		 * Need to iterate over vptbl_xlate(), as it returns a single
		 * mapping upto max. TLB page size on each call.
		 */
		unsigned long rpn = vptbl_xlate(guest->gphys, grpn, &attr,
		                                PTE_PHYS_LEVELS, 1);
		if (!(attr & PTE_DMA)) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: dma-window has unmapped guest address at 0x%llx.\n",
			         __func__, (unsigned long long)grpn << PAGE_SHIFT);
			return ULONG_MAX;
		}

		if (start_rpn == ULONG_MAX) {
			start_rpn = rpn;
		} else if (rpn != next_rpn) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: dma-window has discontiguity at guest address 0x%llx.\n",
			         __func__, (unsigned long long)grpn << PAGE_SHIFT);
			return ULONG_MAX;
		}

		if (!(attr & (PTE_SW | PTE_UW))) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: dma-window not writeable at guest address 0x%llx\n",
			         __func__, (unsigned long long)grpn << PAGE_SHIFT);
			return ULONG_MAX;
		}

		cur_pages = tsize_to_pages(attr >> PTE_SIZE_SHIFT);
		grpn += cur_pages;
		next_rpn = rpn + cur_pages;
	}

	return start_rpn;
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
                             dt_node_t *node, uint64_t gaddr,
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
	dt_node_t *msi_gnode = NULL;
	unsigned long rpn = ULONG_MAX;
	uint8_t *pci_ctrl;
	phys_addr_t pcie_addr, pcie_size;

	/*
	 * pcie controller hwnode properties would have been passed-thru
	 * to the guest devnode, hence look at pcie controller gnode directly.
	 */

	prop = dt_get_prop(node, "fsl,msi", 0);
	if (prop) {
		phandle = *(const uint32_t *)prop->data;
		msi_gnode = dt_lookup_phandle(guest->devtree, phandle);

		if (!msi_gnode ||
			!dt_node_is_compatible(msi_gnode, "fsl,mpic-msi"))
			return ULONG_MAX;

		dt_get_reg(msi_gnode, 0, &msi_addr, NULL);
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

		ret = dt_set_prop(node, "msi-address-64", reg, rootnaddr * 4);
		if (ret < 0)
			return ULONG_MAX;
		dt_set_prop(node, "fsl,hv-msi", msi_gnode, sizeof(uint32_t));
		ret = dt_get_reg(node, 0, &pcie_addr, &pcie_size);
		if (ret < 0) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"%s: no reg found\n", __func__);
			return ULONG_MAX;
		}

		pci_ctrl = map(pcie_addr, pcie_size,
				TLB_MAS2_IO, TLB_MAS3_KERN);
		if (!pci_ctrl)
			return ULONG_MAX;

		for (int i=0; i <= 2; i++) {
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

				if (msi_addr < inb_win_addr ||
					msi_addr >
					(inb_win_addr + inb_win_size)) {

					printf("WARNING: msi-address 0x%llx outside %s inbound memory window range %llx - %llx\n", msi_addr,
						node->name, inb_win_addr,
						inb_win_addr +
						inb_win_size);
					break;
				}
			}
		}
	}

	return rpn;
}

#define MAX_SUBWIN_CNT 16

static int setup_subwins(guest_t *guest, dt_node_t *parent,
                         uint32_t liodn, phys_addr_t primary_base,
                         phys_addr_t primary_size,
                         uint32_t subwindow_cnt,
                         uint32_t omi, uint32_t stash_dest,
                         ppaace_t *ppaace, dt_node_t *cfgnode,
                         dt_node_t *hwnode)
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
					gaddr, &size);

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
int pamu_config_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode, dt_node_t *cfgnode)
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
				"%s: bad subwindow-count length %u in %s\n",
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
		                    stash_dest, ppaace, cfgnode, hwnode);
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

	/* PAACE is invalid, validated by enable hcall */
	ppaace->v = 1; // FIXME: for right now we are leaving PAACE
	               // entries enabled by default.

	return 0;
}

int pamu_enable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	struct ppaace_t *current_ppaace;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		return -1;
	}

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle) {
		return -1;
	}

	liodn = pamu_handle->assigned_liodn;

	current_ppaace = pamu_get_ppaace(liodn);
	current_ppaace->v = 1;

	return 0;
}

int pamu_disable_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	struct ppaace_t *current_ppaace;
	pamu_handle_t *pamu_handle;
	unsigned long liodn;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		return -1;
	}

	pamu_handle = guest->handles[handle]->pamu;
	if (!pamu_handle) {
		return -1;
	}

	liodn = pamu_handle->assigned_liodn;

	current_ppaace = pamu_get_ppaace(liodn);
	current_ppaace->v = 0;

	/*
	 * FIXME : Need to wait or synchronize with any pending DMA flush
	 * operations on disabled liodn's, as once we disable PAMU
	 * window here any pending DMA flushes will fail.
	 */

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

static void pamu_error_log(error_info_t *err, guest_t *guest)
{
	/* Access violations go to per guest and the global error queue*/
	if (guest)
		error_log(&guest->error_event_queue, err, &guest->error_log_prod_lock);

	if (error_manager_guest)
		error_log(&global_event_queue, err, &global_event_prod_lock);
}

static int pamu_av_isr(void *arg)
{
	device_t *dev = arg;
	void *reg_base = dev->regs[0].virt;
	phys_addr_t reg_size = dev->regs[0].size;
	unsigned long reg_off;
	int ret = -1;
	error_info_t err;
	pamu_av_error_t *av;
	uint32_t av_liodn, avs1;

	for (reg_off = 0; reg_off < reg_size; reg_off += PAMU_OFFSET) {
		void *reg = reg_base + reg_off;

		uint32_t pics = in32((uint32_t *)(reg + PAMU_PICS));
		if (pics & PAMU_ACCESS_VIOLATION_STAT) {
			memset(&err, 0, sizeof(error_info_t));
			err.error_code = ERROR_PAMU_AV;
			av = &err.regs.av_err;
			av->avah = in32 ((uint32_t *) (reg + PAMU_AVAH));
			av->aval = in32 ((uint32_t *) (reg + PAMU_AVAL));
			avs1 = in32 ((uint32_t *) (reg + PAMU_AVS1));
			av->avs1 = avs1;
			av->avs2 = in32 ((uint32_t *) (reg + PAMU_AVS2));
			av_liodn = avs1 >> PAMU_AVS1_LIODN_SHIFT;
			printlog(LOGTYPE_PAMU, LOGLEVEL_DEBUG,
					"PAMU access violation on PAMU#%ld, liodn = %x\n",
					 reg_off / PAMU_OFFSET, av_liodn);
			printlog(LOGTYPE_PAMU, LOGLEVEL_DEBUG,
					"PAMU access violation avs1 = %x, avs2 = %x, avah = %x, aval = %x\n",
					 av->avs1, av->avs2, av->avah, av->aval);

			/*FIXME : LIODN index not in PPAACT table*/
			assert(!(avs1 & PAMU_LAV_LIODN_NOT_IN_PPAACT));

			guest_t *guest = liodn_to_guest[av_liodn];
			if (guest) {
				av->lpid = guest->lpid;
				av->handle = liodn_to_handle[av_liodn];
			}

			pamu_error_log(&err, guest);

			ppaace_t *ppaace = pamu_get_ppaace(av_liodn);
			ppaace->v = 0;

			/* Clear the write one to clear bits in AVS1, mask out the LIODN */
			out32((uint32_t *) (reg + PAMU_AVS1), (avs1 & PAMU_AV_MASK));
			/* De-assert access violation pin */
			out32((uint32_t *)(reg + PAMU_PICS), pics);

#ifdef CONFIG_P4080_ERRATUM_PAMU3
			/* erratum -- do it twice */
			out32((uint32_t *)(reg + PAMU_PICS), pics);
#endif

			ret = 0;
		}
	}

	return ret;
}

#define PAMUBYPENR 0x604
void pamu_global_init(void)
{
	int ret;
	phys_addr_t addr, size;
	unsigned long pamu_reg_base, pamu_reg_off;
	unsigned long pamumem_size;
	void *pamumem;
	dt_node_t *guts_node, *pamu_node;
	phys_addr_t guts_addr, guts_size;
	uint32_t pamubypenr, pamu_counter, *pamubypenr_ptr;
	device_t *dev;

	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them.
	 */

	/* find the pamu node */
	pamu_node = dt_get_first_compatible(hw_devtree, "fsl,p4080-pamu");
	if (!pamu_node) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_DEBUG,
		         "%s: warning: no pamu node found\n", __func__);
		return;
	}

	dev = &pamu_node->dev;

	ret = dt_get_reg(pamu_node, 0, &addr, &size);
	if (ret < 0) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: no pamu reg found\n", __func__);
		return;
	}

	guts_node = dt_get_first_compatible(hw_devtree, "fsl,qoriq-device-config-1.0");
	if (!guts_node) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: pamu present, but no guts node found\n", __func__);
		return;
	}

	ret = dt_get_reg(guts_node, 0, &guts_addr, &guts_size);
	if (ret < 0) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: no guts reg found\n", __func__);
		return;
	}

	pamumem_size = align(PAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) +
			 align(SPAACT_SIZE + 1, PAMU_TABLE_ALIGNMENT) + OMT_SIZE;

	pamumem_size = 1 << ilog2_roundup(pamumem_size);
	pamumem = alloc(pamumem_size, pamumem_size);
	if (!pamumem) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: Unable to allocate space for PAMU tables.\n",
				__func__);
		return;
	}

	pamu_node->pma = alloc_type(pma_t);
	if (!pamu_node->pma) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, "%s: out of memory\n", __func__);
		goto fail_mem;
	}

	pamu_node->pma->start = virt_to_phys(pamumem);
	pamu_node->pma->size = pamumem_size;

	pamubypenr_ptr = map(guts_addr + PAMUBYPENR, 4,
	                     TLB_MAS2_IO, TLB_MAS3_KERN);
	pamubypenr = in32(pamubypenr_ptr);

	for (pamu_reg_off = 0, pamu_counter = 0x80000000; pamu_reg_off < size;
	     pamu_reg_off += PAMU_OFFSET, pamu_counter >>= 1) {

		pamu_reg_base = (unsigned long) addr + pamu_reg_off;
		ret = pamu_hw_init(pamu_reg_base - CCSRBAR_PA, pamu_reg_off,
					pamumem, pamumem_size);
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

	ret = setup_pamu_law(pamu_node);
	if (ret < 0)
		goto fail_pma;

	setup_omt();

	if (dev->num_irqs >= 1) {
		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq)
			irq->ops->register_irq(irq, pamu_av_isr, dev, TYPE_MCHK);
	}

	/* Enable all relevant PAMU(s) */
	out32(pamubypenr_ptr, pamubypenr);

	for (int i = 0; i < PAACE_NUMBER_ENTRIES; i++)
		liodn_to_handle[i] = -1;

	return;

fail_pma:
	free(pamu_node->pma);

fail_mem:
	free(pamumem);
}
