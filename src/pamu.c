/** peripheral access management unit (PAMU) support
 *
 * @file
 *
 */

/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

int pamu_global_init_done;

static unsigned int map_addrspace_size_to_wse(unsigned long addrspace_size)
{
	if (addrspace_size & (addrspace_size - 1)) {
		addrspace_size = 1 << (LONG_BITS - 1 -
				(count_msb_zeroes(addrspace_size)));

		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"dma address space size not power of 2, rounding down to next power of 2 = 0x%lx\n", addrspace_size);
	}

	/* window size is 2^(WSE+1) bytes */
	return count_lsb_zeroes(addrspace_size >> PAGE_SHIFT) + 11;
}

/*
 * Given a guest physical page number and size, return
 * the real (true physical) page number
 *
 * return values
 *    ULONG_MAX on failure
 *    rpn value on success
 */
static unsigned long get_rpn(guest_t *guest, unsigned long grpn, phys_addr_t size)
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
						 PTE_PHYS_LEVELS);
		if (!(attr & PTE_VALID)) {
			if (start_rpn == ULONG_MAX) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
						"invalid rpn\n");
				return ULONG_MAX;
			} else {
				break;
			}
		}

		if (start_rpn == ULONG_MAX) {
			start_rpn = rpn;
		} else if (rpn != next_rpn) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
				"rpn = 0x%lx, size = 0x%lx\n",
				start_rpn, mach_phys_contig_size);
			break;
		}

		if (!(attr & (PTE_SW | PTE_UW))) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"dma-window not writeable!\n");
			return ULONG_MAX;
		}

		size_pages = tsize_to_pages(attr >> PTE_SIZE_SHIFT);
		mach_phys_contig_size += (size_pages << PAGE_SHIFT);
		grpn += size_pages;
		next_rpn = rpn + size_pages;
	}

	if (size > mach_phys_contig_size) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: dma-window size error\n", __func__);
		return ULONG_MAX;
	}

	return start_rpn;
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
	dt_prop_t *prop;
	dt_prop_t *gaddr;
	dt_prop_t *size;
	dt_node_t *dma_window;
	phys_addr_t window_addr = -1;
	phys_addr_t window_size = 0;

// TODO implement subwindows

	prop = dt_get_prop(cfgnode, "dma-window", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: warning: missing dma-window\n", __func__);
		return -1;
	}

	dma_window = dt_lookup_phandle(config_tree, *(const uint32_t *)prop->data);
	if (!dma_window) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: warning: bad dma-window phandle ref at %s\n",
		         __func__, cfgnode->name);
		return ERR_BADTREE;
	}
	gaddr = dt_get_prop(dma_window, "guest-addr", 0);
	if (!gaddr || gaddr->len != 8) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: warning: missing/bad guest-addr at %s\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}
	window_addr = *(const phys_addr_t *)gaddr->data;

	size = dt_get_prop(dma_window, "size", 0);
	if (!size || size->len != 8) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: warning: missing/bad size prop at %s\n",
		         __func__, dma_window->name);
		return ERR_BADTREE;
	}
	window_size = *(const phys_addr_t *)size->data;

	rpn = get_rpn(guest, window_addr >> PAGE_SHIFT, window_size);
	if (rpn == ULONG_MAX)
		return -1;

	ppaace = pamu_get_ppaace(liodn);
	if (!ppaace)
		return -1;

	setup_default_xfer_to_host_ppaace(ppaace);

	ppaace->wbah = 0;
	ppaace->wbal = window_addr >> PAGE_SHIFT;
	ppaace->wse  = map_addrspace_size_to_wse(window_size);

	ppaace->atm = PAACE_ATM_WINDOW_XLATE;

	ppaace->twbah = 0;
	ppaace->twbal = rpn;

	smp_lwsync();

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

#define PAMUBYPENR 0x604
void pamu_global_init(void)
{
	int ret;
	phys_addr_t addr, size;
	unsigned long pamu_reg_base, pamu_reg_off;
	dt_node_t *guts_node, *pamu_node;
	phys_addr_t guts_addr, guts_size;
	uint32_t pamubypenr, pamu_counter, *pamubypenr_ptr;

	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them.
	 */

	/* find the pamu node */
	pamu_node = dt_get_first_compatible(hw_devtree, "fsl,p4080-pamu");
	if (!pamu_node) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "%s: warning: no pamu node found\n", __func__);
		return;
	}

	ret = dt_get_reg(pamu_node, 0, &addr, &size);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: no pamu reg found\n", __func__);
		return;
	}

	guts_node = dt_get_first_compatible(hw_devtree, "fsl,p4080-guts");
	if (!guts_node) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: pamu present, but no guts node found\n", __func__);
		return;
	}

	ret = dt_get_reg(guts_node, 0, &guts_addr, &guts_size);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
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

	/* Enable all relevant PAMU(s) */
	out32(pamubypenr_ptr, pamubypenr);

	smp_mbar();

	pamu_global_init_done = 1;
}
