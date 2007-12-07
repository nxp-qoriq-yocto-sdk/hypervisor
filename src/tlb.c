/*-
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Some hw specific parts of this pmap were derived or influenced
 * by NetBSD's ibm4xx pmap module. More generic code is shared with
 * a few other pmap modules from the FreeBSD tree.
 */

#include <libos/fsl-booke-tlb.h>
#include <libos/spr.h>
#include <percpu.h>
#include <paging.h>
#include <libos/bitops.h>

static int alloc_tlb1(unsigned int entry)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long val;
	int idx = 0;
	int i = 0;

	do {
		while (gcpu->tlb1_free[i]) {
			int bit = count_lsb_zeroes(gcpu->tlb1_free[i]);

			if (idx + bit >= TLB1_RSVD)
				return -1;
			
			gcpu->tlb1_free[i] &= ~(1UL << bit);
			gcpu->tlb1_used[entry][i] |= 1UL << bit;

#if 0
			printf("tlb1_free[%d] now %lx\n", i, gcpu->tlb1_free[i]);
			printf("using tlb1[%d] for gtlb1[%d]\n", idx + bit, entry);
#endif

			return idx + bit;
		}

		idx += LONG_BITS;
		i++;
	} while (idx < TLB1_SIZE);

	return -1;
}

static void free_tlb1(unsigned int entry)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long val;
	int i = 0;
	int idx = 0;

	do {
		while (gcpu->tlb1_used[entry][i]) {
			int bit = count_lsb_zeroes(gcpu->tlb1_used[entry][i]);
			assert(idx + bit < TLB1_RSVD);
			
//			printf("clearing tlb1[%d] for gtlb1[%d]\n", idx + bit, entry);

			cpu->tlb1[idx + bit].mas1 = 0;
			tlb1_write_entry(idx + bit);

			gcpu->tlb1_used[entry][i] &= ~(1UL << bit);
			gcpu->tlb1_free[i] |= 1UL << bit;
		}

		i++;
		idx += LONG_BITS;
	} while (idx < TLB1_SIZE);
}

unsigned int guest_tlb1_to_gtlb1(unsigned int idx)
{
	gcpu_t *gcpu = get_gcpu();
	int i = idx / LONG_BITS;
	int bit = idx % LONG_BITS;
	unsigned int entry;

	for (entry = 0; entry < TLB1_GSIZE; entry++)
		if (gcpu->tlb1_used[entry][i] & (1UL << bit))
			return entry;

	return TLB1_GSIZE;
}

void guest_set_tlb1(unsigned int entry, uint32_t mas1,
                    unsigned long epn, unsigned long grpn,
                    uint32_t mas2flags, uint32_t mas3flags)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned int size = (mas1 >> MAS1_TSIZE_SHIFT) & 15;
	unsigned long size_pages = tsize_to_pages(size);
	unsigned long end = epn + size_pages;

//	printf("gtlb1[%d] mapping from %lx to %lx, grpn %lx, mas1 %x\n",
//	       entry, epn, end, grpn, mas1);

	free_tlb1(entry);

	gcpu->gtlb1[entry].mas1 = mas1;
	gcpu->gtlb1[entry].mas2 = (epn << PAGE_SHIFT) | mas2flags;
	gcpu->gtlb1[entry].mas3 = (grpn << PAGE_SHIFT) | mas3flags;
	gcpu->gtlb1[entry].mas7 = grpn >> (32 - PAGE_SHIFT);
	
	while (epn < end) {
		int size = max_page_size(epn, end - epn);

		unsigned long attr, rpn; 
		rpn = vptbl_xlate(guest->gphys, grpn, &attr);

		// If there's no valid mapping, try again at the next page. Note
		// that this searching can cause latency.  If you need to do
		// dynamic TLB1 writes for large pages that include unmapped
		// guest physical regions, and care about latency, use the
		// paravirtualized interface.
		//
		// Unfortunately, we'll have to reflect a TLB miss rather than
		// a machine check for accesses to these mapping holes,
		// as TLB1 entries are a limited resource that we don't want
		// to spend on VF mappings.

		if (unlikely(!(attr & PTE_VALID))) {
//			printf("invalid grpn %lx, epn %lx, skip %lx\n", grpn, epn, rpn);
			epn = (epn | rpn) + 1;
			grpn = (grpn | rpn) + 1;
			continue;
		}

		mas3flags &= attr & PTE_MAS3_MASK;

		unsigned long mas8 = guest->lpid;
		mas8 |= (attr << PTE_MAS8_SHIFT) & PTE_MAS8_MASK;

		size = min(size, attr >> PTE_SIZE_SHIFT);

		int real_entry = alloc_tlb1(entry);
		if (real_entry < 0) {
			printf("Out of TLB1 entries!\n");
			printf("entry %d, base 0x%lx, size 0x%llx\n",
			       entry, epn << PAGE_SHIFT, ((uint64_t)size) << PAGE_SHIFT);

			// FIXME: reflect machine check
			BUG();
		}

		tlb1_set_entry(real_entry, epn << PAGE_SHIFT,
		               ((physaddr_t)rpn) << PAGE_SHIFT,
		               size,
		               mas2flags, mas3flags,
		               (mas1 >> MAS1_TID_SHIFT) & 0xff,
		               (mas1 >> MAS1_TS_SHIFT) & 1,
		               mas8);
		
		epn += tsize_to_pages(size);
		grpn += tsize_to_pages(size);
	}
}
