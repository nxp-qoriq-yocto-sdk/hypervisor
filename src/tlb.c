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

static int alloc_tlb1(unsigned int entry, int evict)
{
	gcpu_t *gcpu = get_gcpu();
	int idx = 0;
	int i = 0;

	do {
		while (~gcpu->tlb1_inuse[i]) {
			int bit = count_lsb_zeroes(~gcpu->tlb1_inuse[i]);

			if (idx + bit >= tlb1_reserved)
				goto none_avail;
			
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

none_avail:
	if (evict) {
		i = gcpu->evict_tlb1++;
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
		         "alloc_tlb1: evicting entry %d\n", i);
		
		if (gcpu->evict_tlb1 >= tlb1_reserved)
			gcpu->evict_tlb1 = 0;

		return i;
	}

	return -1;
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

int find_gtlb_entry(uintptr_t vaddr, tlbctag_t tag, tlbcset_t **setp, int *way)
{
	tlbcset_t *set;
	tlbctag_t mask;
	int index;
	int i;
	
	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
	         "find vaddr %lx tag %lx\n", vaddr, tag.tag);

	mask.tag = ~0UL;
	mask.pid = 0;
	
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

/**
 * Check for a TLB0 cache entry that conflicts with a new TLB1 entry.
 *
 * @param[in]  epn   Effective page number
 * @param[in]  tsize Size of the TLB1 mapping
 * @param[in]  pid   PID of mapping
 * @param[in]  space zero if AS0, 1 if AS1
 * @return non-zero if a conflicting entry was found
 */
int check_tlb1_conflict(uintptr_t epn, int tsize, int pid, int space)
{
 	uintptr_t pages = tsize_to_pages(tsize);
 	int mask = (1 << cpu->client.tlbcache_bits) - 1;
	int cache_entries = min(pages, cpu->client.tlbcache_bits);
	int index = epn & mask;
	int end = index + cache_entries;

	tlbcset_t *set = &cpu->client.tlbcache[index];
	
	uintptr_t tag_start = epn >> cpu->client.tlbcache_bits;
	uintptr_t tag_end = (epn + pages - 1) >> cpu->client.tlbcache_bits;

	for (; index < end; set++, index++) {
		int way;

		for (way = 0; way < TLBC_WAYS; way++) {
			if (!set->tag[way].valid)
				continue;

			if (set->tag[way].vaddr < tag_start ||
			    set->tag[way].vaddr > tag_end)
				continue;

			if (pid != 0 && pid != set->tag[way].pid)
				continue;

			if (space != set->tag[way].space)
				continue;

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "check_tlb1_conflict: tag 0x%08lx entry 0x%08x 0x%08x way %d\n",
			         set->tag[way].tag, set->entry[way].mas3,
			         set->entry[way].pad, way);

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
	                (set->tag[way].space << MAS1_TS_SHIFT) |
	                (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT));
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

static void guest_inv_tlb0_va(register_t vaddr, int pid)
{
	tlbcset_t *set;
	tlbctag_t tag = make_tag(vaddr, pid < 0 ? 0 : pid, 0);
	tlbctag_t mask;
	int index;
	int i;
	
	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
	         "inv vaddr %lx pid %d\n", vaddr, pid);

	mask.tag = ~0UL;
	mask.space = 0;
	
	if (pid < 0)
		mask.pid = 0;
	
	index = vaddr >> PAGE_SHIFT;
	index &= (1 << cpu->client.tlbcache_bits) - 1;

	set = &cpu->client.tlbcache[index];

	for (i = 0; i < TLBC_WAYS; i++) {
		int pid = set->tag[i].pid;

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "inv pid %d tag.pid %d set->tag %lx mask %lx\n",
		         pid, tag.pid, set->tag[i].tag, mask.tag);

		if (((tag.tag ^ set->tag[i].tag) & mask.tag) == 0)
			set->tag[i].valid = 0;
	}
}

static int guest_set_tlbcache(register_t mas0, register_t mas1,
                              register_t mas2, register_t mas3flags,
                              unsigned long rpn, register_t mas8,
                              register_t guest_mas3flags)
{
	tlbcset_t *set;
	tlbcentry_t *entry;
	uintptr_t vaddr = mas2 & MAS2_EPN;
	tlbctag_t tag = make_tag(vaddr, MAS1_GETTID(mas1),
	                         (mas1 & MAS1_TS) >> MAS1_TS_SHIFT);
	int way, ret;

	assert(!(mas0 & MAS0_TLBSEL1));

	ret = find_gtlb_entry(vaddr, tag, &set, &way);

	if (unlikely(!(mas1 & MAS1_VALID)))
		tag.valid = 0;

	if (ret && tag.valid &&
	    unlikely(way != MAS0_GET_TLB0ESEL(mas0))) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "existing: tag 0x%08lx entry 0x%08x 0x%08x way %d\n",
		         set->tag[way].tag, set->entry[way].mas3,
		         set->entry[way].pad, way);

		return ERR_BUSY;
	}

	way = MAS0_GET_TLB0ESEL(mas0);

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "setting TLB0 for 0x%08lx (%#lx), way %d\n", vaddr, rpn, way);

	/* If we're replacing a valid entry, invalidate it. */
	if (set->tag[way].valid) {
		int tagshift = cpu->client.tlbcache_bits + PAGE_SHIFT;
		register_t mask = (1 << tagshift) - 1;
		register_t oldvaddr = (vaddr & mask) |
		                      (set->tag[way].vaddr << tagshift);

		set->tag[way].valid = 0;
		mtspr(SPR_MAS6, set->tag[way].pid << MAS6_SPID_SHIFT);
		tlb_inv_addr(oldvaddr);
	}

	entry = &set->entry[way];
	entry->mas2 = mas2;
	entry->mas3 = (rpn << PAGE_SHIFT) | mas3flags;
	entry->mas7 = rpn >> (32 - PAGE_SHIFT);
	entry->tsize = 1;
	entry->mas8 = mas8 >> 30;
	entry->gmas3 = guest_mas3flags;

	set->tag[way] = tag;
	return 0;
}

/** Try to handle a TLB miss with the guest TLB1 array.
 *
 * @param[in] vaddr Virtual (effective) faulting address.
 * @param[in] space 1 if the fault should be filled from AS1, 0 if from AS0.
 * @param[in] pid The value of SPR_PID.
 * @return TLB_MISS_REFLECT, TLB_MISS_HANDLED, or TLB_MISS_MCHECK
 */
int guest_tlb1_miss(register_t vaddr, int space, int pid)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long epn = vaddr >> PAGE_SHIFT;
	int i;
	
	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *entry = &gcpu->gtlb1[i];
		unsigned long entryepn = entry->mas2 >> PAGE_SHIFT;
		unsigned long grpn, rpn, baserpn, attr;
		int entrypid = MAS1_GETTID(entry->mas1);
		int tsize = MAS1_GETTSIZE(entry->mas1);
		int mapsize, mappages, index;
		
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "checking %x/%lx/%lx for %lx/%d/%d\n",
		         i, entry->mas1, entry->mas2, vaddr, space, pid);

		if (!(entry->mas1 & MAS1_VALID))
			continue;
		if ((space << MAS1_TS_SHIFT) != (entry->mas1 & MAS1_TS))
			continue;
		if (pid && entrypid && pid != entrypid)
			continue;
		if (entryepn > epn)
			continue;
		if (entryepn + tsize_to_pages(tsize) <= epn)
			continue;

		grpn = (entry->mas3 >> PAGE_SHIFT) | (entry->mas7 << (32 - PAGE_SHIFT));
		grpn += epn - entryepn;
		rpn = vptbl_xlate(gcpu->guest->gphys, grpn, &attr, PTE_PHYS_LEVELS);

		if (unlikely(!(attr & PTE_VALID)))
			return TLB_MISS_MCHECK;

		tsize = min(tsize, attr >> PTE_SIZE_SHIFT);
		baserpn = rpn & ~(tsize_to_pages(tsize) - 1);

		mapsize = max_page_tsize(baserpn, tsize);
		mappages = tsize_to_pages(mapsize);
		
		rpn &= ~(mappages - 1);
		epn &= ~(mappages - 1);

		disable_critint();
		save_mas(gcpu);

		index = alloc_tlb1(i, 1);

		tlb1_set_entry(index, epn << PAGE_SHIFT,
		               ((phys_addr_t)rpn) << PAGE_SHIFT,
		               mapsize, entry->mas2,
		               entry->mas3 & ~MAS3_RPN,
		               pid, space, MAS8_GTS);

		restore_mas(gcpu);
		enable_critint();

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
		         "guest_tlb1_miss: inserting %d %lx %lx %d %lx %lx %d %d\n",
		         index, epn, rpn, mapsize, entry->mas2,
		         entry->mas3 & ~MAS3_RPN, pid, space);
		return TLB_MISS_HANDLED;
	}

	return TLB_MISS_REFLECT;
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

/** Check whether an ISI should be reflected as an ISI, or a machine check.
 *
 * @param[in] vaddr Virtual (effective) faulting address.
 * @param[in] space 1 if the address is in AS1, 0 if in AS0.
 * @param[in] pid The value of SPR_PID.
 * @return TLB_MISS_REFLECT or TLB_MISS_MCHECK
 */
int guest_tlb_isi(register_t vaddr, int space, int pid)
{
	gcpu_t *gcpu = get_gcpu();
	int ret = TLB_MISS_REFLECT;

	disable_critint();
	save_mas(gcpu);

	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | space);
	asm volatile("tlbsx 0, %0" : : "r" (vaddr));

	if ((mfspr(SPR_MAS1) & MAS1_VALID) &&
	    (mfspr(SPR_MAS8) & MAS8_VF)) {
		ret = TLB_MISS_MCHECK;

#ifdef CONFIG_TLB_CACHE
		/* FIXME: If the original permission bit was clear, reflect an ISI
		 * even if VF was set.  We can't detect this without the TLB cache.
		 */
#endif
	}

	restore_mas(gcpu);
	enable_critint();
	
	return ret;
}

void guest_set_tlb1(unsigned int entry, unsigned long mas1,
                    unsigned long epn, unsigned long grpn,
                    unsigned long mas2flags, unsigned long mas3flags)
{
	gcpu_t *gcpu = get_gcpu();
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

#ifndef CONFIG_TLB_CACHE
	guest_t *guest = gcpu->guest;

	while (epn < end) {
		int size = max_page_size(epn, end - epn);

		unsigned long attr, rpn; 
		rpn = vptbl_xlate(guest->gphys, grpn, &attr, PTE_PHYS_LEVELS);

		/* If there's no valid mapping, try again at the next page. Note
		 * that this can be slow.  
       *
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

		int real_entry = alloc_tlb1(entry, 0);
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
#endif
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

	if (flags & INV_TLB0) {
#ifdef CONFIG_TLB_CACHE
		if (global)
			guest_inv_tlb0_all(pid);
		else
			guest_inv_tlb0_va(va, pid);
#endif

		if (global) {
			if (pid < 0)
				tlb_inv_lpid();
			else
				tlb_inv_pid();
		} else {
			if (pid < 0) {
				/* tlbilxva requires a search PID, but tlbivax
				 * doesn't provide one, so we read each TLB0 way
				 * to see if there's something to shoot down.
				 */

				register_t mas1;
				int ways = mfspr(SPR_TLB0CFG) & TLBCFG_NENTRY_MASK;
				int way;

				mtspr(SPR_MAS2, va);

				for (way = 0; way < ways; way++) {
					mtspr(SPR_MAS0, MAS0_TLBSEL0 | MAS0_ESEL(way));
					asm volatile("tlbre");
					mas1 = mfspr(SPR_MAS1);

					if (mas1 & MAS1_VALID) {
						mtspr(SPR_MAS6, mas1 & MAS1_TID_MASK);
						tlb_inv_addr(va);
					}
				}
			} else {
				assert((mfspr(SPR_MAS6) & MAS6_SPID_MASK) ==
				       (pid << MAS6_SPID_SHIFT));
				tlb_inv_addr(va);
			}
		}
	}

	if (flags & INV_TLB1)
		guest_inv_tlb1(va, pid, flags, global);
}

int guest_set_tlb0(register_t mas0, register_t mas1, register_t mas2,
                   register_t mas3flags, unsigned long rpn, register_t mas8,
                   register_t guest_mas3flags)
{
#ifndef CONFIG_TLB_CACHE
	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	mtspr(SPR_MAS3, (rpn << PAGE_SHIFT) | mas3flags);
	mtspr(SPR_MAS8, mas8);
	asm volatile("tlbwe" : : : "memory");
	return 0;
#else
	return guest_set_tlbcache(mas0, mas1, mas2, mas3flags,
	                          rpn, mas8, guest_mas3flags);
#endif
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
