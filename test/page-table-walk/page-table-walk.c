/*
 * Copyright 2012 Freescale Semiconductor, Inc.
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

#include <libos/alloc.h>
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>
#include <hvtest.h>
#include <libfdt.h>


static int fail;

#define NOFAULT 0xdead0000
#define DTLB 1
#define DSI 2
#define MCHECK 3
#define PMAS_MAX_COUNT 12
#define MAX_SIZE_PER_PMA (32 * 1024 * 1024)

struct pma {
	phys_addr_t addr, size;
};

struct pma pmas[PMAS_MAX_COUNT];
int pmas_count;
int v4k_count;
static int *vaddrs[MAX_SIZE_PER_PMA * PMAS_MAX_COUNT / (4 * 1024)];
static int test_vals_1[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static int nofaults[8] = {0, 0, 0, 0, 0, 0, 0, 0};

void dtlb_handler(trapframe_t *frameptr)
{
	if ((uint32_t)frameptr->gpregs[0] != NOFAULT) {
		dump_regs(frameptr);

		printf("BROKEN\n");
		fh_partition_stop(-1);
		BUG();
	}

	frameptr->gpregs[0] = DTLB;
	frameptr->srr0 += 4;
}

void dsi_handler(trapframe_t *frameptr)
{
	if ((uint32_t)frameptr->gpregs[0] != NOFAULT) {
		dump_regs(frameptr);

		printf("BROKEN\n");
		fh_partition_stop(-1);
		BUG();
	}

	frameptr->gpregs[0] = DSI;
	frameptr->srr0 += 4;
}

void mcheck_interrupt(trapframe_t *frameptr)
{
	if ((uint32_t)frameptr->gpregs[0] != NOFAULT) {
		dump_regs(frameptr);

		printf("BROKEN\n");
		fh_partition_stop(-1);
		BUG();
	}

	frameptr->gpregs[0] = MCHECK;
	frameptr->srr0 += 4;

}

#define NUM_CORES 2

static int core_state[NUM_CORES];

static void sync_cores(int secondary)
{
	sync();
	isync();

	if (!secondary) {
		static int next_state;
		int i;

		next_state++;

		for (i = 1; i < NUM_CORES; i++)
			while (core_state[i] != next_state)
				sync();

		core_state[0] = next_state;
	} else {
		int pir = mfspr(SPR_PIR);
		core_state[pir]++;

		while (core_state[0] != core_state[pir])
			sync();
	}

	sync();
	isync();
}

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa,
                           int tsize, int pid, int space, int indirect)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT) |
	                (pid << MAS1_TID_SHIFT) | (space << MAS1_TS_SHIFT) |
					(indirect << MAS1_IND_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | MAS2_M);
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

static void store(const char *name, int pass, int num,
                  int **addrs, int *vals, int *faults, uint32_t *epids)
{
	int i;

	for (i = 0; i < num; i++) {
		int r0, val = vals[i];

		barrier(); /* force register flush to memory */

		if (epids && epids[i]) {
			mtspr(SPR_EPSC, epids[i]);
			isync();
			asm volatile("lis %%r0, 0xdead;"
			             "stwepx %2, %y1;"
			             "mr %0, %%r0;"
			             "li %%r0, 0" :
			             "=&r" (r0), "=Z" (*addrs[i]) : "r" (val) :
			             "r0");
		} else {
			asm volatile("lis %%r0, 0xdead;"
			             "stw%U0%X0 %2, %1;"
			             "mr %0, %%r0;"
			             "li %%r0, 0" :
			             "=&r" (r0), "=m" (*addrs[i]) : "r" (val) :
			             "r0");
		}

		if (faults[i]) {
			if (r0 == NOFAULT) {
				printf("%s, %lu: pass %d, val %d: expected "
				       "store fault but got none\n",
				       name, mfspr(SPR_PIR), pass, i);
				fail = 1;
			}
		} else if (r0 != NOFAULT) {
			printf("%s, %lu: pass %d, val %d: got "
			       "unexpected store fault %d\n",
			       name, mfspr(SPR_PIR), pass, i, r0);
			fail = 1;
		}
	}
}

static void expect(const char *name, int pass, int num,
                   int **addrs, int *vals, int *faults, uint32_t *epids)
{
	int i;


	for (i = 0; i < num; i++) {
		int r0, val;

		barrier(); /* force register flush to memory */

		if (epids && epids[i]) {
			mtspr(SPR_EPLC, epids[i]);
			isync();
			asm volatile("lis %%r0, 0xdead;"
			             "lwepx %0, %y2;"
			             "mr %1, %%r0;"
			             "li %%r0, 0" :
			             "=&r" (val), "=&r" (r0) : "Z" (*addrs[i]) :
			             "r0");
		} else {
			asm volatile("lis %%r0, 0xdead;"
			             "lwz%U0%X0 %0, %2;"
			             "mr %1, %%r0;"
			             "li %%r0, 0" :
			             "=&r" (val), "=&r" (r0) : "m" (*addrs[i]) :
			             "r0");
		}

		if (faults[i]) {
			if (r0 == NOFAULT) {
				printf("%s, %lu: pass %d, val %d: expected fault but got val %x\n",
				       name, mfspr(SPR_PIR), pass, i, val);
				fail = 1;
			}
		} else {
			if (r0 != NOFAULT) {
				printf("%s, %lu: pass %d, val %d: expected val %x but got fault %d\n",
				       name, mfspr(SPR_PIR), pass, i, vals[i], r0);
				fail = 1;
			} else if (val != vals[i]) {
				printf("%s, %lu: pass %d, val %d: expected val %x but got %x\n",
				       name, mfspr(SPR_PIR), pass, i, vals[i], val);
				fail = 1;
			}
		}
	}
}

#define PRIMARY 0
#define SECONDARY 1

static uint32_t tlbsync_lock;

static void tlbivax_inv_all(int tlb_mask)
{
	if (tlb_mask & 1)
		asm volatile("tlbivax 0, %0" : :
		             "r" (TLBIVAX_TLB0 | TLBIVAX_INV_ALL) :
		             "memory");
	if (tlb_mask & 2)
		asm volatile("tlbivax 0, %0" : :
		             "r" (TLBIVAX_TLB1 | TLBIVAX_INV_ALL) :
		             "memory");

	spin_lock(&tlbsync_lock);
	asm volatile("tlbsync" : : : "memory");
	sync();
	spin_unlock(&tlbsync_lock);
}

static void tlbivax_inv(void *addr, int tlb_mask, int pid)
{
	register_t ea = ((register_t)addr) & ~4095;

	if (tlb_mask & 1)
		asm volatile("tlbivax 0, %0" : :
		             "r" (TLBIVAX_TLB0 | ea) :
		             "memory");
	if (tlb_mask & 2)
		asm volatile("tlbivax 0, %0" : :
		             "r" (TLBIVAX_TLB1 | ea) :
		             "memory");

	spin_lock(&tlbsync_lock);
	asm volatile("tlbsync" : : : "memory");
	sync();
	spin_unlock(&tlbsync_lock);
}

static void tlbilx_inv_all(int tlb_mask)
{
	asm volatile("tlbilxlpid; isync" : : : "memory");
}

static void tlbilx_inv_pid(int pid)
{
	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT));
	asm volatile("isync; tlbilxpid; isync" : : : "memory");
}

static void tlbilx_inv(void *addr, int tlb_mask, int pid, int ind)
{
	register_t ea = ((register_t)addr) & ~4095;
	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | (ind << MAS6_SIND_SHIFT));

	asm volatile("isync; tlbilxva 0, %0; isync" : : "r" (ea) : "memory");
}

static void tlbsx(void *addr, int pid, int ind)
{
	register_t ea = ((register_t)addr) & ~4095;

	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT) | (ind << MAS6_SIND_SHIFT));

	asm volatile("isync; tlbsx 0, %0" : : "r" (ea));
}

static int read_pmas(void)
{
	int offset = -1, ret;
	const uint32_t *reg;

	offset = fdt_node_offset_by_compatible(fdt, offset, "fsl,pgtbl-test");
	while (offset != -FDT_ERR_NOTFOUND) {
		if (pmas_count == PMAS_MAX_COUNT) {
			printf("Too many PMAs in DTS\n");
			return -1;
		}

		ret = dt_get_reg(fdt, offset, 0, &pmas[pmas_count].addr,
		                 &pmas[pmas_count].size);
		if (ret < 0) {
			printf("BROKEN: missing/bad reg in PMA %d\n", ret);
			return -1;
		}
		pmas_count++;

		offset = fdt_node_offset_by_compatible(fdt, offset, "fsl,pgtbl-test");
	}

	printf("PMAs detected:\n");
	for (int i = 0; i < pmas_count; i++)
		printf("START: %llx SIZE %llx\n", pmas[i].addr, pmas[i].size);

	return 0;
}

typedef uint64_t pte_t;

static pte_t create_pte(unsigned long pa_pn)
{
	pte_t pte;

	pte = 0;
	pte |= pa_pn << HPTE_ARPN_SHIFT;
	pte |= HPTE_WIMGE_M;
	pte |= HPTE_BAP_SR | HPTE_BAP_SW;
	pte |= HPTE_VALID;
	pte |= HPTE_C;
	pte |= HPTE_R;
	pte |= 2 << HPTE_PS_SHIFT;

	return pte;
}

#define MAX_TEST_STEPS 10
int *test_vaddrs[MAX_TEST_STEPS];

/*
 *	This function is used to create some page table entries and to test
 *	several accesses in the mapped space:
 *	- the page table itself is located in the last 32M of the virtual
 *	  space. The physical location of the page table is in the first PMA
 *	- the second pma is the start of the memory area that will be map in
 *	  TLB0 through page tables. Although in hv cfg, there are multiple pmas
 *	  defined, the unit test assumes that the memory space is contiguous.
 *	- a number of page tables of different sizes (indicated by the tsizes array)
 *	  are created and a number of 8 entries within the map space are accessed.
 */
static void populate_pgtable(int steps, int *faults, int *tsizes,
                             int *test_vals, int test_steps,
                             int secondary)
{
	pte_t *pg_table;
	phys_addr_t pgtbl_pa;
	unsigned long pa_pn;
	unsigned long va_offset = 0;
	/* entry #0 is used to map the page table */
	unsigned int tlb_entry = 1;
	/* entry #15 is for initial entry */
	unsigned int max_entries_tlb1 = (mfspr(SPR_TLB1CFG) & TLBCFG_NENTRY_MASK) - 1;

	/* we are using the last 32M of the virtual space to map the page tables */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];
	/* we are using the first pma to store the page table */
	pgtbl_pa = pmas[0].addr;

	/* we create a number of page table entries starting with the second
	 * pma. The unit test assumes that the guest physical addresses are
	 * contiguous even if they are part of two consecutive pmas
	 */
	pa_pn = pmas[1].addr >> 12;

	/* create a TLB 1 mapping to map the page table such that it can
	 * be modified
	 */
	if (!secondary)
		create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_32M, 0, 0, 0);

	for (int step = 0; step < steps; step++) {
		unsigned long size_pages = tsize_to_pages(tsizes[step]);

		if (!secondary)
			for (int i = 0; i < size_pages; i++)
				pg_table[i] = create_pte(pa_pn + i);

		create_mapping(1, tlb_entry, vaddrs[va_offset], pgtbl_pa, tsizes[step], 0, 0, 1);

		for (int i = 0; i < test_steps; i++)
			test_vaddrs[i] = vaddrs[i + va_offset + (size_pages / test_steps) * i];

		if (!secondary)
			store("populate_pgtable", 1, 8, test_vaddrs, test_vals, faults, NULL);

		sync_cores(secondary);

		expect("populate_pgtable", 1, 8, test_vaddrs, test_vals, faults, NULL);

		sync_cores(secondary);

		pg_table += size_pages;
		pgtbl_pa += size_pages * 8;
		pa_pn += size_pages;
		va_offset += size_pages;
		tlb_entry++;

		if (tlb_entry == max_entries_tlb1)
			tlb_entry = 1;
	}
}

static void test_pgtable_extpid(int *test_vals, int secondary)
{
	pte_t *pg_table;
	phys_addr_t pgtbl_pa;
	unsigned long pa_pn;
	/* entry #0 is used to map the page table */
	unsigned int tlb_entry = 1;
	unsigned long size_pages = tsize_to_pages(TLB_TSIZE_2M);
	/* entry #15 is for initial entry */
	unsigned int max_entries_tlb1 = (mfspr(SPR_TLB1CFG) & TLBCFG_NENTRY_MASK) - 1;
	uint32_t epids[4] = {0x1, 0x80, 0xaf, 0x90 };

	/* we are using the last 32M of the virtual space to map the page
	 * tables
	 */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];
	/* we are using the first pma to store the page table */
	pgtbl_pa = pmas[0].addr;

	/* we create a number of page table entries starting with the second
	 * pma. The unit test assumes that the guest physical addresses are
	 * contiguous even if they are part of two consecutive pmas */
	pa_pn = pmas[1].addr >> 12;

	/* create a TLB 1 mapping to map the page table such that it can be
	 * modified
	 */
	if (!secondary)
		create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_32M, 0, 0, 0);

	for (int i = 0; i < 4; i++) {
		if (!secondary)
			pg_table[0] = create_pte(pa_pn);

		create_mapping(1, tlb_entry, vaddrs[0], pgtbl_pa, TLB_TSIZE_2M, epids[i], 0, 1);

		pa_pn += size_pages;
		pgtbl_pa += size_pages * 8;
		pg_table += size_pages;
		tlb_entry++;

		if (tlb_entry == max_entries_tlb1)
			tlb_entry = 1;

		test_vaddrs[i] = vaddrs[0];
	}

	if (!secondary)
		store("test_pgtable_extpid", 1, 4, test_vaddrs, test_vals, nofaults, epids);

	sync_cores(secondary);
	expect("test_pgtable_extpid", 1, 4, test_vaddrs, test_vals, nofaults, epids);
	sync_cores(secondary);
}


int tsizes_1[5] = {TLB_TSIZE_128M, TLB_TSIZE_32M, TLB_TSIZE_16M, TLB_TSIZE_4M, TLB_TSIZE_2M};
int tsizes_2[20] = {[0 ... 19] = TLB_TSIZE_2M};
int tsizes_3[20] = {[0 ... 19] = TLB_TSIZE_1M};

/*
 *	The following scenarios are tested by this function:
 *	1. Create 5 page tables of different sizes and test accesses to the
 *	   mapped space.
 *	2. Create 20 page tables of the same size and test accesses to the
 *	   mapped space. This test creates a number of indirect entries
 *	   larger than the TLB1 capacity.
 *	3. Create 20 page tables having a size smaller than 2M virtual address
 *	   space.
 *	4. Create page tables that maps the same virtual address to different
 *	   physical addresses, but each of them has different PIDs
 */
static void test_pgtable(int secondary)
{
	populate_pgtable(5, nofaults, tsizes_1, test_vals_1, 8, secondary);
	tlbilx_inv_all(2);

	sync_cores(secondary);

	populate_pgtable(20, nofaults, tsizes_2, test_vals_1, 8, secondary);

	tlbilx_inv_all(2);

	sync_cores(secondary);

	populate_pgtable(20, nofaults, tsizes_3, test_vals_1, 8, secondary);

	tlbilx_inv_all(2);

	sync_cores(secondary);

	test_pgtable_extpid(test_vals_1, secondary);
	tlbilx_inv_all(2);

	sync_cores(secondary);
}

/*
 *	This test performs the following steps:
 *	- a page table is created to map 2M of virtual space
 *	- the page table is located in an invalid memory area ( the guest
 *	  is not allowed to access that area).
 *	- the access should fail
 */
static void test_bad_mapping_pgtable(void)
{
	pte_t *pg_table;
	int fault = 1;
	phys_addr_t pgtbl_pa;

	/* we are using the last 32M of the virtual space to map the page tables */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];
	pgtbl_pa = (pmas[pmas_count - 1].addr + pmas[pmas_count - 1].size) >> 12;

	create_mapping(1, 0, pg_table, pmas[0].addr, TLB_TSIZE_32M, 0, 0, 0);
	pg_table[0] = create_pte(pmas[1].addr);

	create_mapping(1, 1, vaddrs[0], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 1);

	store("test_bad_mapping_pgtable", 2, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);
}

/*
 *	This test performs the following steps:
 *	- creates a page table located in the first PMA
 *	- the page table entry is created which maps 2M of virtual space
 *	  to an invalid address
 *	- the access should fail
 */
static void test_bad_mapping_lrat(void)
{
	pte_t *pg_table;
	int fault = 1;
	phys_addr_t pgtbl_pa;

	/* we are using the last 32M of the virtual space to map the page tables */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];
	pgtbl_pa = pmas[0].addr;

	create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_32M, 0, 0, 0);
	pg_table[0] = create_pte((pmas[pmas_count - 1].addr + pmas[pmas_count - 1].size) >> 12);

	create_mapping(1, 1, vaddrs[0], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 1);

	store("test_bad_mapping_lrat", 3, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);
}

/*
 *	The following scenarios are tested by this test:
 *	1. - Creates a page table which maps 128M of the virtual space
 *	   - The page table size is in this case 256KB. The HV config has
 *	     two different GPMAs which covers this area:
 *	   - pmas[1]: 128K
 *	   - pmas2[2]: 128K
 *	   - Topaz should create two different TLB 1 entries.
 *	   - The accesses should succeed
 *	2. - Creates a page table which maps 128M of the virtual space
 *	   - The page table size is in this case 256 KB. The HV config has
 *	     the following configuration:
 *	     pmas[3]: 128KB
 *	     hole  : 64KB
 *	     pmas[4]: 64KB
 *	   - Some accesses should fail ( the ones located in the area with
 *	     the hole ) and some should succeed (the ones located outside the
 *	     hole).
 */
static void test_pgtable_multiple_pmas(int secondary)
{
	pte_t *pg_table;
	phys_addr_t pgtbl_pa;
	unsigned long size_pages = tsize_to_pages(TLB_TSIZE_128M);
	int faults[8] = {0, 0, 0, 0, 0, 0, 0, 0};


	/* we are using the last 32M of the virtual space to map the page tables */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];

	/* we are using an indirect entry covering 128 MB of virtual space - this means
	 * 256 KB size for the page table. the page table will span across 2 PMAS.
	 * pmas[0] - actual memory that will be mapped by the page table
	 * pmas[1], pmas[2] - the memory where the page table is located
	 */
	pgtbl_pa = pmas[1].addr;

	if (!secondary) {
		create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_256K, 0, 0, 0);

		for (int i = 0; i < size_pages; i++)
			pg_table[i] = create_pte((pmas[0].addr >> 12) + i);
	}

	create_mapping(1, 1, vaddrs[0], pgtbl_pa, TLB_TSIZE_128M, 0, 0, 1);

	for (int i = 0; i < 8; i++)
		test_vaddrs[i] = vaddrs[i + (size_pages / 8) * i];

	if (!secondary)
		store("test_pgtable_multiple_pmas", 1, 8, test_vaddrs, test_vals_1, nofaults, NULL);
	sync_cores(secondary);
	expect("test_pgtable_multiple_pmas", 1, 8, test_vaddrs, test_vals_1, nofaults, NULL);
	sync_cores(secondary);


	tlbilx_inv_all(2);

	/* the page table is located in an area with holes in it. We have:
	 * PMA[3]: 128KB
	 * hole  : 64KB
	 * PMA[4]: 64KB
	 */
	pgtbl_pa = pmas[3].addr;

	if (!secondary) {
		create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_256K, 0, 0, 0);

		/* populate only the mapped page tables not to get faults at this point */
		for (int i = 0; i < pmas[3].size/8; i++)
			pg_table[i] = create_pte((pmas[0].addr >> 12) + i);

		for (int i = (pmas[4].addr - pmas[3].addr)/8; i < size_pages; i++)
			pg_table[i] = create_pte((pmas[0].addr >> 12) + i);
	}

	create_mapping(1, 1, vaddrs[0], pgtbl_pa, TLB_TSIZE_128M, 0, 0, 1);

	for (int i = 0; i < 8; i++)  {
		int vaddrs_offset = i + (size_pages / 8) * i;
		test_vaddrs[i] = vaddrs[vaddrs_offset];
		if ((vaddrs_offset >= pmas[3].size/8) && (vaddrs_offset < (pmas[4].addr - pmas[3].addr)/8))
			faults[i] = 1;
	}

	if (!secondary)
		store("test_pgtable_multiple_pmas", 1, 8, test_vaddrs, test_vals_1, faults, NULL);
	sync_cores(secondary);
	expect("test_pgtable_multiple_pmas", 1, 8, test_vaddrs, test_vals_1, faults, NULL);
	sync_cores(secondary);

	tlbilx_inv_all(2);
	sync_cores(secondary);

}

/*
 * This function tests that TLB invalidations and TLB searches take
 * into account the indirect bit.
 */
static void test_search_invalidate(void)
{
	pte_t *pg_table;
	phys_addr_t pgtbl_pa;
	unsigned long pa_pn;
	unsigned long va_offset = 0;
	/* entry #0 is used to map the page table */
	unsigned int tlb_entry = 1;
	int fault;

	/* we are using the last 32M of the virtual space to map the page tables */
	pg_table = (pte_t *)vaddrs[v4k_count - 8 * 1024];
	/* we are using the first pma to store the page table */
	pgtbl_pa = pmas[0].addr;

	/* we create a number of page table entries starting with the second
	 * pma. The unit test assumes that the guest physical addresses are
	 * contiguous even if they are part of two consecutive pmas
	 */
	pa_pn = pmas[1].addr >> 12;

	/* create a TLB 1 mapping to map the page table such that it can
	 * be modified
	 */
	create_mapping(1, 0, pg_table, pgtbl_pa, TLB_TSIZE_32M, 0, 0, 0);

	pg_table[0] = create_pte(pa_pn);

	/* test 1 */
	create_mapping(1, tlb_entry, vaddrs[va_offset], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 1);

	/* it should not find the entry because MAS6[SIND] is 0 */
	tlbsx(vaddrs[va_offset], 0, 0);
	if (mfspr(SPR_MAS1) & MAS1_VALID)
		fail = 1;
 
 	/* it should find the entry and ind bit should be set */
	tlbsx(vaddrs[va_offset], 0, 1);
	if (!(mfspr(SPR_MAS1) & MAS1_VALID) || !(mfspr(SPR_MAS1) & MAS1_IND))
		fail = 1;

	tlbilx_inv(vaddrs[va_offset], 0, 0, 0);

	fault = 0;

	store("test_search_invalidate", 1, 1, vaddrs, test_vals_1, &fault, NULL);
	expect("test_search_invalidate", 1, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);

	/* test 2 */
	create_mapping(1, tlb_entry, vaddrs[va_offset], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 1);

	tlbilx_inv(vaddrs[va_offset], 0, 0, 1);

	fault = 1;

	store("test_search_invalidate", 2, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);

	/* test 3 */
	create_mapping(1, tlb_entry, vaddrs[va_offset], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 0);

	/* it should not find the entry because MAS6[SIND] is 1 */
	tlbsx(vaddrs[va_offset], 0, 1);
	if (mfspr(SPR_MAS1) & MAS1_VALID)
		fail = 1;

	/* it should find the entry */
	tlbsx(vaddrs[va_offset], 0, 0);
	if (!(mfspr(SPR_MAS1) & MAS1_VALID))
		fail = 1;

	tlbilx_inv(vaddrs[va_offset], 0, 0, 1);

	fault = 0;

	store("test_search_invalidate", 3, 1, vaddrs, test_vals_1, &fault, NULL);
	expect("test_search_invalidate", 3, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);

	/* test 4 */
	create_mapping(1, tlb_entry, vaddrs[va_offset], pgtbl_pa, TLB_TSIZE_2M, 0, 0, 0);

	tlbilx_inv(vaddrs[va_offset], 0, 0, 0);

	fault = 1;

	store("test_search_invalidate", 4, 1, vaddrs, test_vals_1, &fault, NULL);

	tlbilx_inv_all(2);

}

static void secondary_entry_A(void)
{
	test_pgtable(SECONDARY);
}

static void secondary_entry_B(void)
{
	test_pgtable_multiple_pmas(SECONDARY);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret;
	pte_t entry;
	int *test_map;
	const char *label;
	int len;

	init(devtree_ptr);

	ret = read_pmas();
	if (ret < 0) {
		printf("FAILED\n");
		return;
	}

	printf("Page table walk test:\n");

	/* allocate 32M per PMA virtual space. The start address is 128M aligned
	   to allow creating large mappings */
	valloc_init(PHYSBASE + 0x20000000, PHYSBASE + 0x120000000);

	test_map = valloc(MAX_SIZE_PER_PMA * (pmas_count + 1), 128 * 1024 * 1024);
	if (test_map == NULL) {
		printf("Cannot allocate virtual mem\n");
		return;
	}

	/* partition the virtual space in 4K chunks as the subpage size is
	   4K and it is simpler to handle in unit test */
	v4k_count = MAX_SIZE_PER_PMA * (pmas_count + 1) / (1024 * 4);

	for (int i = 0; i < v4k_count; i++)
		vaddrs[i] = test_map + i * 1024;

	label = fdt_getprop(fdt, 0, "label", &len);
	if (!label) {
		printf("Error finding \"label\" property: %d\n", len);
		return;
	}

	if (strcmp(label, "pgtable-test-A") == 0) {

		printf("Page table walk test A\n");

		test_bad_mapping_lrat();

		test_bad_mapping_pgtable();

		test_search_invalidate();

		secondary_startp = secondary_entry_A;
		release_secondary_cores();

		test_pgtable(PRIMARY);

	} else if (strcmp(label, "pgtable-test-B") == 0) {

		printf("Page table walk test B\n");

		secondary_startp = secondary_entry_B;
		release_secondary_cores();

		test_pgtable_multiple_pmas(PRIMARY);
	} else {
		printf("Invalid test case label\n");
		return;
	}

	if (fail)
		printf("FAILED\n");
	else
		printf("PASSED\n");

	printf("Test Complete\n");

	fh_partition_stop(-1);
	BUG();
}
