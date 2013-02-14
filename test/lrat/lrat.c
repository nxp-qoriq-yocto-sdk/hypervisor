/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
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

struct pma {
	phys_addr_t addr, size;
};

struct pma pmas[PMAS_MAX_COUNT];
int pmas_count;
static int *vaddrs[PMAS_MAX_COUNT + 1];

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
                           int tsize, int pid, int space)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT) |
	                (pid << MAS1_TID_SHIFT) | (space << MAS1_TS_SHIFT));
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

static int *test_map;

static int nofaults[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static int vals[10] = { 0xee, 0xcc, 0xdd, 0x44, 0x66, 0x88, 0xaa, 0x11, 0x22, 0x12 };
static int vals2[10] = { 0x11, 0x55, 0x33, 0xff, 0xbb, 0x99, 0x00, 0x33, 0x77, 0x13 };

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

static void tlbilx_inv(void *addr, int tlb_mask, int pid)
{
	register_t ea = ((register_t)addr) & ~4095;
	mtspr(SPR_MAS6, (pid << MAS6_SPID_SHIFT));
	asm volatile("isync; tlbilxva 0, %0; isync" : : "r" (ea) : "memory");
}

static int create_tlb_mappings(void)
{
	for (int i = 0; i < pmas_count; i++)
		create_mapping(0, 0, vaddrs[i], pmas[i].addr,
		              TLB_TSIZE_4K, 0, 0);
	return 0;
}

static void create_bad_tlb_mapping(void)
{
	create_mapping(0, 0, vaddrs[pmas_count], pmas[pmas_count - 1].addr + pmas[pmas_count - 1].size,
	               TLB_TSIZE_4K, 0, 0);
}

static int test_bad_mapping(void)
{
	int ret;
	int faults[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

	printf("Bad mapping test\n");

	ret = create_tlb_mappings();
	if (ret < 0) {
		printf("FAILED\n");
		return -1;
	}

	create_bad_tlb_mapping();

	store("test bad mapping", 1, 10, vaddrs, vals, faults, NULL);

	/* clear entries for the next test */
	tlbilx_inv_all(2);

	return 0;
}

static int test1(int secondary)
{
	int ret;

	printf("Test 1\n");

	ret = create_tlb_mappings();
	if (ret < 0) {
		printf("FAILED\n");
		return -1;
	}
	if (!secondary)
		store("test1", 1, 9, vaddrs, vals, nofaults, NULL);

	sync_cores(secondary);

	expect("test1", 1, 9, vaddrs, vals, nofaults, NULL);

	sync_cores(secondary);

	return 0;
}

static void secondary_entry(void)
{
	int ret;

	ret = test1(SECONDARY);
	if (ret < 0) {
		printf("FAILED\n");
		return;
	}

}

static int read_pmas(void)
{
	int offset = -1, ret;
	const uint32_t *reg;

	offset = fdt_node_offset_by_compatible(fdt, offset, "fsl,lrat-test");
	while (offset != -FDT_ERR_NOTFOUND) {
		if (pmas_count == PMAS_MAX_COUNT) {
			printf("Too many PMAs in DTS\n");
			return -1;
		}

		ret = dt_get_reg(fdt, offset, 0, &pmas[pmas_count].addr, &pmas[pmas_count].size);
		if (ret < 0) {
			printf("BROKEN: missing/bad reg in PMA %d\n", ret);
			return -1;
		}
		pmas_count++;

		offset = fdt_node_offset_by_compatible(fdt, offset, "fsl,lrat-test");
	}

	printf("PMAs detected:\n");
	for (int i = 0; i < pmas_count; i++)
		printf("START: %llx SIZE %llx\n", pmas[i].addr, pmas[i].size);

	return 0;
}



void libos_client_entry(unsigned long devtree_ptr)
{
	int ret;

	init(devtree_ptr);

	ret = read_pmas();
	if (ret < 0) {
		printf("FAILED\n");
		return;
	}
	test_map = valloc(4 * 1024 * (pmas_count + 1), 4 * 1024);
	for (int i = 0; i < pmas_count + 1; i++)
		vaddrs[i] = test_map + i*4*1024;

	printf("LRAT test:\n");

	ret = test_bad_mapping();
	if (ret < 0) {
		printf("FAILED\n");
		return;
	}

	secondary_startp = secondary_entry;
	release_secondary_cores();

	ret = test1(PRIMARY);
	if (ret < 0) {
		printf("FAILED\n");
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
