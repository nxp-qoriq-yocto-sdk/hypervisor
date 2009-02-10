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

#include <pamu.h>
#include <percpu.h>
#include <errors.h>
#include <devtree.h>
#include <paging.h>
#include <limits.h>

static unsigned int map_addrspace_size_to_wse(unsigned long addrspace_size)
{
	if (addrspace_size & (addrspace_size - 1)) {
		addrspace_size = 1 << (LONG_BITS - 1 -
				(count_msb_zeroes(addrspace_size)));

		printlog(LOGTYPE_PAMU, LOGLEVEL_NORMAL,
			"%s: dma address space size not power of 2 (0x%lx)\n", __func__, addrspace_size);
	}

	/* window size is 2^(WSE+1) bytes */
	return count_lsb_zeroes(addrspace_size >> PAGE_SHIFT) + 11;
}

static unsigned int map_subwindow_cnt_to_wce(uint32_t subwindow_cnt)
{
	/* window count is 2^(WCE+1) bytes */
	return count_lsb_zeroes(subwindow_cnt) - 1;
}

static inline int is_primary_window_valid(phys_addr_t base, phys_addr_t size)
{
	return !(base & (size - 1));
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
	phys_addr_t size)
{
	unsigned long attr;
	unsigned long mach_phys_contig_size = 0;
	unsigned long start_rpn = ULONG_MAX;
	unsigned long size_pages;
	unsigned long next_rpn = ULONG_MAX;

	while (1) {

		/*
		 * Need to iterate over vptbl_xlate(), as it returns a single
		 * mapping upto max. TLB page size on each call.
		 */
		unsigned long rpn = vptbl_xlate(guest->gphys, grpn, &attr,
		                                PTE_PHYS_LEVELS, 1);
		if (!(attr & PTE_DMA)) {
			if (start_rpn == ULONG_MAX) {
				printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
						"%s: invalid rpn\n", __func__);
				return ULONG_MAX;
			} else {
				break;
			}
		}

		if (start_rpn == ULONG_MAX) {
			start_rpn = rpn;
		} else if (rpn != next_rpn) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_DEBUG,
				"%s: rpn = 0x%lx, size = 0x%lx\n",
				__func__, start_rpn, mach_phys_contig_size);
			break;
		}

		if (!(attr & (PTE_SW | PTE_UW))) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				"dma-window not writeable!\n");
			return ULONG_MAX;
		}

		size_pages = tsize_to_pages(attr >> PTE_SIZE_SHIFT);
		mach_phys_contig_size += (size_pages << PAGE_SHIFT);
		grpn += size_pages;
		next_rpn = rpn + size_pages;
	}

	if (size > mach_phys_contig_size) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: dma-window size error\n", __func__);
		return ULONG_MAX;
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
		return ULONG_MAX ;  /* if no cpu-phandle assume that this is
			      not a per-cpu portal */

	node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
	if (!node) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad cpu phandle reference in %s \n",
		          __func__, hwnode->name);
		return ULONG_MAX;
	}

	/* find the hwnode that represents the cache */
	for (int cache_level = L1; cache_level <= L3; cache_level++) {
		if (stash_dest == cache_level) {
			prop = dt_get_prop(node, "cache-stash-id", 0);
			if (!prop || prop->len != 4) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: missing/bad cache-stash-id at %s \n",
				          __func__, node->name);
				return ULONG_MAX;
			}
			return *(const uint32_t *)prop->data;
		}

		prop = dt_get_prop(node, "next-level-cache", 0);
		if (!prop || prop->len != 4) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: can't find next-level-cache at %s \n",
			          __func__, node->name);
			return ULONG_MAX;  /* can't traverse any further */
		}

		/* advance to next node in cache hierarchy */
		node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
		if (!node) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: bad cpu phandle reference in %s \n",
			          __func__, hwnode->name);
			return ULONG_MAX;
		}
	}

	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: stash dest not found for %d on %s \n",
	          __func__, stash_dest, hwnode->name);
	return ULONG_MAX;
}

static uint32_t get_snoop_id(dt_node_t *gnode, guest_t *guest)
{
	dt_prop_t *prop;
	dt_node_t *node;

	prop = dt_get_prop(gnode, "cpu-handle", 0);
	if (!prop || prop->len != 4)
		return ULONG_MAX;

	node = dt_lookup_phandle(hw_devtree, *(const uint32_t *)prop->data);
	if (!node) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad cpu phandle reference in %s \n",
		          __func__, gnode->name);
		return ULONG_MAX;
	}
	prop = dt_get_prop(node, "reg", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: bad reg in cpu node %s \n",
		          __func__, node->name);
		return ULONG_MAX;
	}
	return *(const uint32_t *)prop->data;
}

#define MAX_SUBWIN_CNT 16

int configure_dma_sub_windows(guest_t *guest, dt_node_t *dma_window,
	uint32_t liodn, uint64_t primary_window_base_addr,
	uint32_t subwindow_cnt, phys_addr_t subwindow_size,
	uint32_t omi, uint32_t stash_dest,
	unsigned int *first_subwin_swse, unsigned long *fspi,
	unsigned long *first_subwin_rpn)
{
	dt_node_t *dma_subwindow = NULL;
	struct spaace_t *spaace = NULL;
	int def_subwindow_cnt = -1, def_window;
	unsigned long current_fspi;
	int unit_address;
	unsigned long rpn;
	uint64_t window_addr[MAX_SUBWIN_CNT], window_size[MAX_SUBWIN_CNT];
	uint64_t next_window_addr;
	int i;

	for (i=0; i < MAX_SUBWIN_CNT; i++) {
		window_addr[i] = ULLONG_MAX;
		window_size[i] = ULLONG_MAX;
	}

	list_for_each(&dma_window->children, i) {
		dt_node_t *dma_subwindow = to_container(i, dt_node_t,
				child_node);
		dt_prop_t *gaddr;
		dt_prop_t *size;

		gaddr = dt_get_prop(dma_subwindow, "guest-addr", 0);
		if (!gaddr || gaddr->len != 8) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: warning: missing/bad guest-addr at %s\n",
				  __func__, dma_subwindow->name);
			return ERR_BADTREE;
		}
		window_addr[++def_subwindow_cnt] = *(uint64_t *)gaddr->data;

		size = dt_get_prop(dma_subwindow, "size", 0);
		if (!size || size->len != 8) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
				 "%s: warning: missing/bad size prop at %s\n",
				__func__, dma_subwindow->name);
			return ERR_BADTREE;
		}
		window_size[def_subwindow_cnt] = *(uint64_t *)size->data;
	}

	*fspi = current_fspi = get_fspi_and_increment(subwindow_cnt);

	if (current_fspi == ULONG_MAX) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			 "%s: spaace indexes exhausted %s\n",
			 __func__, dma_subwindow->name);
		return 0;
	}

	next_window_addr = primary_window_base_addr + subwindow_size;
	def_window = 0;
	*first_subwin_swse = 0;
	*first_subwin_rpn = 0;

	if (primary_window_base_addr == window_addr[0]) {
		*first_subwin_swse = map_addrspace_size_to_wse(window_size[0]);
		rpn = get_rpn(guest, window_addr[0] >> PAGE_SHIFT,
				 window_size[0]);
		if (rpn == ULONG_MAX)
			return 0;
		*first_subwin_rpn = rpn;
		++def_window;
	}

	for (unit_address = 0; unit_address < subwindow_cnt;
		next_window_addr += subwindow_size, unit_address++) {

		if (window_addr[def_window] == next_window_addr) {
			spaace = pamu_get_spaace(current_fspi, unit_address);
			setup_default_xfer_to_host_spaace(spaace);
			spaace->liodn = liodn;

			rpn = get_rpn(guest,
				window_addr[def_window] >> PAGE_SHIFT,
				window_size[def_window]);

			if (rpn == ULONG_MAX)
				return 0;

			spaace->atm = PAACE_ATM_WINDOW_XLATE;

			spaace->twbah = rpn >> 20;
			spaace->twbal = rpn;

			spaace->swse = map_addrspace_size_to_wse(
				window_size[def_window]);
			def_window++;

			if (omi != -1) {
				spaace->otm = PAACE_OTM_INDEXED;
				spaace->op_encode.index_ot.omi = omi;
				spaace->impl_attr.cid = stash_dest;
			}
		}
	}
	return subwindow_cnt;
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
	phys_addr_t window_addr = -1;
	phys_addr_t window_size = 0;
	uint32_t stash_dest = -1;
	uint32_t omi = -1;
	unsigned long fspi;
	phys_addr_t subwindow_size;
	unsigned int first_subwin_swse;
	uint32_t subwindow_cnt = 0;
	unsigned long first_subwin_rpn;

	prop = dt_get_prop(cfgnode, "dma-window", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: warning: missing dma-window at %s\n", __func__, cfgnode->name);
		return -1;
	}

	dma_window = dt_lookup_phandle(config_tree, *(const uint32_t *)prop->data);
	if (!dma_window) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: warning: bad dma-window phandle ref at %s\n",
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
	window_addr = *(const phys_addr_t *)gaddr->data;

	size = dt_get_prop(dma_window, "size", 0);
	if (!size || size->len != 8) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: warning: missing/bad size prop at %s\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}
	window_size = *(const phys_addr_t *)size->data;

	if (!is_primary_window_valid(window_addr, window_size)) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_NORMAL,
		         "%s: warning: %s is not aligned with window size \n",
		         __func__, dma_window->name);
	}

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace)
		return ERR_NOMEM;

	if (ppaace->wse) {
		printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
		         "%s: liodn %d or device in use\n", __func__, liodn);
		return ERR_BUSY;
	}

	ppaace->wse = map_addrspace_size_to_wse(window_size);

	setup_default_xfer_to_host_ppaace(ppaace);

	ppaace->wbah = 0;   // FIXME: 64-bit
	ppaace->wbal = window_addr >> PAGE_SHIFT;

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
				         "%s: warning: bad operation mapping index at %s\n",
				         __func__, cfgnode->name);
			}
		} else {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR,
			         "%s: warning: bad operation mapping index at %s\n",
			         __func__, cfgnode->name);
		}
	}

	/* configure stash id */
	stash_prop = dt_get_prop(cfgnode, "stash-dest", 0);
	if (stash_prop && stash_prop->len == 4) {
		stash_dest = get_stash_dest( *(const uint32_t *)
				stash_prop->data , hwnode);

		if (stash_dest != -1)
			ppaace->impl_attr.cid = stash_dest;
	}

	/* configure snoop-id if needed */
	prop = dt_get_prop(cfgnode, "snoop-cpu-only", 0);
	if (prop && stash_dest != -1) {
		if ((*(const uint32_t *)stash_prop->data) >= L3) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_NORMAL,
				"%s: warning: %s snoop-cpu-only property must have stash-dest as L1 or L2 cache\n",
				 __func__, dma_window->name);
			goto skip_snoop_id;
		}

		ppaace->domain_attr.to_host.snpid =
				get_snoop_id(hwnode, guest) + 1;
	}
skip_snoop_id:

	prop = dt_get_prop(dma_window, "subwindow-count", 0);
	if (prop)
		subwindow_cnt = *(const uint32_t *)prop->data;

	if (!is_subwindow_count_valid(subwindow_cnt)) {
		rpn = get_rpn(guest, window_addr >> PAGE_SHIFT, window_size);
		if (rpn == ULONG_MAX)
			return ERR_NOTRANS;

		ppaace->atm = PAACE_ATM_WINDOW_XLATE;
		ppaace->twbah = rpn >> 20;
		ppaace->twbal = rpn;
	}
	else
	{
		subwindow_size = window_size / subwindow_cnt;

		if (configure_dma_sub_windows(guest, dma_window, liodn,
					      window_addr, subwindow_cnt - 1,
					      subwindow_size, omi, stash_dest,
					      &first_subwin_swse, &fspi,
					      &first_subwin_rpn)) {
			ppaace->mw = 1;
			/*
			 * NOTE: The first sub-window exists in the primary
			 * paace itself and fspi is the index location of
			 * the spaace that describes the second sub-window
			 */
			ppaace->swse = first_subwin_swse;
			ppaace->fspi = fspi;
			ppaace->wce = map_subwindow_cnt_to_wce(subwindow_cnt);
			ppaace->atm = PAACE_ATM_WINDOW_XLATE;
			ppaace->twbah = first_subwin_rpn >> 20;
			ppaace->twbal = first_subwin_rpn;
		}
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

void setup_omt(void)
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
}

static int pamu_av_isr(void *arg)
{
	device_t *dev = arg;
	void *reg_base = dev->regs[0].virt;
	phys_addr_t reg_size = dev->regs[0].size;
	unsigned long reg_off;
	int ret = -1;

	for (reg_off = 0; reg_off < reg_size; reg_off += PAMU_OFFSET) {
		void *reg = reg_base + reg_off;

		uint32_t pics = in32((uint32_t *)(reg + PAMU_PICS));
		if (pics & PAMU_ACCESS_VIOLATION_STAT) {
			printlog(LOGTYPE_PAMU, LOGLEVEL_ERROR, 
			         "PAMU access violation on PAMU#%ld\n",
			         reg_off / PAMU_OFFSET);

			/* De-assert access violation pin */
			out32((uint32_t *)(reg + PAMU_PICS),
			      PAMU_ACCESS_VIOLATION_STAT);

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

	guts_node = dt_get_first_compatible(hw_devtree, "fsl,p4080-guts");
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

	pamubypenr_ptr = map(guts_addr + PAMUBYPENR, 4,
	                     TLB_MAS2_IO, TLB_MAS3_KERN);
	pamubypenr = in32(pamubypenr_ptr);

	for (pamu_reg_off = 0, pamu_counter = 0x80000000; pamu_reg_off < size;
	     pamu_reg_off += PAMU_OFFSET, pamu_counter >>= 1) {

		pamu_reg_base = (unsigned long) addr + pamu_reg_off;
		ret = pamu_hw_init(pamu_reg_base - CCSRBAR_PA, pamu_reg_off);
		if (ret < 0) {
			/* This can only fail for the first instance due to
			 * memory allocation issues, hence this failure
			 * implies global pamu init failure and let all
			 * PAMU(s) remain in bypass mode.
			 */
			return;
		}

		/* Disable PAMU bypass for this PAMU */
		pamubypenr &= ~pamu_counter;
	}

	setup_omt();

	if (dev->num_irqs >= 1) {
		interrupt_t *irq = dev->irqs[0];
		if (irq && irq->ops->register_irq)
			irq->ops->register_irq(irq, pamu_av_isr, dev);
	}

	/* Enable all relevant PAMU(s) */
	out32(pamubypenr_ptr, pamubypenr);
}
