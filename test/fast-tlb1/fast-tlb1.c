
/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
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


/*
 * fast tlb1 test program
 */

#include <libos/libos.h>
#include <libos/fsl_hcalls.h>
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trap_booke.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

#define PAGE_SHIFT		12

#define LOOPS			100000
#define TEST_PMA_COUNT		5
#define BASE_PHYS_ADDR		((phys_addr_t)0x80000000)
#define BASE_VA_SPLIT		((uintptr_t)0x60000000)
#define BASE_VA_NONSPLIT	((uintptr_t)0x80000000)
#define SPLIT_TLB_SIZE		TLB_TSIZE_64K

static void create_mapping(int entry, void *vaddr, phys_addr_t paddr, int tsize)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT) |
	                (0 << MAS1_TID_SHIFT) | (0 << MAS1_TS_SHIFT));
	mtspr(SPR_MAS2, ((register_t)vaddr) | MAS2_M);
	mtspr(SPR_MAS3, (uint32_t)paddr | MAS3_SR | MAS3_SW | MAS3_SX);
	mtspr(SPR_MAS7, (uint32_t)(paddr >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

static void clear_mapping(int entry)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, 0);

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

static void tlbilx_va(register_t vaddr)
{
	register_t ea = vaddr & ~4095;
	mtspr(SPR_MAS6, 0);
	asm volatile("isync; tlbilxva 0, %0; isync" : : "r" (ea) : "memory");
}

int got_miss;

static void tlb_miss(trapframe_t *frameptr)
{
	printf("%s: miss_addr = %lx\n",
		__func__,
		frameptr->exc == EXC_ITLB ? frameptr->srr0 : frameptr->dear);

	got_miss = 1;

	frameptr->gpregs[0] = 1;
	frameptr->srr0 += 4;
}

void dtlb_handler(trapframe_t *frameptr)
{
	tlb_miss(frameptr);
}

void itlb_handler(trapframe_t *frameptr)
{
	tlb_miss(frameptr);
}

static int do_store(int *addr)
{
	got_miss = 0;
	sync();
	*addr = 0xabab;
	sync();

	return got_miss;
}

static void test1(int verbose)
{
	int ret;

	ret = do_store((int *)(BASE_VA_SPLIT + 8192));
	printf("%s(0): (ret = %x)\n", __func__, ret);

	if (verbose) printf("%s: creating split mapping\n", __func__);
	create_mapping(0, (void *)BASE_VA_SPLIT, BASE_PHYS_ADDR, SPLIT_TLB_SIZE);

	ret = do_store((int *)(BASE_VA_SPLIT + 8192));
	if (!ret) {
		printf("%s(1): (ret = %x) PASSED\n", __func__, ret);
	} else {
		printf("%s(1): (ret = %x) FAILED\n", __func__, ret);
	}

	tlbilx_va(BASE_VA_SPLIT);

	ret = do_store((int *)(BASE_VA_SPLIT + 8192));
	if (ret) {
		printf("%s(2): (ret = %x) PASSED\n", __func__, ret);
	} else {
		printf("%s(2): (ret = %x) FAILED\n", __func__, ret);
	}

	create_mapping(1, (void *)BASE_VA_NONSPLIT, BASE_PHYS_ADDR, TLB_TSIZE_4K);

	ret = do_store((int *)BASE_VA_NONSPLIT);
	if (!ret) {
		printf("%s(3): (ret = %x) PASSED\n", __func__, ret);
	} else {
		printf("%s(3): (ret = %x) FAILED\n", __func__, ret);
	}

	tlbilx_va(BASE_VA_NONSPLIT);

	ret = do_store((int *)BASE_VA_NONSPLIT);
	if (ret) {
		printf("%s(4): (ret = %x) PASSED\n", __func__, ret);
	} else {
		printf("%s(4): (ret = %x) FAILED\n", __func__, ret);
	}

	create_mapping(0, (void *)BASE_VA_SPLIT, BASE_PHYS_ADDR, SPLIT_TLB_SIZE);
	tlbilx_va(BASE_VA_SPLIT);
}

static void stress_test(int verbose)
{
	char *p;
	int i, j;

	for (j = 0; j < LOOPS; j++) {
		if (verbose) printf("\nTEST LOOP %d\n\n", j);

		if (verbose) printf("creating split mapping\n");
		create_mapping(0, (void *)BASE_VA_SPLIT, BASE_PHYS_ADDR, SPLIT_TLB_SIZE);

		if (verbose) printf("creating non-split mappings\n");
		for (i = 0; i < TEST_PMA_COUNT; i++)
			create_mapping(i + 1, (void *)(BASE_VA_NONSPLIT + i * 8192), BASE_PHYS_ADDR + i * 8192, TLB_TSIZE_4K);

		if (verbose) printf("access split mapping\n");
		for (p = (char *)BASE_VA_SPLIT, i = 0; i < TEST_PMA_COUNT; p += 8192, i++) {
			*p = 0xab;
		}

		if (verbose) printf("accessing non-split mappings\n");
		for (p = (char *)BASE_VA_NONSPLIT, i = 0; i < TEST_PMA_COUNT; p += 8192, i++)
			*p = 0xab;

		if (verbose) printf("clear split mapping\n");
		tlbilx_va(BASE_VA_SPLIT);

		if (verbose) printf("clear non-split mappings\n");
		for (i = 1; i <= TEST_PMA_COUNT; i++)
			clear_mapping(i);

		if (verbose) printf("creating non-split mappings\n");
		for (i = 0; i < TEST_PMA_COUNT; i++)
			create_mapping(i + 1, (void *)(BASE_VA_NONSPLIT + i * 8192), BASE_PHYS_ADDR + i * 8192, TLB_TSIZE_4K);

		if (verbose) printf("creating split mapping\n");
		create_mapping(0, (void *)BASE_VA_SPLIT, BASE_PHYS_ADDR, SPLIT_TLB_SIZE);

		if (verbose) printf("accessing non-split mappings\n");
		for (p = (char *)BASE_VA_NONSPLIT, i = 0; i < TEST_PMA_COUNT; p += 8192, i++)
			*p = 0xab;

		if (verbose) printf("access split mapping\n");
		for (p = (char *)BASE_VA_SPLIT, i = 0; i < TEST_PMA_COUNT; p += 8192, i++)
			*p = 0xab;

		if (verbose) printf("clear split mapping\n");
		clear_mapping(0);

		if (verbose) printf("clearing non-split mappings\n");
		for (i = 0; i < TEST_PMA_COUNT; i++) {
			tlbilx_va(BASE_VA_NONSPLIT + i * 8192);
		}
	}
}

static void secondary_entry(void)
{
	printf("Fast TLB1 test (secondary)\n");

	test1(1);

	stress_test(0);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("Fast TLB1 test (primary)\n");

	test1(1);

	secondary_startp = secondary_entry;
	release_secondary_cores();

	stress_test(0);

	printf("Test passed\n");
}
