/*
 * Paging, including guest phys to real phys translation.
 *
 * Copyright (C) 2007-2009 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <paging.h>
#include <libos/fsl-booke-tlb.h>
#include <percpu.h>
#include <errors.h>

phys_addr_t CCSRBAR_PA;

/* epn is already shifted by levels that the caller deals with. */
static pte_t *vptbl_get_ptep(pte_t *tbl, int *levels, unsigned long epn,
                             int insert)
{
	pte_t *ptep = NULL;

	while (--*levels >= 0) {
		int idx = (epn >> (PGDIR_SHIFT * *levels)) & (PGDIR_SIZE - 1);
		ptep = &tbl[idx];

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "pte %lx attr %lx epn %lx level %d\n", ptep->page, ptep->attr,
		         epn, *levels);

		if (ptep->attr & PTE_DMA)
			return ptep;

		if (!(ptep->attr & PTE_VALID)) {
			if (!insert)
				return NULL;

			if (*levels == 0)
				return ptep;

			tbl = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
			assert(tbl);

			ptep->page = (unsigned long)tbl;
			ptep->attr = PTE_VALID;
		}

		if (ptep->attr & PTE_SIZE)
			return ptep;

		tbl = (pte_t *)ptep->page;
	}

	return ptep;
}

unsigned long vptbl_xlate(pte_t *tbl, unsigned long epn,
                          unsigned long *attr, int level, int dma)
{
	pte_t *ptep = vptbl_get_ptep(tbl, &level, epn, 0);
	int valid = dma ? PTE_DMA : PTE_VALID;

	if (unlikely(!ptep)) {
		*attr = 0;
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "2 vtable xlate %p 0x%llx %i\n", tbl,
		         (uint64_t)epn << PAGE_SHIFT, level);
		return (1UL << (PGDIR_SHIFT * level)) - 1;
	}

	pte_t pte = *ptep;
	unsigned int size = pte.attr >> PTE_SIZE_SHIFT;
	unsigned long size_pages;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
	         "vtable xlate %p 0x%llx 0x%lx\n", tbl,
	         (uint64_t)epn << PAGE_SHIFT, pte.attr);

	size = pte.attr >> PTE_SIZE_SHIFT;

	if (level == 0) {
		assert(size < TLB_TSIZE_4M);
	} else {
		assert(level == 1);
		assert(size >= TLB_TSIZE_4M);
	}

	*attr = pte.attr;

	if (unlikely(!(pte.attr & valid)))
		return (1UL << (PGDIR_SHIFT * level)) - 1;
	
	size_pages = tsize_to_pages(size);
	return (pte.page & ~(size_pages - 1)) | (epn & (size_pages - 1));
}

/* Large mappings are a latency source -- this should only be done
   at initialization, when processing the device tree. */

void vptbl_map(pte_t *tbl, unsigned long epn, unsigned long rpn,
               unsigned long npages, unsigned long attr, int levels)
{
	unsigned long end = epn + npages;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "vptbl_map: epn %lx end %lx rpn %lx\n", epn, end, rpn);

	while (epn < end) {
		unsigned int size = min(max_page_size(epn, end - epn),
		                        natural_alignment(rpn));
		unsigned long size_pages = tsize_to_pages(size);
		unsigned long sub_end = epn + size_pages;

		assert(size_pages <= end - epn);
		attr = (attr & ~PTE_SIZE) | (size << PTE_SIZE_SHIFT);

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
		         "max_page_size(epn, end - epn) %u\n",
		         max_page_size(epn, end - epn));

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
		         "epn %lx rpn %lx end %lx size %u size_pages %lx sub_end %lx\n",
		         epn, rpn, end, size, size_pages, sub_end);

		assert(size > 0);
		int largepage = size >= TLB_TSIZE_4M;
		int incr = largepage ? PGDIR_SIZE - 1 : 0;
		
		while (epn < sub_end) {
			int level = levels - largepage;
			pte_t *ptep = vptbl_get_ptep(tbl, &level,
			                             epn >> (PGDIR_SHIFT * largepage),
			                             1);

			if (!largepage && level > 0) {
				printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
				         "vptbl_map: Tried to overwrite a large page with "
				         "a small page at %llx\n",
				         (uint64_t)epn << PAGE_SHIFT);

				/* FIXME: verify that the rpn is the same, and that
				 * the permissions of the small page are a subset.
				 */
				goto next;
			}

			if (level < 0) {
				if (!largepage || !ptep->page || (ptep->attr & PTE_SIZE))
					BUG();

				printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
				         "vptbl_map: Overwriting a small page with "
				         "a large page at %llx\n",
				         (uint64_t)epn << PAGE_SHIFT);

				/* FIXME: verify that the all rpns are the same,
				 * and that permissions of the large page are a
				 * superset.
				 */
				free((void *)ptep->page);
			} 

			ptep->page = rpn;
			ptep->attr = attr;
			
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
			         "epn %lx: setting rpn %lx attr %lx\n", epn, rpn, attr);

next:
			epn = (epn | incr) + 1;
			rpn = (rpn | incr) + 1;
		}
	}
}

#ifdef CONFIG_DEVICE_VIRT

/**
 * register_vf_handler - register a virtualization fault handler
 * @start - starting guest physical address of range
 * @end - guest physical address of last byte in range
 * @callback - function to call if an access to the range by the guest occurs
 *
 * This function registers a callback handler for device virtualization.
 * When a virtualization fault occurs, the trap handler the guest physical
 * address of the attempted access.  If matches, the callback function is
 * called.
 *
 * Currently, we only support virtualization of addresses in CCSR space.  We
 * also assume that for CCSR address space, guest physical equals real
 * physical.
 */
vf_range_t *register_vf_handler(guest_t *guest, phys_addr_t start, phys_addr_t end,
				vf_callback_t callback)
{
	vf_range_t *vf;

	vf = malloc(sizeof(vf_range_t));
	if (!vf)
		return NULL;

	assert(start < end);
	assert(callback);

	vf->start = start;
	vf->end = end;
	vf->callback = callback;

	// Get a permanent hypervisor virtual address
	vf->vaddr = map(start, end - start + 1, TLB_MAS2_IO, TLB_MAS3_KERN);

	// Each guest has its own list
	list_add(&guest->vf_list, &vf->list);

	return vf;
}

#endif

