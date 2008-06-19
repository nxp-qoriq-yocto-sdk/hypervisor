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
#include <libos/core-regs.h>
#include <libos/bitops.h>

#include <percpu.h>
#include <paging.h>

/* First TLB1 entry reserved for the hypervisor. Entries below this but
 * above TLB1_GSIZE are used to break up guest TLB1 entries due to
 * alignment, size, or permission holes.
 */
static int tlb1_reserved = BASE_TLB_ENTRY;

static int alloc_tlb1(unsigned int entry)
{
	gcpu_t *gcpu = get_gcpu();
	int idx = 0;
	int i = 0;

	do {
		while (~gcpu->tlb1_inuse[i]) {
			int bit = count_lsb_zeroes(~gcpu->tlb1_inuse[i]);

			if (idx + bit >= tlb1_reserved)
				return -1;
			
			gcpu->tlb1_inuse[i] |= 1UL << bit;
			gcpu->tlb1_map[entry][i] |= 1UL << bit;

			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "tlb1_inuse[%d] now %lx\n", i, gcpu->tlb1_inuse[i]);
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "using tlb1[%d] for gtlb1[%d]\n", idx + bit, entry);

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
	int i = 0;
	int idx = 0;

	do {
		while (gcpu->tlb1_map[entry][i]) {
			int bit = count_lsb_zeroes(gcpu->tlb1_map[entry][i]);
			assert(idx + bit < tlb1_reserved);
			
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "clearing tlb1[%d] for gtlb1[%d], cpu%lu\n",
			         idx + bit, entry, mfspr(SPR_PIR));

			cpu->tlb1[idx + bit].mas1 = 0;
			tlb1_write_entry(idx + bit);

			gcpu->tlb1_map[entry][i] &= ~(1UL << bit);
			gcpu->tlb1_inuse[i] &= ~(1UL << bit);
		}

		i++;
		idx += LONG_BITS;
	} while (idx < TLB1_SIZE);

	gcpu->gtlb1[entry].mas1 &= ~MAS1_VALID;
}

void guest_set_tlb1(unsigned int entry, unsigned long mas1,
                    unsigned long epn, unsigned long grpn,
                    unsigned long mas2flags, unsigned long mas3flags)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned int size = (mas1 >> MAS1_TSIZE_SHIFT) & 15;
	unsigned long size_pages = tsize_to_pages(size);
	unsigned long end = epn + size_pages;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "gtlb1[%d] mapping from %lx to %lx, grpn %lx, mas1 %lx\n",
	         entry, epn, end, grpn, mas1);

	free_tlb1(entry);

	gcpu->gtlb1[entry].mas1 = mas1;
	gcpu->gtlb1[entry].mas2 = (epn << PAGE_SHIFT) | mas2flags;
	gcpu->gtlb1[entry].mas3 = (grpn << PAGE_SHIFT) | mas3flags;
	gcpu->gtlb1[entry].mas7 = grpn >> (32 - PAGE_SHIFT);

	if (!(mas1 & MAS1_VALID))
		return;
	
	while (epn < end) {
		int size = max_page_size(epn, end - epn);

		unsigned long attr, rpn; 
		rpn = vptbl_xlate(guest->gphys, grpn, &attr, PTE_PHYS_LEVELS);

		/* If there's no valid mapping, try again at the next page. Note
		 * that this searching can cause latency.  If you need to do
		 * dynamic TLB1 writes for large pages that include unmapped
		 * guest physical regions, and care about latency, use the
		 * paravirtualized interface.

		 * Unfortunately, we'll have to reflect a TLB miss rather than
		 * a machine check for accesses to these mapping holes,
		 * as TLB1 entries are a limited resource that we don't want
		 * to spend on VF mappings.
		 */

		if (!(attr & PTE_VALID)) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "invalid grpn %lx, epn %lx, skip %lx\n", grpn, epn, rpn);
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
		               ((phys_addr_t)rpn) << PAGE_SHIFT,
		               size,
		               mas2flags, mas3flags,
		               (mas1 >> MAS1_TID_SHIFT) & 0xff,
		               (mas1 >> MAS1_TS_SHIFT) & 1,
		               mas8);
		
		epn += tsize_to_pages(size);
		grpn += tsize_to_pages(size);
	}
}

void guest_inv_tlb1(register_t ivax, int inv_iprot)
{
	gcpu_t *gcpu = get_gcpu();
	int global = ivax & TLBIVAX_INV_ALL;
	register_t va = ivax & TLBIVAX_VA;
	int i;

	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *tlbe = &gcpu->gtlb1[i];
		
		if (!(tlbe->mas1 & MAS1_VALID))
			continue;
		
		if (inv_iprot || !(tlbe->mas1 & MAS1_IPROT)) {
			register_t begin = tlbe->mas2 & MAS2_EPN;
			register_t end = begin;
			
			end += (tsize_to_pages(MAS1_GETTSIZE(tlbe->mas1)) - 1) * PAGE_SIZE;
		
			if (global || (va >= begin && va <= end))
				free_tlb1(i);
		}
	}
}

void guest_reset_tlb(void)
{
	/* Invalidate TLB0.  Note that there's to be no way to partition a
	 * whole-array TLB invalidation, but it's unlikely to be a performance
	 * issue given the rarity of reboots.
	 */	
	asm volatile("tlbivax 0, %0" : : "r" (TLBIVAX_INV_ALL) : "memory");
	tlbsync();

	guest_inv_tlb1(TLBIVAX_INV_ALL, 1);
}

/** Return the index of a conflicting guest TLB1 entry, or -1 if none.
 *
 * @param[in] entry TLB1 entry to ignore during search, or -1 if none.
 * @param[in] mas1 MAS1 value describing entry being added/searched for.
 * @param[in] epn Virtual page number describing the beginning of the
 * mapping to be added, or the page to be searched for.
 */
int guest_find_tlb1(unsigned int entry, unsigned long mas1, unsigned long epn)
{
	gcpu_t *gcpu = get_gcpu();
	int pid = MAS1_GETTID(mas1);
	int i;
	
	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *other = &gcpu->gtlb1[i];
		unsigned long otherepn = other->mas2 >> PAGE_SHIFT;
		int otherpid = MAS1_GETTID(other->mas1);
		
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "checking %x/%lx/%lx against %x/%lx/%lx\n",
		         entry, mas1, epn, i, other->mas1, other->mas2);

		if (entry == i)
			continue;
		if (!(other->mas1 & MAS1_VALID))
			continue;
		if ((mas1 & MAS1_TS) != (other->mas1 & MAS1_TS))
			continue;
		if (pid && otherpid && pid != otherpid)
			continue;
		if (otherepn >= epn + tsize_to_pages(MAS1_GETTSIZE(mas1)))
			continue;
		if (otherepn + tsize_to_pages(MAS1_GETTSIZE(other->mas1)) <= epn)
			continue;

		return i;
	}
	
	return -1;
}                         

void tlbsync(void)
{
	static uint32_t tlbsync_lock;

	register_t saved = spin_lock_critsave(&tlbsync_lock);
	asm volatile("mbar 1; tlbsync" : : : "memory");
	spin_unlock_critsave(&tlbsync_lock, saved);
}

void tlb1_init(void)
{
	int tlb = BASE_TLB_ENTRY;
	
	tlb1_set_entry(--tlb, CCSRBAR_VA, CCSRBAR_PA,
	               CCSRBAR_SIZE, TLB_MAS2_IO,
	               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	phys_addr_t addr = 256 * 1024 * 1024;
	while (addr < 4096ULL * 1024 * 1024 - PHYSBASE) {
		int tsize = natural_alignment(addr >> PAGE_SHIFT);
		tlb1_set_entry(--tlb, PHYSBASE + addr,
		               addr, tsize, TLB_MAS2_MEM,
		               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

		addr += tsize_to_pages(tsize) << PAGE_SHIFT;
	}

	if (mfspr(SPR_PIR))
		tlb1_reserved = tlb;

	cpu->console_ok = 1;
}
