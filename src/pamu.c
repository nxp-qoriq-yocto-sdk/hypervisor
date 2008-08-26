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

extern void *fdt;
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

static int pamu_config_assigned_liodn(guest_t *guest, uint32_t assigned_liodn, uint32_t *gpa_range, uint32_t *gpa_size)
{
	struct ppaace_t *current_ppaace;
	unsigned long attr;
	unsigned long mach_phys_contig_size = 0;
	unsigned long start_rpn = 0;
	unsigned long grpn = gpa_range[3] >> PAGE_SHIFT; /* vptbl needs pfn */
	unsigned long size_pages;
	unsigned long next_rpn = 0;

	while (1) {

		/*
		 * Need to iterate over vptbl_xlate(), as it returns a single
		 * mapping upto max. TLB page size on each call.
		 */

		unsigned long rpn = vptbl_xlate(guest->gphys, grpn, &attr,
						 PTE_PHYS_LEVELS);

		if (!(attr & PTE_VALID)) {
			if (!start_rpn) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
						"invalid rpn\n");
				return -1;
			}
			else {
				break;
			}
		}

		if (!start_rpn) {
			start_rpn = rpn;
		} else if (rpn != next_rpn) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
				"rpn = 0x%lx, size = 0x%lx\n",
				start_rpn, mach_phys_contig_size);
			break;
		}

		if (!(attr & (PTE_SW | PTE_UW))) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"dma-ranges not writeable!\n");
			return -1;
		}

		size_pages = tsize_to_pages(attr >> PTE_SIZE_SHIFT);
		mach_phys_contig_size += (size_pages << PAGE_SHIFT);
		grpn += size_pages;
		next_rpn = rpn + size_pages;
	}

	current_ppaace = get_ppaace(assigned_liodn);
	if (!current_ppaace)
		return -1;

	setup_default_xfer_to_host_ppaace(current_ppaace);

	current_ppaace->wbah = 0;
	current_ppaace->wbal = (gpa_range[3]) >> PAGE_SHIFT;
	current_ppaace->wse  = map_addrspace_size_to_wse(mach_phys_contig_size);

	current_ppaace->atm = PAACE_ATM_WINDOW_XLATE;

	current_ppaace->twbah = 0;
	current_ppaace->twbal = start_rpn; /* vptbl functions return pfn's */

	/* PAACE is invalid, validated by enable hcall */
	current_ppaace->v = 1;  // FIXME: for right now we are leaving PAACE
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

	current_ppaace = get_ppaace(liodn);
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

	current_ppaace = get_ppaace(liodn);
	current_ppaace->v = 0;

	/*
	 * FIXME : Need to wait or synchronize with any pending DMA flush
	 * operations on disabled liodn's, as once we disable PAMU
	 * window here any pending DMA flushes will fail.
	 */

	return 0;
}

unsigned int pamu_map_ppid_liodn(unsigned int handle)
{
	guest_t *guest = get_gcpu()->guest;
	ppid_handle_t *ppid_handle;

	// FIXME: race against handle closure
	if (handle >= MAX_HANDLES || !guest->handles[handle]) {
		return -1;
	}

	ppid_handle = guest->handles[handle]->ppid;
	if (!ppid_handle) {
		return -1;
	}

	return ppid_handle->liodn_handle;
}

void pamu_process_standard_liodns(guest_t *guest)
{
	int len, ret;
	const uint32_t *dma_ranges;
	uint32_t cas_addrbuf[MAX_ADDR_CELLS];	/* child address space */
	uint32_t pas_addrbuf[MAX_ADDR_CELLS];	/* parent address space */
	uint32_t cas_sizebuf[MAX_SIZE_CELLS];
	uint32_t  c_naddr, c_nsize, p_naddr, p_nsize;
	int parent;
	const uint32_t *liodnrp;
	int offset = -1;
	int liodnrp_len, offset_in_gdt;
	char pathbuf[256];
	unsigned long assigned_liodn;
	pamu_handle_t *pamu_handle;
	int ghandle;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		"processing standard liodns...\n");

	while (1) {
		offset = fdt_node_offset_by_prop(guest->devtree, offset, "fsl,liodn");
		if (offset == -FDT_ERR_NOTFOUND) 
			break;

		ret = fdt_get_path(guest->devtree, offset, pathbuf, 256);

		offset_in_gdt = fdt_path_offset(fdt, pathbuf);

		liodnrp = fdt_getprop(fdt, offset_in_gdt,
				"fsl,liodn", &liodnrp_len);

		if (!liodnrp)
			continue;
		if (liodnrp_len < 0)
			continue;

		dma_ranges = fdt_getprop(guest->devtree, offset,
				"dma-ranges", &len);
		if (!dma_ranges) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"pamu_partition_init: error getting dma-ranges property, error_code = %d\n", len);
			continue;
		}
		if (len == 0)
			continue;

		ret = get_addr_format(guest->devtree, offset,
				&c_naddr, &c_nsize);
		if (ret < 0)
			continue;

		copy_val(cas_addrbuf, dma_ranges, c_naddr);

		dma_ranges += c_naddr;

		parent = fdt_parent_offset(guest->devtree, offset);
		if (parent == -FDT_ERR_NOTFOUND)
			continue;

		ret = get_addr_format(guest->devtree, parent, &p_naddr,
						&p_nsize);
		if (ret < 0)
			continue;

		copy_val(pas_addrbuf, dma_ranges, p_naddr);

		/*
		* Current assumption is that child dma address space is
		* directly mapped to parent dma address space
		*/

		dma_ranges += p_naddr;

		copy_val(cas_sizebuf, dma_ranges, c_nsize);

		assigned_liodn = *liodnrp;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
			"Assigned standard liodn = %d\n", *liodnrp);

		ret = pamu_config_assigned_liodn(guest,
				assigned_liodn,
				cas_addrbuf, cas_sizebuf);
		if (ret < 0)
			continue;

		pamu_handle = alloc_type(pamu_handle_t);
		pamu_handle->user.pamu = pamu_handle;
		pamu_handle->assigned_liodn = assigned_liodn;

		ghandle = alloc_guest_handle(guest, &pamu_handle->user);
		if (ghandle < 0)
			 continue;

		ret = fdt_setprop(guest->devtree, offset,
				"fsl,hv-liodn-handle", &ghandle, 4);

		if (ret < 0)
			continue;

		/*
		 * Pass-thru "fsl,liodn" to the guest device tree
		 */
		ret = fdt_setprop(guest->devtree, offset,
				"fsl,liodn", &assigned_liodn, 4);
	}
}

void pamu_process_ppid_liodns(guest_t *guest)
{
	int len, ret;
	const uint32_t *dma_ranges;
	uint32_t cas_addrbuf[MAX_ADDR_CELLS];	/* child address space */
	uint32_t pas_addrbuf[MAX_ADDR_CELLS];	/* parent address space */
	uint32_t cas_sizebuf[MAX_SIZE_CELLS];
	uint32_t  c_naddr, c_nsize, p_naddr, p_nsize;
	int parent;
	const uint32_t *ppidp;
	const uint32_t *ppid_to_liodn = NULL;
	const uint32_t *ppid_to_liodn_guest = NULL;
	int ppid_liodn_len_guest;
	int offset = -1;
	int ppidp_len, offset_in_gdt, off_ppid_liodn, ppid_liodn_len;
	char pathbuf[256];
	unsigned int assigned_liodn;
	pamu_handle_t *pamu_handle;
	ppid_handle_t *ppid_handle;
	int ghandle;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		"processing ppid liodns...\n");

	/*
	 * Current assumption is that FMAN, Crypto, PME will all
	 * express the same LIODN for a given PPID, hence we simply
	 * need to lookup fsl,ppid-to-liodn property in hypervisor
	 * device tree, search it once and cache it for ppaace programming
	 * & pass-thru to the guest device trees.
	 */

	for (off_ppid_liodn = fdt_next_node(fdt, -1, NULL);
	     off_ppid_liodn >= 0;
	     off_ppid_liodn = fdt_next_node(fdt, off_ppid_liodn, NULL)){

		ppid_to_liodn = fdt_getprop(fdt, off_ppid_liodn,
			"fsl,ppid-to-liodn", &ppid_liodn_len);

		if (ppid_to_liodn && ppid_liodn_len)
			break;
	}

	if (!ppid_to_liodn) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"pamu_partition_init: no fsl,ppid-to-liodn property\n");
		return;
	}

	while (1) {
		offset = fdt_node_offset_by_prop(guest->devtree, offset, "fsl,ppid");
		if (offset == -FDT_ERR_NOTFOUND)
			break;

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
			"doing ppid/liodn lookup for %s\n",
			fdt_get_name(guest->devtree,offset,NULL));

		ret = fdt_get_path(guest->devtree, offset, pathbuf, 256);

		offset_in_gdt = fdt_path_offset(fdt, pathbuf);

		if (offset_in_gdt < 0)
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"fsl,ppid not found in global dev tree: %s\n",pathbuf);

		ppidp = fdt_getprop(fdt, offset_in_gdt, "fsl,ppid", &ppidp_len);

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
			"	ppid = %d\n", *ppidp);

		if (!ppidp)
			continue;
		if (ppidp_len < 0)
			continue;

		if (*ppidp >= (ppid_liodn_len/sizeof(uint32_t))) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"error : fsl,ppid value is incorrect\n");
			continue;
		}

		assigned_liodn = ppid_to_liodn[*ppidp];

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
			"ppid %d mapped to liodn %d\n", *ppidp, assigned_liodn);

		dma_ranges = fdt_getprop(guest->devtree, offset,
				"dma-ranges", &len);
		if (!dma_ranges) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"pamu_partition_init: error getting dma-ranges property, error_code = %d\n", len);
			continue;
		}
		if (len == 0)
			continue;

		ret = get_addr_format(guest->devtree, offset,
				&c_naddr, &c_nsize);
		if (ret < 0)
			continue;

		copy_val(cas_addrbuf, dma_ranges, c_naddr);

		dma_ranges += c_naddr;

		parent = fdt_parent_offset(guest->devtree, offset);
		if (parent == -FDT_ERR_NOTFOUND)
			continue;

		ret = get_addr_format(guest->devtree, parent, &p_naddr,
						&p_nsize);
		if (ret < 0)
			continue;

		copy_val(pas_addrbuf, dma_ranges, p_naddr);

		/*
		* Current assumption is that child dma address space is
		* directly mapped to parent dma address space
		*/

		dma_ranges += p_naddr;

		copy_val(cas_sizebuf, dma_ranges, c_nsize);

		ret = pamu_config_assigned_liodn(guest,
				assigned_liodn,
				cas_addrbuf, cas_sizebuf);
		if (ret < 0)
			continue;

		pamu_handle = alloc_type(pamu_handle_t);
		pamu_handle->user.pamu = pamu_handle;
		pamu_handle->assigned_liodn = assigned_liodn;

		ghandle = alloc_guest_handle(guest, &pamu_handle->user);
		if (ghandle < 0)
			 continue;

		ppid_handle = alloc_type(ppid_handle_t);
		ppid_handle->user.ppid = ppid_handle;
		ppid_handle->liodn_handle = ghandle;

		ghandle = alloc_guest_handle(guest, &ppid_handle->user);
		if (ghandle < 0)
			 continue;

		ret = fdt_setprop(guest->devtree, offset,
				"fsl,hv-ppid-handle", &ghandle, 4);

		if (ret < 0)
			continue;

		/*
		 * Pass-thru "fsl,ppid" to * the guest device tree
		 */
		ret = fdt_setprop(guest->devtree, offset, "fsl,ppid", ppidp, 4);
	}

	/*
	 * Pass-thru "fsl,ppid-to-liodn" property to the guest device tree
	 */

	for (off_ppid_liodn = fdt_next_node(guest->devtree, -1, NULL);
	     off_ppid_liodn >= 0;
	     off_ppid_liodn = fdt_next_node(guest->devtree,
		off_ppid_liodn, NULL)) {

		ppid_to_liodn_guest = fdt_getprop(guest->devtree,
			off_ppid_liodn, "fsl,ppid-to-liodn",
			 &ppid_liodn_len_guest);

		if (ppid_to_liodn_guest && ppid_liodn_len_guest) {
			ret = fdt_setprop(guest->devtree, off_ppid_liodn,
				"fsl,ppid-to-liodn",
				ppid_to_liodn, ppid_liodn_len);
		}
	}
}

void pamu_partition_init(guest_t *guest)
{
	if (!pamu_global_init_done)
		return;

	pamu_process_standard_liodns(guest);

	pamu_process_ppid_liodns(guest);
}

#define PAMUBYPENR 0x604
void pamu_global_init(void)
{
	int pamu_node, ret;
	phys_addr_t addr, size;
	unsigned long pamu_reg_base, pamu_reg_off;
	int guts_node;
	phys_addr_t guts_addr, guts_size;
	uint32_t pamubypenr, pamu_counter, *pamubypenr_ptr;

	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them.
	 */

	/* find the pamu node */
	pamu_node = fdt_node_offset_by_compatible(fdt, -1, "fsl,p4080-pamu");
	if (pamu_node < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
			"warning: no pamu node found\n");
		return;
	}

	ret = dt_get_reg(fdt, pamu_node, 0, &addr, &size);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"ERROR : no pamu reg found\n");
		return;
	}

	guts_node = fdt_node_offset_by_compatible(fdt, -1, "fsl,p4080-guts");
	if (guts_node < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"warning: no guts node found\n");
		return;
	}

	ret = dt_get_reg(fdt, guts_node, 0, &guts_addr, &guts_size);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"ERROR : no guts reg found, ret = %d\n", ret);
		return;
	}

	pamubypenr_ptr = (uint32_t *)((uintptr_t) (CCSRBAR_VA + guts_addr
				- CCSRBAR_PA) + PAMUBYPENR);
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
	pamu_global_init_done = 1;
}
