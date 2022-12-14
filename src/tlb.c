/* @file

 * TLB management
 */

/*
 * Copyright (C) 2007-2012 Freescale Semiconductor, Inc.
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
#include <libos/list.h>
#include <libos/alloc.h>
#include <libos/cache.h>

#include <percpu.h>
#include <paging.h>
#include <errors.h>
#include <benchmark.h>

static void tlb1_set_entry_safe(unsigned int idx, unsigned long va,
                                phys_addr_t pa, register_t tsize,
                                register_t mas1flags, register_t mas2flags,
                                register_t mas3flags, unsigned int tid,
                                register_t mas8)
{
	register_t mas0, mas1, mas2, mas3, mas7, saved_mas8;
	register_t saved;

	saved = disable_int_save();

	mas0 = mfspr(SPR_MAS0);
	mas1 = mfspr(SPR_MAS1);
	mas2 = mfspr(SPR_MAS2);
	mas3 = mfspr(SPR_MAS3);
	mas7 = mfspr(SPR_MAS7);
	saved_mas8 = mfspr(SPR_MAS8);

	tlb1_set_entry(idx, va, pa, tsize, mas1flags, mas2flags, mas3flags, tid, mas8);

	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS3, mas3);
	mtspr(SPR_MAS7, mas7);
	mtspr(SPR_MAS8, saved_mas8);

	restore_int(saved);
}


static void free_tlb1(unsigned int entry, int write_tlb)
{
	gcpu_t *gcpu = get_gcpu();
	int i = 0;
	int idx = 0;
	shared_cpu_t *shared_cpu = get_shared_cpu();
	register_t saved;

	saved = tlb_lock();
	do {
		while (gcpu->tlb1_map[entry][i]) {
			int bit = count_lsb_zeroes(gcpu->tlb1_map[entry][i]);
			assert(idx + bit <= GUEST_TLB_END);

			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "clearing tlb1[%d] for gtlb1[%d], cpu%lu\n",
			         idx + bit, entry, mfspr(SPR_PIR));

			cpu->tlb1[idx + bit].mas1 = 0;

			if (write_tlb)
				tlb1_write_entry(idx + bit);

			gcpu->tlb1_map[entry][i] &= ~(1UL << bit);
			shared_cpu->tlb1_inuse[i] &= ~(1UL << bit);
		}

		i++;
		idx += LONG_BITS;
	} while (idx < TLB1_SIZE);
	tlb_unlock(saved);

	gcpu->gtlb1[entry].mas1 &= ~MAS1_VALID;
}

static int alloc_tlb1(unsigned int entry, int evict)
{
	gcpu_t *gcpu = get_gcpu();
	int idx = 0;
	int i = 0;
	shared_cpu_t *shared_cpu = get_shared_cpu();
	register_t saved;

	saved = tlb_lock();
	do {
		while (~shared_cpu->tlb1_inuse[i]) {
			int bit = count_lsb_zeroes(~shared_cpu->tlb1_inuse[i]);

			if (idx + bit > GUEST_TLB_END)
				goto none_avail;

			shared_cpu->tlb1_inuse[i] |= 1UL << bit;
			gcpu->tlb1_map[entry][i] |= 1UL << bit;

			tlb_unlock(saved);

			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "tlb1_inuse[%d] now %lx\n", i, shared_cpu->tlb1_inuse[i]);
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "using tlb1[%d] for gtlb1[%d]\n", idx + bit, entry);

			return idx + bit;
		}

		idx += LONG_BITS;
		i++;
	} while (idx < TLB1_SIZE);

none_avail:
	if (evict) {
		int bit;

		i = shared_cpu->evict_tlb1++;

		idx = i / LONG_BITS;
		bit = i % LONG_BITS;

		gcpu->tlb1_map[entry][idx] |= 1UL << bit;
		for (int j = 0; j < TLB1_GSIZE; j++) {
			if (j != entry && (gcpu->tlb1_map[j][idx] & (1UL << bit))) {
				printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
				         "%s[%d]: evicting entry %d used by %d\n",
				         __func__, entry, i, j);

				cpu->tlb1[i].mas1 = 0;
				tlb1_write_entry(i);
				gcpu->tlb1_map[j][idx] &= ~(1UL << bit);
			}
		}

		if (shared_cpu->evict_tlb1 > GUEST_TLB_END)
			shared_cpu->evict_tlb1 = 0;

		tlb_unlock(saved);
		return i;
	}
	tlb_unlock(saved);

	return -1;
}

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

int find_gtlb_entry(uintptr_t vaddr, tlbctag_t tag,
                    tlbcset_t **setp, unsigned int *way)
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
int check_tlb1_conflict(uintptr_t epn, unsigned int tsize,
                        unsigned int pid, unsigned int space)
{
 	uintptr_t pages = tsize_to_pages(tsize);
 	int mask = (1 << cpu->client.tlbcache_bits) - 1;
	unsigned int cache_entries = min(pages, 1 << cpu->client.tlbcache_bits);
	unsigned int index = epn & mask;
	unsigned int end = index + cache_entries;

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

void gtlb0_to_mas(unsigned int index, unsigned int way, gcpu_t *gcpu)
{
	tlbcset_t *set = &gcpu->cpu->client.tlbcache[index];
	int bits = gcpu->cpu->client.tlbcache_bits;
	register_t mas3;

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

	unsigned long attr;
	unsigned long grpn = (set->entry[way].mas7 << (32 - PAGE_SHIFT)) |
	                     (set->entry[way].mas3 >> MAS3_RPN_SHIFT);
	unsigned long rpn = vptbl_xlate(gcpu->guest->gphys_rev,
	                                grpn, &attr, PTE_PHYS_LEVELS, 0);

	/* Currently, we only use virtualization faults for bad mappings. */
	if (likely(!(set->entry[way].mas8 & 1))) {
		assert(attr & PTE_VALID);

		mas3 = (uint32_t)(rpn << PAGE_SHIFT) |
		       (set->entry[way].mas3 & MAS3_USER);
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	} else {
		mas3 = set->entry[way].mas3 & ~PTE_MAS3_MASK;
		mtspr(SPR_MAS7, set->entry[way].mas7);
	}

	mtspr(SPR_MAS3, mas3 | set->entry[way].gmas3);
}

static void guest_inv_tlb0_all(void)
{
	unsigned long bm_start = bench_start();

	if (!get_gcpu()->clean_tlb) {
		memset(cpu->client.tlbcache, 0,
		       sizeof(tlbcset_t) << cpu->client.tlbcache_bits);

		get_gcpu()->clean_tlb = 1;
	}

	bench_stop(bm_start, bm_tlb0_inv_all);
}

static void guest_inv_tlb0_pid(int pid)
{
	unsigned long bm_start;
	tlbcset_t *set;
	unsigned int num_sets;
	unsigned int i, j;

	/* Optimize away repeated invalidations to the same PID. */
	if (get_gcpu()->clean_tlb_pid == pid)
		return;

	bm_start = bench_start();
	set = cpu->client.tlbcache;
	num_sets = 1 << cpu->client.tlbcache_bits;

	for (i = 0; i < num_sets; i++) {
		prefetch_store(&set[i + 4]);

		for (j = 0; j < TLBC_WAYS; j++)
			if (pid == set[i].tag[j].pid)
				set[i].tag[j].valid = 0;
	}

	get_gcpu()->clean_tlb_pid = pid;

	bench_stop(bm_start, bm_tlb0_inv_pid);
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
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "inv pid %d tag.pid %d set->tag %lx mask %lx\n",
		         set->tag[i].pid, tag.pid, set->tag[i].tag, mask.tag);

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
	unsigned int way;
	int ret;

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

	way = MAS0_GET_TLB0ESEL(mas0) & (TLBC_WAYS - 1);

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
	entry->mas3 = (uint32_t)(rpn << PAGE_SHIFT) | mas3flags;
	entry->mas7 = rpn >> (32 - PAGE_SHIFT);
	entry->tsize = 1;
	entry->mas8 = mas8 >> 30;
	entry->gmas3 = guest_mas3flags;

	get_gcpu()->clean_tlb = 0;
	get_gcpu()->clean_tlb_pid = -1;

	set->tag[way] = tag;

	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	mtspr(SPR_MAS3, (uint32_t)(rpn << PAGE_SHIFT) | mas3flags);
	mtspr(SPR_MAS8, mas8);
	asm volatile("isync; tlbwe" : : : "memory");

	return 0;
}

/** Try to handle a TLB miss with the guest TLB1 array.
 *
 * @param[in] vaddr Virtual (effective) faulting address.
 * @param[in] space 1 if the fault should be filled from AS1, 0 if from AS0.
 * @param[in] pid The value of SPR_PID.
 * @return TLB_MISS_REFLECT, TLB_MISS_HANDLED, or TLB_MISS_MCHECK
 */
int guest_tlb1_miss(register_t vaddr, unsigned int space, unsigned int pid)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long epn = vaddr >> PAGE_SHIFT;
	int i;

	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *entry = &gcpu->gtlb1[i];
		unsigned long entryepn = entry->mas2 >> PAGE_SHIFT;
		unsigned long grpn, rpn, baserpn, attr;
		unsigned int entrypid = MAS1_GETTID(entry->mas1);
		unsigned int tsize = MAS1_GETTSIZE(entry->mas1);
		unsigned int mapsize, mappages, index, tsize_rpn, offset = 0;

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		         "checking %x/%lx/%lx for %lx/%d/%d\n",
		         i, entry->mas1, entry->mas2, vaddr, space, pid);

		if (!(entry->mas1 & MAS1_VALID))
			continue;
		if ((space << MAS1_TS_SHIFT) != (entry->mas1 & MAS1_TS))
			continue;
		if (entrypid && pid != entrypid)
			continue;
		if (entryepn > epn)
			continue;
		if (entryepn + tsize_to_pages(tsize) <= epn)
			continue;

		if (entry->mas1 & MAS1_IND)
			offset = MAS3_GETSPSIZE(entry->mas3) + 7;

		grpn = (entry->mas3 >> PAGE_SHIFT) | (entry->mas7 << (32 - PAGE_SHIFT));

		grpn += (epn - entryepn) >> offset;

		rpn = vptbl_xlate(gcpu->guest->gphys, grpn, &attr, PTE_PHYS_LEVELS, 0);

		if (unlikely(!(attr & PTE_VALID)))
			return TLB_MISS_MCHECK;

		tsize_rpn = tsize - offset;

		tsize_rpn = min(tsize_rpn, attr >> PTE_SIZE_SHIFT);
		rpn = rpn & ~(tsize_to_pages_roundup(tsize_rpn) - 1);

		mapsize = tsize_rpn + offset;
		mappages = tsize_to_pages(mapsize);

		epn &= ~(mappages - 1);

		disable_int();
		save_mas(gcpu);

		index = alloc_tlb1(i, 1);

		tlb1_set_entry(index, epn << PAGE_SHIFT,
		               ((phys_addr_t)rpn) << PAGE_SHIFT, mapsize,
		               MAS1_IPROT | (entry->mas1 & MAS1_IND)
		                | (space ? MAS1_TS : 0),
		               entry->mas2, (entry->mas3 & ~MAS3_RPN)
				& (attr & PTE_MAS3_MASK),
		               pid, MAS8_GTS | gcpu->lpid);

		restore_mas(gcpu);
		enable_int();

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

	cpu->client.tlbcache_enable = 1;

	mtspr(SPR_SPRG3, ((uintptr_t)cpu->client.tlbcache) |
	                 cpu->client.tlbcache_bits);
	mtspr(SPR_IVOR13, (uintptr_t)dtlb_miss_fast);
	mtspr(SPR_IVOR14, (uintptr_t)itlb_miss_fast);
}

/** Check whether an ISI should be reflected as an ISI, or a machine check.
 *
 * @param[in] vaddr Virtual (effective) faulting address.
 * @param[in] space 1 if the address is in AS1, 0 if in AS0.
 * @param[in] pid The value of SPR_PID.
 * @return TLB_MISS_REFLECT or TLB_MISS_MCHECK
 */
int guest_tlb_isi(register_t vaddr, unsigned int space, unsigned int pid)
{
	gcpu_t *gcpu = get_gcpu();
	int ret = TLB_MISS_REFLECT;

	disable_int();
	save_mas(gcpu);

	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | space);
	asm volatile("isync; tlbsx 0, %0" : : "r" (vaddr));

	if ((mfspr(SPR_MAS1) & MAS1_VALID) &&
	    (mfspr(SPR_MAS8) & MAS8_VF)) {
		ret = TLB_MISS_MCHECK;

		if (cpu->client.tlbcache_enable) {
			/* FIXME: If the original permission bit was clear, reflect an ISI
			 * even if VF was set.  We can't detect this without the TLB cache.
			*/
		}
	}

	restore_mas(gcpu);
	enable_int();

	return ret;
}

void guest_set_tlb1(unsigned int entry, unsigned long mas1,
                    unsigned long epn, unsigned long grpn,
                    unsigned long mas2flags, unsigned long mas3flags)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned int size = (mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
	unsigned long size_pages = tsize_to_pages(size);
	int offset = 0;
	unsigned long end;

	/* there is no restrictions for the page offset bit to be cleared by
	 * the guest */
	epn &= (~(size_pages - 1));
	end = epn + size_pages;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "gtlb1[%d] mapping from %lx to %lx, grpn %lx, mas1 %lx\n",
	         entry, epn, end, grpn, mas1);

	free_tlb1(entry, 1);

	gcpu->gtlb1[entry].mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(entry);
	gcpu->gtlb1[entry].mas1 = mas1;
	gcpu->gtlb1[entry].mas2 = (epn << PAGE_SHIFT) | mas2flags;
	gcpu->gtlb1[entry].mas3 = (uint32_t)(grpn << PAGE_SHIFT) | mas3flags;
	gcpu->gtlb1[entry].mas7 = grpn >> (32 - PAGE_SHIFT);

	if (!(mas1 & MAS1_VALID))
		return;

	if (cpu->client.tlb1_virt)
		return;

	guest_t *guest = gcpu->guest;

	if (mas1 & MAS1_IND)
		offset = MAS3_GETSPSIZE(mas3flags) + 7;

	while (epn < end) {
		unsigned long attr, rpn;
		int size_epn = max_page_size(epn, end - epn);
		int size_rpn = size_epn - offset;

		rpn = vptbl_xlate(guest->gphys, grpn, &attr, PTE_PHYS_LEVELS, 0);

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
			epn = (epn | (((rpn + 1) << offset) - 1)) + 1;
			grpn = (grpn | rpn) + 1;
			continue;
		}

		mas3flags &= attr & PTE_MAS3_MASK;

		unsigned long mas8 = gcpu->lpid;
		mas8 |= (attr << PTE_MAS8_SHIFT) & PTE_MAS8_MASK;

		size_rpn = min(size_rpn, attr >> PTE_SIZE_SHIFT);
		size_epn = size_rpn + offset;

		int real_entry = alloc_tlb1(entry, 0);
		if (real_entry < 0) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_ALWAYS, "Out of TLB1 entries!\n");
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_ALWAYS,
			         "entry %d, base 0x%lx, size 0x%llx\n",
			         entry, epn << PAGE_SHIFT, ((uint64_t)size) << PAGE_SHIFT);

			// FIXME: reflect machine check
			BUG();
		}

#ifdef CONFIG_FAST_TLB1
		gcpu->fast_tlb1_to_gtlb1[real_entry] = -1;
#endif

		tlb1_set_entry_safe(real_entry, epn << PAGE_SHIFT,
		                    ((phys_addr_t)rpn) << PAGE_SHIFT, size_epn,
		                    mas1 & (MAS1_IND | MAS1_TS | MAS1_IPROT),
		                    mas2flags, mas3flags,
		                    (mas1 >> MAS1_TID_SHIFT) & 0xff, mas8);

		epn += tsize_to_pages(size_epn);
		grpn += tsize_to_pages_roundup(size_rpn);
	}
}

static int nonsplit_gtlb1_to_tlb1(int entry)
{
	int i = 0, real_entry = 0;

	do {
		if (get_gcpu()->tlb1_map[entry][i]) {
			real_entry += count_lsb_zeroes(get_gcpu()->tlb1_map[entry][i]);
			return real_entry;
		}

		i++;
		real_entry += LONG_BITS;
	} while (real_entry <= GUEST_TLB_END);

	return -1;
}

void check_invalidated_gtlb1(int skip_entry)
{
	gcpu_t *gcpu = get_gcpu();
	int real_entry, i;

	for (i = 0; i < TLB1_GSIZE; i++) {
		/* Skip the split entry that was just changed */
		if (i == skip_entry)
			continue;

		/* Skip invalid or IPROT gtlb1 entries */
		if ((gcpu->gtlb1[i].mas1 & (MAS1_VALID | MAS1_IPROT)) != MAS1_VALID)
			continue;

		real_entry = nonsplit_gtlb1_to_tlb1(i);
		assert(real_entry >= 0);

		mtspr(SPR_MAS0, MAS0_ESEL(real_entry) | MAS0_TLBSEL(1));
		asm volatile("isync; tlbre" : : : "memory");

		/* Still valid? */
		if (mfspr(SPR_MAS1) & MAS1_VALID)
			continue;

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
		         "%s: invalidating gtlb1 entry %d\n",
		         __func__, i);

		gcpu->gtlb1[i].mas1 &= ~MAS1_VALID;
	}
}

unsigned long update_dgtmi(register_t mas0, register_t mas1)
{
	unsigned int i, entry;
	unsigned long  new_split_gtlb1_map;
	gcpu_t *gcpu = get_gcpu();

	/* Both "direct guest tlb management" and "direct guest tlb miss"
	 * must be enabled. One might question: why is "direct guest tlb miss"
	 * a must?
	 * Consider the following case with "direct guest tlb miss" off:
	 * - a non-split gtlb1 entry with it's associated real tlb1 entry
	 * - the real tlb1 entry gets evicted by another newly added gtlb1 entry
	 * - a native tlbilx on our non-split entry won't hit any real tlb1 entry
	 * - the gtlb1 entry will remain in the hv structures even if the guest
	 * - invalidated it, which is wrong
	 * - since eviction happens only with "direct guest tlb miss" off,
	 *   we must ensure it's on, in order to avoid described case
	 */
	if (!gcpu->guest->direct_guest_tlb_mgt ||
	    !gcpu->guest->direct_guest_tlb_miss)
		return 0;

	assert(mas0 & MAS0_TLBSEL1);

	new_split_gtlb1_map = gcpu->split_gtlb1_map;
	entry = MAS0_GET_TLB1ESEL(mas0) & (TLB1_GSIZE - 1);
	if ((mas1 & (MAS1_VALID | MAS1_IPROT)) == MAS1_VALID) {
		int splits = 0;
		unsigned long tlb1_map;

		for (i = 0; i < (TLB1_SIZE + LONG_BITS - 1) / LONG_BITS; i++) {

			tlb1_map = gcpu->tlb1_map[entry][i];

			if (!tlb1_map)
				continue;

			/* tlb1_map != 0 => at least one split */
			splits++;
			/* tlb1_map not a power of 2 => multiple splits */
			if (tlb1_map & (tlb1_map - 1))
				splits++;

			if (splits > 1) {
				new_split_gtlb1_map |= (1 << entry);
				break;
			}
		}
	} else {
		new_split_gtlb1_map &= ~(1 << entry);
	}

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "%s: old_map = %lx new_map = %lx\n",
	         __func__, gcpu->split_gtlb1_map, new_split_gtlb1_map);

	if (!gcpu->split_gtlb1_map && new_split_gtlb1_map) {
		int real_entry;

		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
		         "%s: disabling guest tlb mgmt instrs (entry = %d)\n",
		         __func__, entry);

		/* disable "guest tlb mgmt instrs" */
		mtspr(SPR_EPCR, mfspr(SPR_EPCR) | EPCR_DGTMI);

		/* While running with DGTMI = 0 a native tlbilx instr might
		 * have invalidated one of the tlb1 entries without hv's
		 * knowledge.
		 * Iterate gtlb1 entries, check if the real tlb1 entry
		 * backing them up was invalidated and if so, also invalidate
		 * them in hv structs.
		 */
		save_mas(gcpu);
		check_invalidated_gtlb1(entry);
		restore_mas(gcpu);
	} else if (gcpu->split_gtlb1_map && !new_split_gtlb1_map) {
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
		         "%s: enabling guest tlb mgmt instrs (entry = %d)\n",
		         __func__, entry);

		/* enable "guest tlb mgmt instrs" */
		mtspr(SPR_EPCR, mfspr(SPR_EPCR) & ~EPCR_DGTMI);
	}
	gcpu->split_gtlb1_map = new_split_gtlb1_map;

	return new_split_gtlb1_map;
}

static int guest_tlb1_match(register_t mas1, register_t mas2, register_t va,
                            int pid, int ind, int flags, int global)
{
	if (!(mas1 & MAS1_VALID))
		return 0;

	if ((flags & INV_IPROT) || !(mas1 & MAS1_IPROT)) {
		register_t begin = mas2 & MAS2_EPN;
		register_t end = begin;

		end += (tsize_to_pages(MAS1_GETTSIZE(mas1)) - 1) * PAGE_SIZE;

		if (!global && (va < begin || va > end))
			return 0;

		if (pid >= 0 &&
		    (unsigned int)pid != (mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT)
			return 0;

		if (ind >= 0 &&
		    (unsigned int)ind != (mas1 & MAS1_IND) >> MAS1_IND_SHIFT)
			return 0;

		return 1;
	}

	return 0;
}

static void guest_inv_tlb1(register_t va, int pid, int ind,
                           int flags, int global)
{
	unsigned long bm_start = bench_start();
	gcpu_t *gcpu = get_gcpu();
	unsigned int i;

	fast_guest_inv_tlb1(va, pid, ind, flags, global);

	for (i = 0; i < TLB1_GSIZE; i++) {
		tlb_entry_t *tlbe = &gcpu->gtlb1[i];

		if (guest_tlb1_match(tlbe->mas1, tlbe->mas2, va, pid,
		                     ind, flags, global)) {
			int iprot = tlbe->mas1 & MAS1_IPROT;

			free_tlb1(i, 1);

			if (!iprot)
				update_dgtmi(tlbe->mas0, tlbe->mas1);
		}
	}

	bench_stop(bm_start, bm_tlb1_inv);
}

void guest_inv_tlb(register_t ivax, int pid, int ind, int flags)
{
	int global = ivax & TLBIVAX_INV_ALL;
	register_t va = ivax & TLBIVAX_VA;

	if (flags & INV_TLB0) {
		if (cpu->client.tlbcache_enable && (ind < 1)) {
			if (global) {
				if (pid < 0)
					guest_inv_tlb0_all();
				else
					guest_inv_tlb0_pid(pid);
			} else {
				guest_inv_tlb0_va(va, pid);
			}
		}

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
				int ways = cpu_caps.tlb0_assoc;
				int way;

				mtspr(SPR_MAS2, va);

				for (way = 0; way < ways; way++) {
					mtspr(SPR_MAS0, MAS0_TLBSEL0 | MAS0_ESEL(way));
					asm volatile("isync; tlbre" : : : "memory");
					mas1 = mfspr(SPR_MAS1);

					if (mas1 & MAS1_VALID) {
						mtspr(SPR_MAS6, (mas1 & MAS1_TID_MASK) |
						      (ind << MAS6_SIND_SHIFT));
						tlb_inv_addr(va);
					}
				}

				/* restore MAS8 as it might have been changed */
				mtspr(SPR_MAS8, get_gcpu()->lpid | MAS8_GTS);
			} else {
				assert((mfspr(SPR_MAS6) & MAS6_SPID_MASK) ==
				       ((unsigned int)pid << MAS6_SPID_SHIFT));
				assert((mfspr(SPR_MAS6) & MAS6_SIND) ==
				       ((unsigned int)ind << MAS6_SIND_SHIFT));
				tlb_inv_addr(va);
			}
		}
	}

	if (flags & INV_TLB1)
		guest_inv_tlb1(va, pid, ind, flags, global);
}

int guest_set_tlb0(register_t mas0, register_t mas1, register_t mas2,
                   register_t mas3flags, unsigned long rpn, register_t mas8,
                   register_t guest_mas3flags)
{
	if (cpu->client.tlbcache_enable)
		return guest_set_tlbcache(mas0, mas1, mas2, mas3flags,
		                          rpn, mas8, guest_mas3flags);

	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	mtspr(SPR_MAS3, (uint32_t)(rpn << PAGE_SHIFT) | mas3flags);
	mtspr(SPR_MAS8, mas8);
	asm volatile("isync; tlbwe" : : : "memory");
	return 0;
}

void guest_reset_tlb(void)
{
	mtspr(SPR_MMUCSR0, MMUCSR_L2TLB0_FI);
	isync();
	guest_inv_tlb(TLBIVAX_INV_ALL, -1, -1, INV_TLB0 | INV_TLB1 | INV_IPROT);
	isync();
}

/** Return the index of a conflicting guest TLB1 entry, or -1 if none.
 *
 * @param[in] entry TLB1 entry to ignore during search, or -1 if none.
 * @param[in] mas1 MAS1 value describing entry being added/searched for.
 * @param[in] epn Virtual page number describing the beginning of the
 * mapping to be added, or the page to be searched for.
 * @param[in] check_ind If set to 1 MAS1[IND] is also checked for matching,
 * if set to 0 MAS1[IND] is ignored.
 */
int guest_find_tlb1(unsigned int entry, unsigned long mas1, unsigned long epn)
{
	gcpu_t *gcpu = get_gcpu();
	int pid = MAS1_GETTID(mas1);
	unsigned int i;

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
		if ((mas1 & MAS1_IND) != (other->mas1 & MAS1_IND))
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

void fixup_tlb_sx_re(void)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long mas3 = mfspr(SPR_MAS3);
	unsigned long mas7 = mfspr(SPR_MAS7);

	unsigned long grpn = (mas7 << (32 - PAGE_SHIFT)) |
				(mas3 >> MAS3_RPN_SHIFT);

	if (!(mfspr(SPR_MAS1) & MAS1_VALID))
		return;

	if (cpu->client.tlbcache_enable)
		BUG();

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
		"sx_re: mas0 %lx mas1 %lx mas3 %lx mas7 %lx grpn %lx\n",
		mfspr(SPR_MAS0), mfspr(SPR_MAS1), mas3, mas7, grpn);

	/* Currently, we only use virtualization faults for bad mappings. */
	if (likely(!(mfspr(SPR_MAS8) & MAS8_VF))) {
		unsigned long attr;
		unsigned long rpn = vptbl_xlate(gcpu->guest->gphys_rev,
		                                grpn, &attr, PTE_PHYS_LEVELS, 0);

		assert(attr & PTE_VALID);

		mtspr(SPR_MAS3, (uint32_t)(rpn << PAGE_SHIFT) |
		                (mas3 & (MAS3_FLAGS | MAS3_USER)));
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	}
}

int guest_tlb_search_mas(uintptr_t va)
{
	gcpu_t *gcpu = get_gcpu();
	register_t mas1, mas6;

	mas6 = mfspr(SPR_MAS6);
	mas1 = (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT) |
		((mas6 & MAS6_SAS) << MAS1_TS_SHIFT) |
		(mas6 & MAS6_SPID_MASK) |
		(((mas6 & MAS6_SIND) >> MAS6_SIND_SHIFT) << MAS1_IND_SHIFT);

	int tlb1 = guest_find_tlb1(-1, mas1, va >> PAGE_SHIFT);
	if (tlb1 >= 0) {
		mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(tlb1));
		mtspr(SPR_MAS1, gcpu->gtlb1[tlb1].mas1);
		mtspr(SPR_MAS2, gcpu->gtlb1[tlb1].mas2);
		mtspr(SPR_MAS3, gcpu->gtlb1[tlb1].mas3);
		mtspr(SPR_MAS7, gcpu->gtlb1[tlb1].mas7);

		return 0;
	}
	if (cpu->client.tlbcache_enable && !(mas6 & MAS6_SIND)) {
		tlbctag_t tag = make_tag(va, (mas6 & MAS6_SPID_MASK) >> MAS6_SPID_SHIFT,
					mas6 & MAS6_SAS);
		tlbcset_t *set;
		unsigned int way;

		if (find_gtlb_entry(va, tag, &set, &way)) {
			gtlb0_to_mas(set - cpu->client.tlbcache, way, gcpu);
			return 0;
		}
	}

	asm volatile("isync; tlbsx 0, %0" : : "r" (va) : "memory");
	fixup_tlb_sx_re();

	return 0;
}

/** Guest TLB Search
 *
 * @param[in] va effective address for which tlb search needs to be done
 * @param[in] as address space to be used fot tlb search.
 * @param[in] pid processor id to be used for tlb search
 * @param[out] mas used to return the MAS values if tlb entry exists.
 * @return 0 on success, ERR_NOTFOUND if tlb entry does not exist. MAS
 * registers are preserved.
 *
 */
int guest_tlb_search(uintptr_t va, int as, int pid, int ind, tlb_entry_t *mas)
{
	register_t saved;

	saved = disable_critint_save();
	save_mas(get_gcpu());

	mtspr(SPR_MAS6, ((pid << MAS6_SPID_SHIFT) | as | (ind << MAS6_SIND_SHIFT)));
	guest_tlb_search_mas(va);
	if ((mfspr(SPR_MAS1) & MAS1_VALID)) {
		mas->mas0 = mfspr(SPR_MAS0);
		mas->mas1 = mfspr(SPR_MAS1);
		mas->mas2 = mfspr(SPR_MAS2);
		mas->mas3 = mfspr(SPR_MAS3);
		mas->mas7 = mfspr(SPR_MAS7);

		restore_mas(get_gcpu());
		restore_critint(saved);

		return 0;
	}

	restore_mas(get_gcpu());
	restore_critint(saved);

	return ERR_NOTFOUND;
}


unsigned long CCSRBAR_VA;

static uint32_t map_lock;
static DECLARE_LIST(maps);
static int next_pinned_tlbe = PERM_TLB_START;

typedef struct map_entry {
	list_t map_node;
	unsigned long start_page, end_page;
	unsigned long phys_offset; /* phys start page - virt start page */
	uint32_t mas2flags:8;
	uint32_t mas3flags:6;
	uint32_t pinned_tlbe:6; /* zero if not pinned */
	uint32_t tsize:5;
} map_entry_t;

/* map_lock must be held */
static void insert_map_entry(map_entry_t *me, uintptr_t gaddr)
{
	unsigned long start_page = me->start_page;
	unsigned long start_phys;
	register_t saved;
	shared_cpu_t *shared_cpu = get_shared_cpu();

	int tlbe = me->pinned_tlbe;

	if (likely(!tlbe)) {
		register_t saved;

		start_phys = (gaddr >> PAGE_SHIFT) + me->phys_offset;

		saved = tlb_lock();
		tlbe = shared_cpu->next_dyn_tlbe++;

		if (shared_cpu->next_dyn_tlbe > DYN_TLB_END)
			shared_cpu->next_dyn_tlbe = DYN_TLB_START;
		tlb_unlock(saved);

		unsigned long page_mask = ~(tsize_to_pages(me->tsize) - 1);
		start_phys &= page_mask;
		start_page &= page_mask;
	} else {
		start_phys = start_page + me->phys_offset;
	}

	tlb1_set_entry_safe(tlbe, start_page << PAGE_SHIFT,
	                    ((phys_addr_t)start_phys) << PAGE_SHIFT,
	                    me->tsize, MAS1_IPROT, me->mas2flags, me->mas3flags,
	                    0, TLB_MAS8_HV);
}

/** Try to handle a TLB miss on a hypervisor mapping
 *
 * @param[in] regs trap frame
 * @param[in] vaddr faulting address
 * @return non-zero if successfully handled
 */
int handle_hv_tlb_miss(trapframe_t *regs, uintptr_t vaddr)
{
	unsigned long saved = spin_lock_intsave(&map_lock);
	unsigned long page = vaddr >> PAGE_SHIFT;
	int ret = 0;

	list_for_each(&maps, i) {
		map_entry_t *me = to_container(i, map_entry_t, map_node);

		if (page < me->start_page)
			continue;
		if (page > me->end_page)
			continue;

		insert_map_entry(me, vaddr);
		ret = 1;
		break;
	}

	spin_unlock_intsave(&map_lock, saved);
	return ret;
}

void secondary_map_mem(void)
{
	unsigned long saved = spin_lock_intsave(&map_lock);
	register_t tlb_saved;

	tlb_saved = tlb_lock();
	get_shared_cpu()->next_dyn_tlbe = DYN_TLB_START;
	tlb_unlock(tlb_saved);

	/* Insert pre-existing pinned mappings */
	list_for_each(&maps, i) {
		map_entry_t *me = to_container(i, map_entry_t, map_node);

		if (me->pinned_tlbe)
			insert_map_entry(me, me->start_page << PAGE_SHIFT);
	}

	spin_unlock_intsave(&map_lock, saved);
}

/** Create a permanent hypervisor mapping
 *
 * @param[in] paddr base of physical region to map
 * @param[in] len length of region to map
 * @param[in] mas2flags WIMGE bits
 * @param[in] mas3flags permission bits
 *
 * @return the virtual address of the mapping, or NULL if out of resources.
 */
void *map(phys_addr_t paddr, size_t len, int mas2flags, int mas3flags)
{
	map_entry_t *me;
	register_t saved;
	uintptr_t ret = 0;

	unsigned long start_page = paddr >> PAGE_SHIFT;
	unsigned long end_page = (paddr + len - 1) >> PAGE_SHIFT;

	saved = spin_lock_intsave(&map_lock);

	/* Look for an existing region of which this is a subset */
	list_for_each(&maps, i) {
		me = to_container(i, map_entry_t, map_node);

		if (start_page < me->start_page + me->phys_offset)
			continue;
		if (end_page > me->end_page + me->phys_offset)
			continue;

		ret = (start_page - me->phys_offset) << PAGE_SHIFT;
		ret += paddr & (PAGE_SIZE - 1);

		goto out;
	}

	unsigned long pages = end_page - start_page + 1;

	me = malloc(sizeof(map_entry_t));
	if (!me)
		goto out;

	ret = (uintptr_t)valloc(pages << PAGE_SHIFT, pages << PAGE_SHIFT);
	if (!ret)
		goto out_me;

	ret += paddr & (PAGE_SIZE - 1);

	me->start_page = ret >> PAGE_SHIFT;
	me->end_page = me->start_page + pages - 1;
	me->phys_offset = start_page - me->start_page;
	me->mas2flags = mas2flags;
	me->mas3flags = mas3flags;

	me->tsize = min(max_page_size(me->start_page,
	                              me->end_page - me->start_page + 1),
	                natural_alignment(start_page));

	me->pinned_tlbe = 0;

	list_add(&maps, &me->map_node);

out:
	spin_unlock_intsave(&map_lock, saved);
	return (void *)ret;

out_me:
	free(me);
	spin_unlock_intsave(&map_lock, saved);
	return (void *)ret;
}

static map_entry_t pma_maps[PERM_TLB_END - PERM_TLB_START + 1];

/** Create a permanent hypervisor mapping for a PMA
 *
 * @param[in] paddr base of physical region to map, must be naturally aligned
 * @param[in] len length of region to map, must be power of 2
 * @param[in] text non-zero if adding a text mapping (PHYSBASE),
 *   zero if adding a data mapping (BIGPHYSBASE).
 * @return zero on success
 */
int map_hv_pma(phys_addr_t paddr, size_t len, int text)
{
	unsigned long npages = len >> PAGE_SHIFT;
	int tsize = max_valid_tsize(pages_to_tsize_msb(npages));
	unsigned long pages_per_entry = tsize_to_pages(tsize);
	register_t saved;
	map_entry_t *me;
	int entries = pages_per_entry < npages ? 2 : 1;
	int ret = 0;

	saved = spin_lock_intsave(&map_lock);

	for (int i = 0; i < entries; i++) {
		unsigned long page = paddr >> PAGE_SHIFT;
		int tlbe;

		if (next_pinned_tlbe > PERM_TLB_END) {
			ret = ERR_BUSY;
			goto out;
		}

		tlbe = next_pinned_tlbe++;
		me = &pma_maps[tlbe - PERM_TLB_START];

		if (text)
			me->start_page = (paddr - text_phys + PHYSBASE) >> PAGE_SHIFT;
		else
			me->start_page = (paddr - bigmap_phys + BIGPHYSBASE) >> PAGE_SHIFT;

		me->end_page = me->start_page + pages_per_entry - 1;
		me->phys_offset = page - me->start_page;
		me->mas2flags = TLB_MAS2_MEM;
		me->mas3flags = TLB_MAS3_KERN;
		me->tsize = tsize;
		me->pinned_tlbe = tlbe;

		list_add(&maps, &me->map_node);
		insert_map_entry(me, ret);

		paddr += pages_per_entry << PAGE_SHIFT;
	}

out:
	spin_unlock_intsave(&map_lock, saved);
	return ret;
}

/** Temporarily map guest physical memory into a hypervisor virtual address
 *
 * @param[in]  tlbentry TLB1 entry index to use
 * @param[in]  tbl Guest page table to map from
 * @param[in]  addr Guest physical address to map
 * @param[in]  vpage Virtual base of window to hold mapping
 * @param[out] len Length of the actual mapping
 * @param[in]  maxtsize tsize of the virtual window
 * @param[in]  mas2flags flags for the mapping (mem or i/o)
 * @param[in]  write if non-zero, fail if write access is not allowed
 * @return virtual address that corresponds to addr
 */
void *map_gphys(int tlbentry, pte_t *tbl, phys_addr_t addr,
                void *vpage, size_t *len, int maxtsize, register_t mas2flags,
                int write)
{
	size_t offset, bytesize;
	unsigned long attr;
	unsigned long rpn;
	phys_addr_t physaddr;
	int tsize;

	rpn = vptbl_xlate(tbl, addr >> PAGE_SHIFT, &attr, PTE_PHYS_LEVELS, 0);

	if (!(attr & PTE_VALID) || (attr & PTE_VF))
		return NULL;
	if (write && !(attr & PTE_UW))
		return NULL;

	tsize = attr >> PTE_SIZE_SHIFT;
	if (tsize > maxtsize)
		tsize = maxtsize;

	bytesize = tsize_to_pages(tsize) << PAGE_SHIFT;
	offset = addr & (bytesize - 1);
	physaddr = (phys_addr_t)rpn << PAGE_SHIFT;

	if (len)
		*len = bytesize - offset;

	tlb1_set_entry_safe(tlbentry, (unsigned long)vpage,
	                    physaddr & ~((phys_addr_t)bytesize - 1),
	                    tsize, MAS1_IPROT, TLB_MAS2_MEM, TLB_MAS3_KERN,
	                    0, TLB_MAS8_HV);

	return vpage + offset;
}

/** Copy from a hypervisor virtual address to a guest physical address
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Guest physical address to copy to
 * @param[in] src Hypervisor virtual address to copy from
 * @param[in] len Bytes to copy
 * @param[in] cache_sync flag indicating i-cache should be synced
 * @return number of bytes successfully copied
 */
size_t copy_to_gphys(pte_t *tbl, phys_addr_t dest, void *src, size_t len,
                     int cache_sync)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vdest;

		vdest = map_gphys(TEMPTLB1, tbl, dest, TEMP_MAPPING1,
		                  &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
		if (!vdest)
			break;

		if (chunk > len)
			chunk = len;

		memcpy(vdest, src, chunk);

		if (cache_sync)
			icache_range_sync(vdest, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}

/** Fill a block of guest physical memory with zeroes
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Guest physical address to copy to
 * @param[in] len Bytes to zero
 * @param[in] cache_sync flag indicating i-cache should be synced
 * @return number of bytes successfully zeroed
 */
size_t zero_to_gphys(pte_t *tbl, phys_addr_t dest, size_t len, int cache_sync)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vdest;

		vdest = map_gphys(TEMPTLB1, tbl, dest, TEMP_MAPPING1,
		                  &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
		if (!vdest)
			break;

		if (chunk > len)
			chunk = len;

		memset(vdest, 0, chunk);

		if (cache_sync)
			icache_range_sync(vdest, chunk);

		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}


/** Copy from a guest physical address to a hypervisor virtual address
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Hypervisor virtual address to copy to
 * @param[in] src Guest physical address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_from_gphys(pte_t *tbl, void *dest, phys_addr_t src, size_t len)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vsrc;

		vsrc = map_gphys(TEMPTLB1, tbl, src, TEMP_MAPPING1,
		                 &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 0);
		if (!vsrc)
			break;

		if (chunk > len)
			chunk = len;

		memcpy(dest, vsrc, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}

/** Copy from a guest physical address to another guest physical address
 *
 * @param[in] dtbl Guest physical page table of the destination
 * @param[in] dest Guest physical address to copy to
 * @param[in] stbl Guest physical page table of the source
 * @param[in] src Guest physical address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_between_gphys(pte_t *dtbl, phys_addr_t dest,
                          pte_t *stbl, phys_addr_t src, size_t len)
{
	size_t schunk = 0, dchunk = 0, chunk, ret = 0;

	/* Initializiations not needed, but GCC is stupid. */
	void *vdest = NULL, *vsrc = NULL;

	while (len > 0) {
		if (!schunk) {
			vsrc = map_gphys(TEMPTLB1, stbl, src, TEMP_MAPPING1,
			                 &schunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 0);
			if (!vsrc)
				break;
		}

		if (!dchunk) {
			vdest = map_gphys(TEMPTLB2, dtbl, dest, TEMP_MAPPING2,
			                  &dchunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
			if (!vdest)
				break;
		}

		chunk = min(schunk, dchunk);
		if (chunk > len)
			chunk = len;

		memcpy(vdest, vsrc, chunk);
		
		/* NOTE: the caller may not be in the coherence domain
		 * for one or both of the partitions being accessed.
		 *
		 * This should not be a problem on the initial access, as
		 * the data should not be in any cache that is outside
		 * the coherence domain -- yet.  However, once we do the
		 * access, the data will be in a cache that it should not
		 * be in, so we flush it back out.  We'd need to do this
		 * anyway on the destination to ensure coherency with
		 * icache.
		 *
		 * For this to work, nothing else must access any of this
		 * data between the memcpy and the flush, unless the
		 * caller is part of that data's coherence domain.
		 *
		 * For the typical usage of copying data from a partition
		 * manager into a stopped partition, the source data will
		 * be in the caller's coherence domain (so it doesn't
		 * matter if other cores in the manager partition read
		 * the data simultaneously).  However, the destination
		 * data is likely is a different partition.  That data
		 * must not be touched (even loaded) while this is going
		 * on.  One way of ensuring this is to only load into
		 * PMAs which are either private to the destination
		 * partition, or are shared with the manager partition.
		 */
		dcache_range_flush(vsrc, chunk);
		icache_range_sync(vdest, chunk);

		vsrc += chunk;
		vdest += chunk;
		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
		dchunk -= chunk;
		schunk -= chunk;
	}

	return ret;
}

/* length includes null terminator, unlike regular strnlen */
static ssize_t guest_strnlen(pte_t *tbl, phys_addr_t gaddr, ssize_t maxlen)
{
	size_t len = 0;

	while (1) {
		size_t chunk;
		char *vaddr;
		char *end;

		vaddr = map_gphys(TEMPTLB1, tbl, gaddr, TEMP_MAPPING1,
		                  &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 0);
		if (!vaddr)
			return -EV_EFAULT;

		if (maxlen > 0) {
			if (chunk > (size_t)maxlen)
				chunk = maxlen;

			maxlen -= chunk;
		}

		end = memchr(vaddr, 0, chunk);
		if (end) {
			len += end - vaddr + 1;
			break;
		}

		if (maxlen == 0)
			return -EV_EINVAL;

		gaddr += chunk;
		len += chunk;
	}

	return len;
}

/** Copy a null-terminated string from a guest physical address.
 *
 * A buffer (up to an optional maximum size) is dynamically allocated.
 *
 * @param[in] tbl Guest physical page table
 * @param[in] src Guest physical address to copy from
 * @param[in] maxlen Largest buffer to allocate, or -1 for unlimited.
 * @param[out] free()able string
 * @return size on success, -EFAULT or -ENOMEM on error.
 */
ssize_t copy_string_from_gphys(pte_t *tbl, phys_addr_t src,
                               ssize_t maxlen, char **buf)
{
	ssize_t len;
	size_t ret;
	
	len = guest_strnlen(tbl, src, maxlen);
	if (len < 0)
		return len;

	*buf = malloc(len);
	if (!(*buf))
		return -EV_ENOMEM;

	ret = copy_from_gphys(tbl, *buf, src, len);
	if (ret < (size_t)len) {
		free(*buf);
		return -EV_EFAULT;
	}

	return len;
}

/** Temporarily map physical memory into a hypervisor virtual address
 *
 * @param[in] tlbentry TLB1 entry index to use
 * @param[in] paddr Guest physical address to map
 * @param[in] vpage Virtual base of window to hold mapping
 * @param[inout] len Length of the actual mapping
 * @param[in] maxtsize tsize of virtual address window
 * @param[in] mas2flags WIMGE bits, typically TLB_MAS2_IO or TLB_MAS2_MEM
 * @param[in] mas3flags permissions, typically TLB_MAS3_KERN or TLB_MAS3_KDATA
 * @return the virtual address that corresponds to paddr.
 */
void *map_phys(int tlbentry, phys_addr_t paddr, void *vpage,
               size_t *len, int maxtsize, register_t mas2flags,
               register_t mas3flags)
{
	size_t offset, bytesize;
	int tsize = pages_to_tsize_msb((*len + PAGE_SIZE - 1) >> PAGE_SHIFT);

	tsize = min(max_page_tsize((uintptr_t)vpage >> PAGE_SHIFT, tsize),
	            natural_alignment(paddr >> PAGE_SHIFT));
	tsize = min(tsize, maxtsize);

	bytesize = tsize_to_pages(tsize) << PAGE_SHIFT;
	offset = paddr & (bytesize - 1);

	tlb1_set_entry_safe(tlbentry, (unsigned long)vpage,
	                    paddr & ~((phys_addr_t)bytesize - 1),
	                    tsize, MAS1_IPROT, mas2flags, mas3flags, 0, TLB_MAS8_HV);

	*len = min(bytesize - offset, *len);
	return vpage + offset;
}

/** Copy from a true physical address to a hypervisor virtual address
 *
 * @param[in] dest Hypervisor virtual address to copy to
 * @param[in] src Physical address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_from_phys(void *dest, phys_addr_t src, size_t len)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk = len >= PAGE_SIZE ? 1UL << ilog2(len) : len;
		void *vsrc;

		vsrc = map_phys(TEMPTLB1, src, TEMP_MAPPING1,
		                &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM,
		                TLB_MAS3_KERN);
		if (!vsrc)
			break;

		assert (chunk <= len);
		memcpy(dest, vsrc, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}

/** Zero a true physical range
 *
 * @param[in] dest Physical address to zero
 * @param[in] len Bytes to zero
 * @return number of bytes successfully zeroed
 */
phys_addr_t zero_to_phys(phys_addr_t dest, phys_addr_t len)
{
	phys_addr_t ret = 0;

	while (len > 0) {
		size_t chunk = 16 * 1024 * 1024;
		void *vdest;

		if (chunk > len)
			chunk = len;
		if (chunk > PAGE_SIZE)
			chunk = 1UL << ilog2(chunk);

		vdest = map_phys(TEMPTLB1, dest, TEMP_MAPPING1,
		                 &chunk, TLB_TSIZE_16M, TLB_MAS2_MEM,
		                 TLB_MAS3_KERN);
		if (!vdest)
			break;

		assert (chunk <= len);
		memset(vdest, 0, chunk);

		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	tlb1_clear_entry(TEMPTLB1);
	return ret;
}

/** Copy from a true physical address to a guest physical address
 *
 * @param[in] dtbl Guest physical page table of the destination
 * @param[in] dest Guest physical address to copy to
 * @param[in] src Guest physical address to copy from
 * @param[in] len Bytes to copy
 * @param[in] cache_sync flag indicating i-cache should be synced
 * @return number of bytes successfully copied
 */
size_t copy_phys_to_gphys(pte_t *dtbl, phys_addr_t dest,
                          phys_addr_t src, size_t len, int cache_sync)
{
	size_t schunk = 0, dchunk = 0, chunk, ret = 0;

	/* Initializiations not needed, but GCC is stupid. */
	void *vdest = NULL, *vsrc = NULL;

	while (len > 0) {
		if (!schunk) {
			schunk = len >= PAGE_SIZE ? 1UL << ilog2(len) : len;
			vsrc = map_phys(TEMPTLB1, src, TEMP_MAPPING1,
			                &schunk, TLB_TSIZE_16M, TLB_MAS2_MEM,
			                TLB_MAS3_KERN);
			if (!vsrc) {
				printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_ERROR,
				         "%s: cannot map src %llx, %zu bytes\n",
				         __func__, src, schunk);
				break;
			}
		}

		if (!dchunk) {
			vdest = map_gphys(TEMPTLB2, dtbl, dest, TEMP_MAPPING2,
			                  &dchunk, TLB_TSIZE_16M, TLB_MAS2_MEM, 1);
			if (!vdest) {
				printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_ERROR,
				         "%s: cannot map dest %llx, %zu bytes\n",
				         __func__, dest, dchunk);
				break;
			}
		}

		chunk = min(schunk, dchunk);
		if (chunk > len)
			chunk = len;

		memcpy(vdest, vsrc, chunk);

		if (cache_sync)
			icache_range_sync(vdest, chunk);

		vsrc += chunk;
		vdest += chunk;
		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
		dchunk -= chunk;
		schunk -= chunk;
	}

	return ret;
}

/** Dump the guest tlb entries
 *
 * @param[in] gmas pointer to guest tlb_entry
 * @param[inout] flags pointer to flags  (self-clearing,
 *    caller should not use it after initializing it to TLB_READ_FIRST )
 * @return success or indication that there are no more tlbe's to be read
 *
 * NOTE: This is a debug stub API.  If it changes the API spec must
 * be changed as well.
 *
 */
int guest_tlb_read(tlb_entry_t *gmas, uint32_t *flags)
{
	gcpu_t *gcpu = get_gcpu();
	return guest_tlb_read_vcpu(gmas, flags, gcpu);
}

/** Dump the guest tlb entries
 *
 * @param[in] gmas pointer to guest tlb_entry
 * @param[inout] flags pointer to flags  (self-clearing,
 *    caller should not use it after initializing it to TLB_READ_FIRST )
 * @param[in] gcpu pointer to target virtual cpu's gcpu struct
 * @return success or indication that there are no more tlbe's to be read
 *
 * NOTE: This is a debug stub API.  If it changes the API spec must
 * be changed as well.
 *
 */
int guest_tlb_read_vcpu(tlb_entry_t *gmas, uint32_t *flags, gcpu_t *gcpu)
{
	uint32_t tlb_index, way, tlb0_nentries, tlb0_nways;
	unsigned int tlb;

	if (cpu->client.tlbcache_enable) {
		tlb0_nways = TLBC_WAYS;
		tlb0_nentries = 1 << (cpu->client.tlbcache_bits);
	} else {
		tlb0_nways = cpu_caps.tlb0_assoc;
		tlb0_nentries = cpu_caps.tlb0_nentries / tlb0_nways;
	}

	tlb = MAS0_GET_TLBSEL(gmas->mas0);

	switch (tlb) {
	case 0:
	{
		if (*flags & TLB_READ_FIRST) {
			tlb_index = way = 0;
			*flags &= ~TLB_READ_FIRST;
		} else {
			tlb_index = ((gmas->mas2 >> PAGE_SHIFT) &
				(tlb0_nentries - 1));
			way = (MAS0_GET_TLB0ESEL(gmas->mas0) + 1) &
				(tlb0_nways - 1);
			if (!way)
				++tlb_index;
		}

		if (tlb_index >= tlb0_nentries)
			return ERR_NOTFOUND;

		/*
		 * TLB0 is pass-thorugh with grpn<->true-rpn fixups applied
		 * in non TLB cache case
		 */
		disable_int();
		save_mas(gcpu);

		if (cpu->client.tlbcache_enable) {
			gtlb0_to_mas(tlb_index, way, gcpu);
		} else {
			mtspr(SPR_MAS0, MAS0_ESEL(way));
			mtspr(SPR_MAS2, tlb_index << PAGE_SHIFT);
			asm volatile("isync; tlbre" : : : "memory");
			fixup_tlb_sx_re();
		}

		if (!(mfspr(SPR_MAS1) & MAS1_VALID)) {
			gmas->mas0 = MAS0_ESEL(way);
			gmas->mas1 = mfspr(SPR_MAS1);
			gmas->mas2 = tlb_index << PAGE_SHIFT;
		} else {
			gmas->mas0 = mfspr(SPR_MAS0);
			gmas->mas1 = mfspr(SPR_MAS1);
			gmas->mas2 = mfspr(SPR_MAS2);
			gmas->mas3 = mfspr(SPR_MAS3);
			gmas->mas7 = mfspr(SPR_MAS7);
			gmas->mas8 = mfspr(SPR_MAS8);
		}
		restore_mas(gcpu);
		enable_int();
		break;
	}
	case 1:
	{
		if (*flags & TLB_READ_FIRST) {
			tlb_index = 0;
			*flags &= ~TLB_READ_FIRST;
		} else {
			tlb_index = MAS0_GET_TLB1ESEL(gmas->mas0);
			++tlb_index;
		}
		if (tlb_index >= TLB1_GSIZE)
			return ERR_NOTFOUND;
		memcpy(gmas, &gcpu->gtlb1[tlb_index], sizeof(tlb_entry_t));
		break;
	}
	default:
		return ERR_NOTFOUND;
	}

	return 0;
}

void inv_lrat(gcpu_t *gcpu)
{
	unsigned long mas0, mas1 = 0;
	register_t saved;

	save_mas(gcpu);

	/* lrat entries can only be invalidated by writing the LRAT entry
	 * with MAS1[V] = 0
	 */

	saved = tlb_lock();
	for (int i = 0; i < cpu_caps.lrat_nentries; i++) {
		/* read the entry */
		mas0 = MAS0_LRATSEL | MAS0_ESEL(i);
		mtspr(SPR_MAS0, mas0);
		asm volatile("isync; tlbre" : : : "memory");

		/* invalidate the entry */
		mas1 &= ~MAS1_VALID;
		mtspr(SPR_MAS1, mas1);
		asm volatile("isync; tlbwe" : : : "memory");

	}

	get_shared_cpu()->lrat_next_entry = 0;
	tlb_unlock(saved);

	restore_mas(gcpu);
}

void lrat_miss(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	register_t saved;
	unsigned long grpn = 0;
	unsigned long attr;
	unsigned long rpn;
	unsigned long mas0, mas1, mas2, mas3, mas7, mas8, mas3_flags;
	unsigned long tsize, size_pages;
	uint32_t esr = mfspr(SPR_ESR);
	int pt = mfspr(SPR_ESR) & ESR_PT;

	disable_int();
	save_mas(gcpu);

	set_stat(bm_stat_lrat_miss, regs);

	if (pt)
		grpn = (mfspr(SPR_LPER) & LPER_ALPN) >> LPER_ALPN_SHIFT;
	else
		grpn = (gcpu->mas7 << (32 - PAGE_SHIFT)) |
		       (gcpu->mas3 >> MAS3_RPN_SHIFT);

	rpn = vptbl_xlate(guest->gphys, grpn, &attr, PTE_PHYS_LEVELS, 0);

	if (unlikely(!(attr & PTE_VALID))) {
		printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
		        "Trying to map a non existing page srr0 0x%lx, srr1 0x%lx, grpn 0x%lx\n",
		        regs->srr0, regs->srr1, grpn);

		/* for page table walk originated bad mappings, reflect a machine
		 * check to the guest.
		 */
		if (pt) {
			uint32_t mcsr = MCSR_MAV | MCSR_MEA;
			register_t vaddr;

			vaddr = mfspr(SPR_DEAR);
			if (esr & ESR_ST)
				mcsr |= MCSR_ST;
			else
				mcsr |= MCSR_LD;

			reflect_mcheck(regs, mcsr, vaddr);

			restore_mas(gcpu);
			enable_int();
			return;
		}

		/* for tlbwe originated bad mappings, the Hypervisor writes the TLB
		 * entry on behalf of the guest and sets the virtualization fault bit
		 */

		mas0 = gcpu->mas0;
		mas1 = gcpu->mas1;
		mas2 = gcpu->mas2;
		mas3 = gcpu->mas3;
		mas7 = gcpu->mas7;
		mas8 = gcpu->lpid | MAS8_GTS | MAS8_VF;
		mas3 &= (attr & PTE_MAS3_MASK) | MAS3_USER;

		/* VF does not prevent instruction execution */
		mas3 &= !(MAS3_SX | MAS3_UX);

		rpn = grpn;

		mtspr(SPR_MAS0, mas0);
		mtspr(SPR_MAS1, mas1);
		mtspr(SPR_MAS2, mas2);
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
		mtspr(SPR_MAS3, (uint32_t)(rpn << PAGE_SHIFT) | mas3);
		mtspr(SPR_MAS8, mas8);
		asm volatile("isync; tlbwe" : : : "memory");

		mtspr(SPR_MAS8, gcpu->lpid | MAS8_GTS);
		restore_mas(gcpu);
		enable_int();

		/* the Hypervisor has written the entry on behalf of the guest,
		   so the tlbwe instruction should not be again executed
		 */
		regs->srr0 += 4;

		return;
	}

	tsize = attr >> PTE_SIZE_SHIFT;
	size_pages = tsize_to_pages(tsize);
	grpn &= ~(size_pages - 1);
	rpn &= ~(size_pages - 1);

	saved = tlb_lock();

	shared_cpu_t *shared_cpu = get_shared_cpu();

	mas0 = MAS0_LRATSEL | MAS0_ESEL(shared_cpu->lrat_next_entry);
	mas1 = MAS1_VALID;
	mas1 |= tsize << MAS1_TSIZE_SHIFT;
	mas2 = grpn << PAGE_SHIFT;
	mas3 = (rpn << PAGE_SHIFT) & MAS3_RPN;
	mas7 = rpn >> (32 - PAGE_SHIFT);
	mas8 = gcpu->lpid;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE, "LPN: %lx, RPN: %lx,"
	         "MAS0: %lx, MAS1: %lx, MAS2: %lx, MAS3: %lx, MAS7: %lx, MAS8: %lx\n",
	         grpn, rpn, mas0, mas1, mas2, mas3, mas7, mas8);

	mtspr(SPR_MAS0, mas0);
	mtspr(SPR_MAS1, mas1);
	mtspr(SPR_MAS2, mas2);
	mtspr(SPR_MAS7, mas7);
	mtspr(SPR_MAS3, mas3);
	mtspr(SPR_MAS8, mas8);
	asm volatile("isync; tlbwe" : : : "memory");

	shared_cpu->lrat_next_entry++;
	if (shared_cpu->lrat_next_entry == cpu_caps.lrat_nentries)
		shared_cpu->lrat_next_entry = 0;

	mtspr(SPR_MAS8, gcpu->lpid | MAS8_GTS);
	restore_mas(gcpu);

	tlb_unlock(saved);

	enable_int();

}

#ifdef CONFIG_FAST_TLB1

void fast_guest_tlb1_init(void)
{
	for (int i = 0; i <= GUEST_TLB_END; i++)
		get_gcpu()->fast_tlb1_to_gtlb1[i] = -1;
}

int fast_guest_set_tlb1(register_t mas0, register_t mas1)
{
	gcpu_t *gcpu = get_gcpu();
	unsigned long pages, grpn = 0, attr = 0, rpn = 0;
	int tsize = 0, entry, real_entry = -1;
	register_t mas3, saved_mas0, saved_mas3, saved_mas7;

	saved_mas0 = mas0;
	saved_mas3 = mas3 = mfspr(SPR_MAS3);
	saved_mas7 = mfspr(SPR_MAS7);

	entry = MAS0_GET_TLB1ESEL(mas0) & (TLB1_GSIZE - 1);

	if (mas1 & MAS1_VALID) {
		tsize = MAS1_GETTSIZE(mas1);
		pages = tsize_to_pages(tsize);
		if (mas1 & MAS1_IND)
			tsize -= MAS3_GETSPSIZE(mas3) + 7;
		grpn = (mfspr(SPR_MAS7) << (32 - PAGE_SHIFT)) | (mas3 >> MAS3_RPN_SHIFT);

		rpn = vptbl_xlate(gcpu->guest->gphys, grpn, &attr, PTE_PHYS_LEVELS, 0);
		if (!(attr & PTE_VALID) || (attr >> PTE_SIZE_SHIFT) < tsize) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
			         "%s: xlate error entry = %d grpn: %lx "
			         "attr: %lx tsize: %d xtsize: %ld\n",
			          __func__, entry, grpn, attr, tsize,
			         (attr >> PTE_SIZE_SHIFT));
			return 1;
		}

		free_tlb1(entry, 0);
		real_entry = alloc_tlb1(entry, 0);
		if (real_entry < 0) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_ALWAYS,
				 "%s: Out of TLB1 entries!\n", __func__);
			return 1;
		}
		gcpu->fast_tlb1_to_gtlb1[real_entry] = entry;

		mas0 &= ~MAS0_ESEL_TLB1MASK;
		mas0 |= MAS0_ESEL(real_entry);

		mas3 &= attr & PTE_MAS3_MASK;
		mas3 = ((rpn << PAGE_SHIFT) & MAS3_RPN) | (attr & PTE_MAS3_MASK);


		register_t saved_mas1, saved_mas2;

		saved_mas1 = mfspr(SPR_MAS1);
		saved_mas2 = mfspr(SPR_MAS2);

		apply_a008139_workaround(real_entry);

		mtspr(SPR_MAS1, saved_mas1);
		mtspr(SPR_MAS2, saved_mas2);

		mtspr(SPR_MAS0, mas0);
		mtspr(SPR_MAS3, mas3);
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
		mtspr(SPR_MAS8, gcpu->lpid | ((attr << PTE_MAS8_SHIFT) & PTE_MAS8_MASK));

		asm volatile("isync; tlbwe; isync; msync" : : : "memory");

		mtspr(SPR_MAS0, saved_mas0);
		mtspr(SPR_MAS3, saved_mas3);
		mtspr(SPR_MAS7, saved_mas7);
	} else {
		int i = 0, idx = 0;

		save_mas(gcpu);
		real_entry = nonsplit_gtlb1_to_tlb1(entry);
		if (real_entry >= 0 && gcpu->fast_tlb1_to_gtlb1[real_entry] > 0) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
			         "%s@0x%lx clearing gentry = %d real_entry = %d\n",
			         __func__, mfspr(SPR_GSRR0), entry, real_entry);

			gcpu->fast_tlb1_to_gtlb1[real_entry] = -1;
		} else {
			update_dgtmi(mas0, mas1);
		}

		free_tlb1(entry, 1);
		restore_mas(gcpu);
	}

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "%s@0x%lx: V=%d eaddr = %lx gaddr = %lx raddr = %lx entry = %u "
	         "real_entry = %u attr = %lx tsize = %d (xtsize = %lu)\n",
	         __func__, mfspr(SPR_GSRR0), (mas1 & MAS1_VALID) ? 1 : 0,
	         mfspr(SPR_MAS2) & MAS2_EPN, grpn << PAGE_SHIFT, rpn << PAGE_SHIFT,
	         entry, real_entry, attr, tsize, (attr >> PTE_SIZE_SHIFT));

	return 0;
}

int fast_guest_tlbsx(unsigned long va)
{
	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "%s: va = %lx\n", __func__, va);

	asm volatile("isync; tlbsx 0, %0" : : "r" (va) : "memory");

	if (mfspr(SPR_MAS1) & MAS1_VALID) {
		register_t mas0 = mfspr(SPR_MAS0);
		unsigned int entry = MAS0_GET_TLB1ESEL(mas0) & (TLB1_GSIZE - 1);

		if (get_gcpu()->fast_tlb1_to_gtlb1[entry] > 0) {
			mas0 &= ~MAS0_ESEL_TLB1MASK;
			mas0 |=  get_gcpu()->fast_tlb1_to_gtlb1[entry];
			mtspr(SPR_MAS0, mas0);
			fixup_tlb_sx_re();

			return 0;
		}
	}

	return 1;
}

int fast_guest_tlbre(void)
{
	register_t mas0 = mfspr(SPR_MAS0);
	unsigned int entry = MAS0_GET_TLB1ESEL(mas0) & (TLB1_GSIZE - 1);
	int i = 0, real_entry = 0;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "%s: entry = %u\n", __func__, entry);

	real_entry = nonsplit_gtlb1_to_tlb1(entry);
	if (real_entry < 0)
		return 1;

	if (get_gcpu()->fast_tlb1_to_gtlb1[real_entry] >= 0) {
		mas0 &= ~MAS0_ESEL_TLB1MASK;
		mas0 |= MAS0_ESEL(real_entry);
		mtspr(SPR_MAS0, mas0);

		asm volatile("tlbre" : : : "memory");
		fixup_tlb_sx_re();

		mas0 &= ~MAS0_ESEL_TLB1MASK;
		mas0 |=  MAS0_ESEL(entry);
		mtspr(SPR_MAS0, mas0);

		return 0;
	}

	return 1;
}

void fast_guest_inv_tlb1(register_t va, int pid, int ind, int flags, int global)
{
	int i;
	gcpu_t *gcpu = get_gcpu();

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
	         "%s: va = %lx pid = %d ind = %d flags = %x global = %d\n",
	         __func__, va, pid, ind, flags, global);

	for (i = 0; i <= GUEST_TLB_END; i++) {
		if (gcpu->fast_tlb1_to_gtlb1[i] == -1)
			continue;

		mtspr(SPR_MAS0, MAS0_ESEL(i) | MAS0_TLBSEL(1));
		asm volatile("isync; tlbre" : : : "memory");

		/*
		 * The entry may be purged by a previous generous tlb
		 * invalidate done earlier with the calls to tlb_inv_addr()
		 * in guest_inv_tlb()'s TLB0 code path, so make it
		 * back valid so the match below has a chance to work.
		 */
		if (!(mfspr(SPR_MAS1) & MAS1_VALID)) {
			mtspr(SPR_MAS1, mfspr(SPR_MAS1) | MAS1_VALID);
			asm volatile("isync; tlbwe; isync; msync" : : : "memory");

			/* ... and read it back again */
			mtspr(SPR_MAS0, MAS0_ESEL(i) | MAS0_TLBSEL(1));
			asm volatile("isync; tlbre" : : : "memory");
		}

		if (guest_tlb1_match(mfspr(SPR_MAS1), mfspr(SPR_MAS2),
		                     va, pid, ind, flags, global)) {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
			         "%s: %d (real %d) ea = %lx MATCH\n",
			         __func__, gcpu->fast_tlb1_to_gtlb1[i],
			         i, mfspr(SPR_MAS2));
			free_tlb1(gcpu->fast_tlb1_to_gtlb1[i], 1);
			gcpu->fast_tlb1_to_gtlb1[i] = -1;
		} else {
			printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE,
			         "%s: %d (real %d) mas1 = %lx ea = %lx no match\n",
			         __func__, gcpu->fast_tlb1_to_gtlb1[i], i,
			         mfspr(SPR_MAS1), mfspr(SPR_MAS2));
		}
	}
}

#endif /* CONFIG_FAST_TLB1 */

