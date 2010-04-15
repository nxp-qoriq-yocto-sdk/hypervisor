/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
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
#include <libos/platform_error.h>
#include <libos/io.h>
#include <hvtest.h>

static unsigned long non_coherent_write;
static volatile int crit_int;
static hv_error_t crit_err;

void crit_int_handler(trapframe_t *regs)
{
	uint32_t bufsize = sizeof(hv_error_t);

	int ret = fh_err_get_info(1, &bufsize, 0, virt_to_phys(&crit_err), 0);
	if (!ret)
		crit_int++;
}

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa, int tsize, int flag)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | (flag ? MAS2_M : 0));
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

#define NON_COHERENT 0
#define COHERENT 1

static int *test_mem, *test_map;
const char str[] = "ccf error test";

static void update_mem(int flag)
{
	phys_addr_t memphys = (phys_addr_t)(unsigned long)test_mem - PHYSBASE;


	create_mapping(1, 3, test_map, memphys, TLB_TSIZE_4K, flag);

	memcpy(test_map, str, strlen(str) + 1);
}


static void secondary_entry(void)
{
	uint32_t pir = mfspr(SPR_PIR);

	enable_extint();
	enable_critint();
	enable_mcheck();

	if (pir != 2) {
		update_mem(NON_COHERENT);
		atomic_add(&non_coherent_write, 1);
	} else {
		volatile unsigned long *ptr = &non_coherent_write;

		while(*ptr < 2);
		update_mem(COHERENT);
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	test_mem = alloc(4096, 4096);
	test_map = valloc(4096, 4096);

	secondary_startp = secondary_entry;
	release_secondary_cores();

	enable_extint();
	enable_critint();
	enable_mcheck();

	printf("CCF MINT test:\n");

	update_mem(NON_COHERENT);
	atomic_add(&non_coherent_write, 1);

	while (!crit_int);

	printf("domain: %s, error: %s, path: %s\n", crit_err.domain, crit_err.error,
		crit_err.hdev_tree_path);
	printf("error detect reg: %x, error enable reg: %x, error attr: %x\n", crit_err.ccf.cedr,
		 crit_err.ccf.ceer, crit_err.ccf.cecar);
	printf("error attribute reg: %x, error address: %llx\n", crit_err.ccf.cmecar,
		crit_err.ccf.cecaddr);

	printf("PASSED\n");

	printf("Test Complete\n");
}
