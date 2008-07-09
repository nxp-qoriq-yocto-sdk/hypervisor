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

static unsigned long current_liodn = -1;

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
	unsigned long grpn = gpa_range[3];
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
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"rpn = 0x%lx, size = 0x%lx\n",
				start_rpn, mach_phys_contig_size);
			break;
		}

		if (!(attr & (PTE_SW | PTE_UW))) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"dma-ranges not writeable!!!\n");
			return -1;
		}

		size_pages = tsize_to_pages(attr >> PTE_SIZE_SHIFT);
		mach_phys_contig_size += (size_pages << PAGE_SHIFT);
		grpn += size_pages;
		next_rpn = rpn + size_pages;
	}

	current_ppaace = get_ppaace(assigned_liodn);
	setup_default_xfer_to_host_ppaace(current_ppaace);

	current_ppaace->wbah = 0;
	current_ppaace->wbal = (gpa_range[3]) >> PAGE_SHIFT;
	current_ppaace->wse  = map_addrspace_size_to_wse(mach_phys_contig_size);

	current_ppaace->atm = PAACE_ATM_WINDOW_XLATE;

	current_ppaace->twbah = 0;
	current_ppaace->twbal = start_rpn; /* vptbl functions return pfn's */

	/* PAACE is invalid, validated by enable hcall */
	current_ppaace->v = 0;

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

	assert(current_liodn != (unsigned long) -1 || liodn <= current_liodn);

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

	assert(current_liodn != (unsigned long) -1 || liodn <= current_liodn);

	current_ppaace = get_ppaace(liodn);
	current_ppaace->v = 0;

	/*
	 * FIXME : Need to wait or synchronize with any pending DMA flush
	 * operations on disabled liodn's, as once we disable PAMU
	 * window here any pending DMA flushes will fail.
	 */

	return 0;
}

void pamu_partition_init(guest_t *guest)
{
	int node = -1, liodnr_node = -1, len, ret;
	const uint32_t *dma_ranges;
	uint32_t cas_addrbuf[MAX_ADDR_CELLS];	/* child address space */
	uint32_t pas_addrbuf[MAX_ADDR_CELLS];	/* parent address space */
	uint32_t cas_sizebuf[MAX_SIZE_CELLS];
	uint32_t  c_naddr, c_nsize, p_naddr, p_nsize;
	int parent;
	const uint32_t *liodnrp;
	unsigned long next_assigned_liodn;
	pamu_handle_t *pamu_handle;
	int ghandle;
	phys_addr_t liodnr_addr;
#ifdef DEBUG
	uint32_t naddr, nsize;
	const char *s;
#endif

	while ((node = fdt_next_node(guest->devtree, node, NULL)) >= 0) {
		liodnrp = fdt_getprop(guest->devtree, node, "fsl,liodn-reg",
						 &len);
		if (!liodnrp)
			continue;
		if (len == 0)
			continue;

		liodnr_node = fdt_node_offset_by_phandle(guest->devtree,
						*liodnrp);
		if (liodnr_node < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"pamu_partition_init: invalid phandle\n");
			continue;
		}

#ifdef DEBUG
		s = fdt_get_name(guest->devtree, liodnr_node, &len);
		printf("liodn node = %s\n", s);
		parent = fdt_parent_offset(guest->devtree, liodnr_node);
		s = fdt_get_name(guest->devtree, parent, &len);
		printf("liodn parent node = %s\n", s);
		ret = get_addr_format(guest->devtree, parent, &naddr, &nsize);
		printf("naddr = %d, nsize = %d\n", naddr, nsize);
#endif

		ret = dt_get_reg(guest->devtree, liodnr_node, 0, &liodnr_addr,
						 NULL);
		if (ret < 0) {
			printf("pamu failed to get reg: %d\n", ret);
			continue;
		}

		dma_ranges = fdt_getprop(guest->devtree, node, "dma-ranges",
						 &len);
		if (!dma_ranges) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				"pamu_partition_init: error getting dma-ranges property\n");
			continue;
		}
		if (len == 0)
			continue;

		ret = get_addr_format(guest->devtree, node, &c_naddr, &c_nsize);
		if (ret < 0)
			continue;

		copy_val(cas_addrbuf, dma_ranges, c_naddr);

		dma_ranges += c_naddr;

		parent = fdt_parent_offset(guest->devtree, node);
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

		next_assigned_liodn = atomic_add(&current_liodn, 1);

		ret = pamu_config_assigned_liodn(guest, next_assigned_liodn,
				cas_addrbuf, cas_sizebuf);
		if (ret < 0)
			continue;

		pamu_handle = alloc_type(pamu_handle_t);
		pamu_handle->user.pamu = pamu_handle;
		pamu_handle->assigned_liodn = next_assigned_liodn;

		ghandle = alloc_guest_handle(guest, &pamu_handle->user);
		if (ghandle < 0)
			 continue;

		ret = fdt_setprop(guest->devtree, node, "fsl,liodn",
					 &ghandle, 4);
		if (ret < 0)
			continue;

		out32((uint32_t *)(unsigned long)
			(CCSRBAR_VA + (liodnr_addr - CCSRBAR_PA)),
			 next_assigned_liodn);
	}

}

void pamu_global_init(void *fdt)
{
	int pamu_node, ret;
	phys_addr_t addr, size;
	unsigned long pamu_reg_base, pamu_reg_size;

	/*
	 * enumerate all PAMUs and allocate and setup PAMU tables
	 * for each of them.
	 */

	/* find the pamu node */
	pamu_node = fdt_node_offset_by_compatible(fdt, -1, "fsl,p4080-pamu");
	if (pamu_node < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			"warning: no pamu node found\n");
		return;
	}

	ret = dt_get_reg(fdt, pamu_node, 0, &addr, &size);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"ERROR : no pamu reg found\n");
		return;
	}

	for (pamu_reg_size = 0; pamu_reg_size < size;
		pamu_reg_size += PAMU_OFFSET) {
		pamu_reg_base = (unsigned long) addr + pamu_reg_size;
		pamu_hw_init(pamu_reg_base-CCSRBAR_PA, pamu_reg_size);
	}
}
