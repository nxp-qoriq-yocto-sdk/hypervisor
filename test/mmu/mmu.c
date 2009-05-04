/*
 * Copyright (C) 2008 - 2009 Freescale Semiconductor, Inc.
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
#include <libos/hcalls.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>
#include <hvtest.h>

static int fail;

void dtlb_handler(trapframe_t *frameptr)
{
	if (frameptr->gpregs[0] != 0xdead0000) {
		dump_regs(frameptr);

		printf("BROKEN\n");
		fh_partition_stop(-1);
 		BUG();
	}

	frameptr->gpregs[0] = 1;
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

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa, int tsize)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | MAS2_M);
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

static void expect(const char *name, int pass, int num,
                   int **addrs, int *vals, int *faults)
{
	int i;

	for (i = 0; i < num; i++) {
		int r0, val;

		asm volatile("lis %%r0, 0xdead;"
		             "lwz%U0%X0 %0, %2;"
		             "mr %1, %%r0;"
		             "li %%r0, 0" :
		             "=&r" (val), "=&r" (r0) : "m" (*addrs[i]) : "r0");

		if (faults[i]) {
			if (r0 != 1) {
				printf("%s, %lu: pass %d, val %d: expected fault but got val %x\n",
				       name, mfspr(SPR_PIR), pass, i, val);
				fail = 1;
			}
		} else {
			if (r0 == 1) {
				printf("%s, %lu: pass %d, val %d: expected val %x but got fault\n",
				       name, mfspr(SPR_PIR), pass, i, vals[i]);
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

#define PRIMARY 0
#define SECONDARY_NOINVAL 1
#define SECONDARY_INVAL 2


static void inv_all_test(const char *name, void (*inv)(int tlbmask),
                         int secondary, int unified)
{
	int *tlb0 = test_map + 65536/4;
	int *tlb1 = test_map;

	int *addrs[3] = { tlb0, tlb1, tlb1 + 4096/4 };
	int vals[3] = { 0xee, 0xcc, 0xdd };

	phys_addr_t memphys = (phys_addr_t)(unsigned long)test_mem - PHYSBASE;

	int fault = 1;

	if (!secondary) {	
		test_mem[0] = 0xcc;
		test_mem[4096/4] = 0xdd;
		test_mem[65536/4] = 0xee;
	}

	if (secondary == SECONDARY_NOINVAL)
		fault = 0;

	create_mapping(1, 3, tlb1, memphys, TLB_TSIZE_64K);
	create_mapping(0, 0, tlb0, memphys + 65536, TLB_TSIZE_4K);

	sync_cores(secondary);
	expect(name, 1, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(0);

	sync_cores(secondary);
	expect(name, 2, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary) {
		tlb1[0] = 0x22;
		tlb1[4096/4] = 0x33;
		tlb0[0] = 0x44;
	}

	vals[1] = 0x22;
	vals[2] = 0x33;
	vals[0] = 0x44;

	sync_cores(secondary);
	expect(name, 3, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(1);

	sync_cores(secondary);
	
	if (unified) {
		expect(name, 4, 3, addrs, vals, (int[]){fault, fault, fault});
		create_mapping(1, 3, tlb1, memphys, TLB_TSIZE_64K);
	} else {
		expect(name, 4, 3, addrs, vals, (int[]){fault, 0, 0});
	}

	create_mapping(0, 0, tlb0, memphys + 65536, TLB_TSIZE_4K);
	expect(name, 5, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(2);

	sync_cores(secondary);
	
	if (unified) {
		expect(name, 6, 3, addrs, vals, (int[]){fault, fault, fault});
		create_mapping(0, 0, tlb0, memphys + 65536, TLB_TSIZE_4K);
	} else {
		expect(name, 6, 3, addrs, vals, (int[]){0, fault, fault});
	}

	create_mapping(1, 3, tlb1, memphys, TLB_TSIZE_64K);
	expect(name, 7, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(3);

	sync_cores(secondary);
	expect(name, 8, 3, addrs, vals, (int[]){fault, fault, fault});

	if (secondary == SECONDARY_NOINVAL)
		inv(3);

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

	asm volatile("tlbsync" : : : "memory");
	sync();
}

static void tlbivax_inv(void *addr, int tlb_mask)
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

	asm volatile("tlbsync" : : : "memory");
	sync();
}

static void inv_test(const char *name, void (*inv)(void *addr, int tlbmask),
                     int secondary, int unified)
{
	int *tlb0 = test_map + 65536/4;
	int *tlb1 = test_map;

	int *addrs[3] = { tlb0, tlb1, tlb1 + 4096/4 };
	int vals[3] = { 0xee, 0xcc, 0xdd };
	
	int fault = 1;

	phys_addr_t memphys = (phys_addr_t)(unsigned long)test_mem - PHYSBASE;

	if (!secondary) {
		test_mem[0] = 0xcc;
		test_mem[4096/4] = 0xdd;
		test_mem[65536/4] = 0xee;
	}

	if (secondary == SECONDARY_NOINVAL)
		fault = 0;

	create_mapping(1, 3, tlb1, memphys, TLB_TSIZE_64K);
	create_mapping(0, 0, tlb0, memphys + 65536, TLB_TSIZE_4K);

	sync_cores(secondary);
	expect(name, 1, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(inv_test, 3);

	sync_cores(secondary);
	expect(name, 2, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(tlb0, 2);

	sync_cores(secondary);
	expect(name, 5, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary && !unified)
		inv(tlb1, 1);

	sync_cores(secondary);
	expect(name, 6, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary) {
		tlb1[0] = 0x22;
		tlb1[4096/4] = 0x33;
		tlb0[0] = 0x44;
	}
	
	vals[1] = 0x22;
	vals[2] = 0x33;
	vals[0] = 0x44;

	sync_cores(secondary);
	expect(name, 7, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(tlb0, 1);

	sync_cores(secondary);

	expect(name, 8, 3, addrs, vals, (int[]){fault, 0, 0});

	create_mapping(0, 0, tlb0, memphys + 65536, TLB_TSIZE_4K);
	expect(name, 9, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(tlb1, 2);

	sync_cores(secondary);
	expect(name, 10, 3, addrs, vals, (int[]){0, fault, fault});

	create_mapping(1, 3, tlb1, memphys, TLB_TSIZE_64K);
	expect(name, 11, 3, addrs, vals, (int[]){0, 0, 0});
	sync_cores(secondary);

	if (!secondary)
		inv(tlb1 + 4096/4, 2);

	sync_cores(secondary);
	expect(name, 12, 3, addrs, vals, (int[]){0, fault, fault});
	sync_cores(secondary);

	if (!secondary)
		inv(tlb0, 1);

	sync_cores(secondary);
	expect(name, 12, 3, addrs, vals, (int[]){fault, fault, fault});

	if (secondary == SECONDARY_NOINVAL) {
		inv(tlb0, 1);
		inv(tlb1, 2);
	}

	sync_cores(secondary);
}

static void tlbivax_test(int secondary)
{
	inv_all_test("tlbivax.all", tlbivax_inv_all, secondary, 0);
	inv_test("tlbivax.ea", tlbivax_inv, secondary, 0);
}

static void tlbilx_inv_all(int tlb_mask)
{
	asm volatile("tlbilxlpid; isync" : : : "memory");
}

static void tlbilx_inv(void *addr, int tlb_mask)
{
	register_t ea = ((register_t)addr) & ~4095;
	asm volatile("tlbilxva 0, %0; isync" : : "r" (ea) : "memory");
}

static void tlbilx_test(int secondary)
{
	inv_all_test("tlbilx.all", tlbilx_inv_all, secondary, 1);
	inv_test("tlbilx.ea", tlbilx_inv, secondary, 1);
}

static void secondary_entry(void)
{
	mmucsr_test(SECONDARY_NOINVAL);
	tlbivax_test(SECONDARY_INVAL);
	tlbilx_test(SECONDARY_NOINVAL);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	test_mem = alloc(65536 + 4096, 65536);
	test_map = valloc(65536 + 4096, 65536);

	secondary_startp = secondary_entry;
	release_secondary_cores();

	printf("MMU test:\n");

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
