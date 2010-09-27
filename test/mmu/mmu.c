/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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

static int fail;

#define NOFAULT 0xdead0000
#define DTLB 1
#define DSI 2

void dtlb_handler(trapframe_t *frameptr)
{
	if (frameptr->gpregs[0] != NOFAULT) {
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
	if (frameptr->gpregs[0] != NOFAULT) {
		dump_regs(frameptr);

		printf("BROKEN\n");
		fh_partition_stop(-1);
 		BUG();
	}

	frameptr->gpregs[0] = DSI;
	frameptr->srr0 += 4;
}

#define NUM_CORES 4

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

static int *test_mem, *test_map;
static int *tlb0, *tlb1, *epidva;

static int *paddrs[6], *addrs[7];
static uint32_t epids[7] = { 0, 0, 0, 0x5a, 0x80, 0x40000080, 0xc0000080 };

/* The final one always faults due to the userspace bit */
static int nofaults[7] = { 0, 0, 0, 0, 0, 0, 1 };

static int vals[7] = { 0xee, 0xcc, 0xdd, 0x44, 0x66, 0x88, 0xaa };
static int vals2[7] = { 0x11, 0x55, 0x33, 0xff, 0xbb, 0x99, 0x00 };

#define PRIMARY 0
#define SECONDARY_NOINVAL 1
#define SECONDARY_INVAL 2

static void create_tlb0_mappings(void)
{
	create_mapping(0, 0, tlb0, virt_to_phys(paddrs[0]),
	               TLB_TSIZE_4K, 0, 0);
	create_mapping(0, 0, epidva, virt_to_phys(paddrs[3]),
	               TLB_TSIZE_4K, 0x5a, 0);
	create_mapping(0, 1, epidva, virt_to_phys(paddrs[5]),
	               TLB_TSIZE_4K, 0x80, 1);
}

static void create_tlb1_mappings(void)
{
	create_mapping(1, 3, tlb1, virt_to_phys(paddrs[1]),
	               TLB_TSIZE_64K, 0, 0);
	create_mapping(1, 4, epidva, virt_to_phys(paddrs[4]),
	               TLB_TSIZE_4K, 0x80, 0);
}

static void inv_all_test(const char *name, void (*inv)(int tlbmask),
                         int secondary, int unified)
{
	int fault = secondary != SECONDARY_NOINVAL;

	int allfault[7] = { fault, fault, fault, fault, fault, fault, 1 };
	int tlb0fault[7] = { fault, 0, 0, fault, 0, fault, 1 };
	int tlb1fault[7] = { 0, fault, fault, 0, fault, 0, 1 };

	if (!secondary) {
		for (int i = 0; i < 6; i++)
			*paddrs[i] = vals[i];
	}

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);
	expect(name, 1, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(0);

	sync_cores(secondary);
	expect(name, 2, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		store(name, 3, 7, addrs, vals2, nofaults, epids);

	sync_cores(secondary);
	expect(name, 3, 7, addrs, vals2, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(1);

	sync_cores(secondary);
	
	if (unified) {
		expect(name, 4, 7, addrs, vals2, allfault, epids);
		create_tlb1_mappings();
	} else {
		expect(name, 4, 7, addrs, vals2, tlb0fault, epids);
	}

	create_tlb0_mappings();
	expect(name, 5, 7, addrs, vals2, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(2);

	sync_cores(secondary);

	if (!secondary) {
		for (int i = 0; i < 6; i++)
			*paddrs[i] = vals[i];
	}

	sync_cores(secondary);
	
	if (unified) {
		expect(name, 6, 7, addrs, vals, allfault, epids);
		create_tlb0_mappings();
	} else {
		expect(name, 6, 7, addrs, vals, tlb1fault, epids);
	}

	create_tlb1_mappings();
	expect(name, 7, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(3);

	sync_cores(secondary);
	expect(name, 8, 7, addrs, vals, allfault, epids);

	if (secondary == SECONDARY_NOINVAL)
		inv(3);

	expect(name, 9, 7, addrs, vals, (int[]){1, 1, 1, 1, 1, 1, 1}, epids);

	sync_cores(secondary);
}

static void mmucsr_inv(int tlb_mask)
{
	int val = 0;
	
	if (tlb_mask & 1)
		val |= MMUCSR_L2TLB0_FI;
	if (tlb_mask & 2)
		val |= MMUCSR_L2TLB1_FI;

	mtspr(SPR_MMUCSR0, val);
	sync();
	isync();
}

static void mmucsr_test(int secondary)
{
	inv_all_test("mmucsr", mmucsr_inv, secondary, 0);
}

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

static void inv_test(const char *name,
                     void (*inv)(void *addr, int tlbmask, int pid),
                     int secondary, int unified, int matchpid)
{
	int fault = secondary != SECONDARY_NOINVAL;

	int tlb0fault[7] = { fault, 0, 0, 0, 0, 0, 1 };
	int tlb1fault[7] = { 0, fault, fault, 0, 0, 0, 1 };
	int tlb01fault[7] = { fault, fault, fault, 0, 0, 0, 1 };
	int epidvafault[7] = { 0, 0, 0, fault, fault, fault, 1 };

	if (!secondary) {
		for (int i = 0; i < 6; i++)
			*paddrs[i] = vals[i];
	}

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);
	expect(name, 1, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(inv_test, 3, 0);

	sync_cores(secondary);
	expect(name, 2, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(tlb0, 2, 0);

	sync_cores(secondary);
	expect(name, 5, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(tlb1, 1, 0);

	sync_cores(secondary);
	expect(name, 6, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		store(name, 7, 7, addrs, vals2, nofaults, epids);

	sync_cores(secondary);
	expect(name, 7, 7, addrs, vals2, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(tlb0, 1, 0);

	sync_cores(secondary);

	expect(name, 8, 7, addrs, vals2, tlb0fault, epids);
	create_tlb0_mappings();
	expect(name, 9, 7, addrs, vals2, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(tlb1, 2, 0);

	sync_cores(secondary);
	expect(name, 10, 7, addrs, vals2, tlb1fault, epids);

	create_mapping(1, 3, tlb1, virt_to_phys(paddrs[1]),
	               TLB_TSIZE_64K, 0, 0);
	expect(name, 11, 7, addrs, vals2, nofaults, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(tlb1 + 4096/4, 2, 0);

	sync_cores(secondary);
	expect(name, 12, 7, addrs, vals2, tlb1fault, epids);
	sync_cores(secondary);

	if (!secondary)
		inv(tlb0, 1, 0);

	sync_cores(secondary);
	expect(name, 13, 7, addrs, vals2, tlb01fault, epids);
	sync_cores(secondary);

	if (!secondary) {
		for (int i = 0; i < 6; i++)
			*paddrs[i] = vals[i];
	}

	sync_cores(secondary);

	/* Test simultaneous invalidates */
	create_mapping(1, 3, tlb1, virt_to_phys(paddrs[1]),
	               TLB_TSIZE_64K, 0, 0);
	expect(name, 14, 7, addrs, vals, tlb0fault, epids);

	sync_cores(secondary);

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);

	if (!secondary)
		inv(epidva, 3, 0);

	sync_cores(secondary);

	expect(name, 15, 7, addrs, vals,
	       matchpid ? nofaults : epidvafault, epids);

	sync_cores(secondary);

	if (!secondary)
		inv(epidva, 3, 0x80);

	sync_cores(secondary);

	expect(name, 17, 7, addrs, vals,
	       matchpid ? (int[]){ 0, 0, 0, 0, fault, fault, 1 } : epidvafault,
	       epids);

	sync_cores(secondary);

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);

	inv(tlb0, 1, 0);
	inv(tlb1, 2, 0);
	inv(epidva, 3, 0);
	inv(epidva, 3, 0x5a);
	inv(epidva, 3, 0x80);

	expect(name, 18, 7, addrs, vals, (int[]){1, 1, 1, 1, 1, 1, 1}, epids);

	sync_cores(secondary);
}

static void inv_pid_test(const char *name, void (*inv)(int pid), int secondary)
{
	int fault = secondary != SECONDARY_NOINVAL;

	if (!secondary) {
		for (int i = 0; i < 6; i++)
			*paddrs[i] = vals[i];
	}

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);
	expect(name, 1, 7, addrs, vals, nofaults, epids);
	sync_cores(secondary);

	inv(0);

	sync_cores(secondary);

	expect(name, 2, 7, addrs, vals,
	       (int[]){ 1, 1, 1, 0, 0, 0, 1 }, epids);

	sync_cores(secondary);

	create_tlb0_mappings();
	create_tlb1_mappings();

	sync_cores(secondary);

	expect(name, 3, 7, addrs, vals, nofaults, epids);

	sync_cores(secondary);

	inv(0x5a);

	sync_cores(secondary);

	expect(name, 4, 7, addrs, vals,
	       (int[]){ 0, 0, 0, 1, 0, 0, 1 }, epids);

	sync_cores(secondary);

	inv(0x80);

	expect(name, 5, 7, addrs, vals,
	       (int[]){ 0, 0, 0, 1, 1, 1, 1 }, epids);

	sync_cores(secondary);

	inv(0);

	expect(name, 6, 7, addrs, vals,
	       (int[]){ 1, 1, 1, 1, 1, 1, 1 }, epids);

	sync_cores(secondary);
}

static void tlbivax_test(int secondary)
{
	inv_all_test("tlbivax.all", tlbivax_inv_all, secondary, 0);
	inv_test("tlbivax.ea", tlbivax_inv, secondary, 0, 0);
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

static void tlbilx_test(int secondary)
{
	inv_all_test("tlbilx.all", tlbilx_inv_all, secondary, 1);
	inv_test("tlbilx.ea", tlbilx_inv, secondary, 1, 1);
	inv_pid_test("tlbilx.pid", tlbilx_inv_pid, secondary);
}

static void secondary_entry(void)
{
	mmucsr_test(SECONDARY_NOINVAL);
	tlbivax_test(SECONDARY_INVAL);
	tlbilx_test(SECONDARY_NOINVAL);
}

/*
 * Handler defined by test/common/libos_client.h
 */
void program_handler(trapframe_t *regs)
{
	register_t esr;
	
	/* Read Exception Syndrome Reg */
	esr = mfspr(SPR_ESR);
	
	/* HV raises an illegal program exception in case of TLB collision */
	if (!(esr & ESR_PIL))
		BUG();
 
	/* Remove the troublemaking 4k entry to allow current TLBWE to finish */
	tlbilx_inv(tlb0 - 4096/4, 0, 0);
	
	/* No need to clear the cause in ESR */
	fail = 0;	/* This test passed */
}


/*
 * This test maps two overlapping virtual spaces, tbl1 of 64K and tlb0 as
 * the last 4K page. We are not using HID0[EN_L2MMU_MHD] to trigger a machine
 * check in case of overlapping TLB entries as the HV generates illegal program
 * exception before allowing the conflicting entries to be set in the HW TLB.
 */
static void tlb_conflict_test(void)
{

	fail = 1;

	create_mapping(0, 0, tlb0 - 4096/4, virt_to_phys(paddrs[0]),
	               TLB_TSIZE_4K, 0, 0);
	create_mapping(1, 3, tlb1, virt_to_phys(paddrs[1]),
	               TLB_TSIZE_64K, 0, 0);

	/* Do some accesses to test the mappings */
	tlb1[1024] = 0;		/* No conflicting entries */
	barrier();
	tlb1[65536/4-1] = 0;	/* Conflicting entries */

	/* Clean the ground for the remaining tests */
	tlbilx_inv(tlb1, 0, 0);

	if (fail) {
		tlbilx_inv(tlb0 - 4096/4, 0, 0);
		puts("Unexpected permission to setup multiple matching TLB entries");
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	test_mem = alloc(65536 + 4096*4, 65536);
	test_map = valloc(65536 + 4096*2, 65536);

	tlb0 = test_map + 65536/4;
	tlb1 = test_map;
	epidva = test_map + 65536/4 + 1024;

	paddrs[0] = test_mem + 65536/4;
	paddrs[1] = test_mem;
	paddrs[2] = test_mem + 4096/4;
	paddrs[3] = test_mem + 65536/4 + 1024;
	paddrs[4] = test_mem + 65536/4 + 1024 * 2;
	paddrs[5] = test_mem + 65536/4 + 1024 * 3;

	addrs[0] = tlb0;
	addrs[1] = tlb1;
	addrs[2] = tlb1 + 4096 / 4;
	addrs[3] = addrs[4] = addrs[5] = addrs[6] = epidva;

	printf("MMU test:\n");
	
	tlb_conflict_test();

	secondary_startp = secondary_entry;
	release_secondary_cores();

	mmucsr_test(PRIMARY);
	tlbivax_test(PRIMARY);
	tlbilx_test(PRIMARY);
	
	if (fail)
		printf("FAILED\n");
	else
		printf("PASSED\n");

	printf("Test Complete\n");
	fh_partition_stop(-1);
	BUG();
}
