/* @file
 * TLB management
 */
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#include <libos/fsl-booke-tlb.h>
#include <libos/core-regs.h>
#include <libos/bitops.h>

#include <percpu.h>
#include <paging.h>
#include <errors.h>

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

#ifdef CONFIG_TLB_CACHE
/**
 * Find a TLB cache entry, or a slot suitable for use
 *
 * @param[in]  vaddr Virtual (effective) address
 * @param[in]  tag   TLB tag to find
 * @param[out] setp  TLB set containing entry
 * @param[out] way   way within the set
 * @param[in]  ignorespace AS does not need to match (used for invalidation)
 *
 * If a translation for the address exists in the cache, then setp/way is
 * filled in appropriately, and the return value is non-zero.
 *
 * Otherwise, setp and way are filled in with a suitable slot for adding
 * such a translation.  Evicting the current contents of the slot
 * is the responsibility of the caller.
 */

int find_gtlb_entry(uintptr_t vaddr, tlbctag_t tag, tlbcset_t **setp,
                    int *way, int ignorespace)
{
	tlbcset_t *set;
	tlbctag_t mask;
	int index;
	int i;
	
	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
	         "find vaddr %lx tag %lx ignorespace %d\n",
	         vaddr, tag.tag, ignorespace);

	mask.tag = ~0UL;
	mask.pid = 0;
	if (ignorespace)
		mask.space = 0;
	
	index = vaddr >> PAGE_SHIFT;
	index &= (1 << cpu->client.tlbcache_bits) - 1;

	*setp = set = &cpu->client.tlbcache[index];

	for (i = 0; i < TLBC_WAYS; i++) {
		int pid = set->tag[i].pid;

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "pid %d tag.pid %d set->tag %lx mask %lx\n",
		         pid, tag.pid, set->tag[i].tag, mask.tag);

		if (pid != tag.pid && tag.pid != 0 && pid != 0)
			continue;

		if (((tag.tag ^ set->tag[i].tag) & mask.tag) == 0) {
			*way = i;
			return 1;
		}
	}

	return 0;
}

void gtlb0_to_mas(int index, int way)
{
	tlbcset_t *set = &cpu->client.tlbcache[index];
	int bits = cpu->client.tlbcache_bits;

	if (!set->tag[way].valid) {
		mtspr(SPR_MAS1, mfspr(SPR_MAS1) & ~MAS1_VALID);
		return;
	}

	/* We generate a rather useless hint (always zero) when tlbsx/tlbre
	 * finds a valid entry.  The assumption is that it's probably not
	 * going to be used in this case, and it's not worth doing a real
	 * tlbsx to get a good hint.
	 */
	mtspr(SPR_MAS0, MAS0_ESEL(way));
	mtspr(SPR_MAS1, MAS1_VALID |
	                (set->tag[way].pid << MAS1_TID_SHIFT) |
	                (set->tag[way].space << MAS1_TS_SHIFT));
	mtspr(SPR_MAS2, (set->tag[way].vaddr << (PAGE_SHIFT + bits)) |
	                (index << PAGE_SHIFT) |
	                set->entry[way].mas2);

	/* Currently, we only use virtualization faults for bad mappings. */
	if (likely(!(set->entry[way].mas8 & 1))) {
		unsigned long attr;
		unsigned long grpn = (set->entry[way].mas7 << (32 - PAGE_SHIFT)) |
		                     (set->entry[way].mas3 >> MAS3_RPN_SHIFT);
		unsigned long rpn = vptbl_xlate(get_gcpu()->guest->gphys_rev,
		                                grpn, &attr, PTE_PHYS_LEVELS);

		assert(attr & PTE_VALID);

		mtspr(SPR_MAS3, (rpn << PAGE_SHIFT) |
		                (set->entry[way].mas3 & (MAS3_FLAGS | MAS3_USER)));
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	} else {
		mtspr(SPR_MAS3, set->entry[way].mas3);
		mtspr(SPR_MAS7, set->entry[way].mas7);
	}
}

static void guest_inv_tlb0_all(int pid)
{
	tlbcset_t *set = cpu->client.tlbcache;
	unsigned int num_sets = 1 << cpu->client.tlbcache_bits;
	int i, j;

	for (i = 0; i < num_sets; i++)
		for (j = 0; j < TLBC_WAYS; j++)
			if (pid == set[i].tag[j].pid || pid < 0)
				set[i].tag[j].valid = 0;
}

static void guest_inv_tlb0_va(register_t va, int pid)
{
	tlbcset_t *set;
	tlbctag_t tag = make_tag(va, pid < 0 ? 0 : pid, 0);
	int way;

	if (find_gtlb_entry(va, tag, &set, &way, 1))
		set->tag[way].valid = 0;
}

void dtlb_miss_fast(void);
void itlb_miss_fast(void);

void tlbcache_init(void)
{
	cpu->client.tlbcache_bits = 12;
	cpu->client.tlbcache =
		alloc(sizeof(tlbcset_t) << cpu->client.tlbcache_bits, PAGE_SIZE);

	mtspr(SPR_SPRG3, ((uintptr_t)cpu->client.tlbcache) |
	                 cpu->client.tlbcache_bits);
	mtspr(SPR_IVOR13, (uintptr_t)dtlb_miss_fast);
	mtspr(SPR_IVOR14, (uintptr_t)itlb_miss_fast);
}
#endif /* TLB cache */

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

static void guest_inv_tlb1(register_t va, int pid, int flags, int global)
{
	gcpu_t *gcpu = get_gcpu();
	int i;

	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *tlbe = &gcpu->gtlb1[i];
		
		if (!(tlbe->mas1 & MAS1_VALID))
			continue;
		
		if ((flags & INV_IPROT) || !(tlbe->mas1 & MAS1_IPROT)) {
			register_t begin = tlbe->mas2 & MAS2_EPN;
			register_t end = begin;
			
			end += (tsize_to_pages(MAS1_GETTSIZE(tlbe->mas1)) - 1) * PAGE_SIZE;
		
			if (!global && (va < begin || va > end))
				continue;

			if (pid >= 0 && pid != (tlbe->mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT)
				continue;

			free_tlb1(i);
		}
	}
}

void guest_inv_tlb(register_t ivax, int pid, int flags)
{
	int global = ivax & TLBIVAX_INV_ALL;
	register_t va = ivax & TLBIVAX_VA;

#ifdef CONFIG_TLB_CACHE
	if (flags & INV_TLB0) {
		if (global)
			guest_inv_tlb0_all(pid);
		else
			guest_inv_tlb0_va(va, pid);
	}
#endif

	if (flags & INV_TLB1)
		guest_inv_tlb1(va, pid, flags, global);
}

int guest_set_tlb0(register_t mas0, register_t mas1, register_t mas2,
                   register_t mas3, unsigned long rpn, register_t mas8)
{
#ifndef CONFIG_TLB_CACHE
	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	mtspr(SPR_MAS3, (rpn << PAGE_SHIFT) | mas3);
	mtspr(SPR_MAS8, mas8);
	asm volatile("tlbwe" : : : "memory");
#else
	tlbcset_t *set;
	tlbcentry_t *entry;
	uintptr_t vaddr = mas2 & MAS2_EPN;
	tlbctag_t tag = make_tag(vaddr, MAS1_GETTID(mas1),
	                         (mas1 & MAS1_TS) >> MAS1_TS_SHIFT);
	int way, ret;

	ret = find_gtlb_entry(vaddr, tag, &set, &way, 0);
	if (ret) {
		if (unlikely(way != MAS0_GET_TLB0ESEL(mas0))) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "existing: tag 0x%08lx entry 0x%08x 0x%08x way %d\n",
			         set->tag[way].tag, set->entry[way].mas3,
			         set->entry[way].pad, way);

			return ERR_BUSY;
		}

		asm volatile("tlbilxva 0, %0" : : "r" (vaddr));
	}

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "setting TLB0 for 0x%08lx (%#lx)\n", vaddr, rpn);
	way = MAS0_GET_TLB0ESEL(mas0);

	entry = &set->entry[way];
	entry->mas2 = mas2;
	entry->mas3 = (rpn << PAGE_SHIFT) | mas3;
	entry->mas7 = rpn >> (32 - PAGE_SHIFT);
	entry->tsize = MAS1_GETTSIZE(mas1);
	entry->mas8 = mas8 >> 30;

	set->tag[way] = tag;
#endif

	return 0;
}

void guest_reset_tlb(void)
{
	mtspr(SPR_MMUCSR0, MMUCSR_L2TLB0_FI);
	guest_inv_tlb(TLBIVAX_INV_ALL, -1, INV_TLB0 | INV_TLB1 | INV_IPROT);
	isync();
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
