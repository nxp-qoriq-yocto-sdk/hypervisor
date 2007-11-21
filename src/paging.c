/*
 * Paging, including guest phys to real phys translation.
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
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
#include <tlb.h>

unsigned long vptbl_xlate(pte_t *tbl, unsigned long epn, unsigned long *attr)
{
	pte_t pte = tbl[epn >> PGDIR_SHIFT];
	unsigned int size = pte.attr >> PTE_SIZE_SHIFT;
	unsigned long size_pages;

//	printf("vtable xlate %p 0x%lx 0x%lx\n", tbl, epn << PAGE_SHIFT, pte.attr);

	if (unlikely(!(pte.attr & PTE_VALID))) {
		*attr = 0;
		return 0x3ff;
	}

	if (size == 0) {
		pte.page &= ~(PAGE_SIZE * 2 - 1);
		tbl = (pte_t *)pte.page;
		pte = tbl[epn & (PGDIR_SIZE - 1)];

//		printf("page %lx attr %lx\n", pte.page, pte.attr);

		if (unlikely(!(pte.attr & PTE_VALID))) {
			*attr = 0;
			return 0; 
		}
		
		size = pte.attr >> PTE_SIZE_SHIFT;
		assert(size != 0 && size < TLB_TSIZE_4M);
	} else {
		assert(size >= TLB_TSIZE_4M);
	}

	*attr = pte.attr;
	
	size_pages = tsize_to_pages(size);
	return (pte.page & ~(size_pages - 1)) | (epn & (size_pages - 1));
}

// Large mappings are a latency source -- this should only be done
// at initialization, when processing the device tree.

void vptbl_map(pte_t *tbl, unsigned long epn, unsigned long rpn,
               unsigned long npages, unsigned long attr)
{
	unsigned long end = epn + npages;

	while (epn < end) {
		unsigned int size = min(max_page_size(epn, end - epn),
		                        natural_alignment(rpn));
		unsigned long size_pages = tsize_to_pages(size);
		unsigned long sub_end = epn + size_pages;
		int l2idx = epn >> PGDIR_SHIFT;

		assert(size_pages <= end - epn);
		attr = (attr & ~PTE_SIZE) | (size << PTE_SIZE_SHIFT);

#if 0
		printf("max_page_size(epn, end - epn) %u\n", max_page_size(epn, end - epn));

		printf("epn %lx rpn %lx end %lx size %u size_pages %lx sub_end %lx\n", epn, rpn, end,
		       size, size_pages, sub_end);
#endif

		assert(size > 0);

		if (size >= TLB_TSIZE_4M) {
			while (epn < sub_end) {
				tbl[l2idx].page = rpn;
				tbl[l2idx].attr = attr;
	
				epn = (epn | (PGDIR_SIZE - 1)) + 1;
				rpn = (rpn | (PGDIR_SIZE - 1)) + 1;
				l2idx++;
				continue;
			}
		} else while (epn < sub_end) {
			pte_t *subtbl = (pte_t *)tbl[l2idx].page;
			int idx = epn & (PGDIR_SIZE - 1);

			if (!subtbl) {
				subtbl = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
				assert(subtbl);
				
				tbl[l2idx].page = (unsigned long)subtbl;
				tbl[l2idx].attr = PTE_VALID;
			}

			subtbl[idx].page = rpn;
			subtbl[idx].attr = attr;

			epn++;
			rpn++;
			idx++;
		}
	}
}
