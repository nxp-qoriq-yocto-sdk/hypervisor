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
#include <libos/fsl_hcalls.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/platform_error.h>
#include <libos/io.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile int crit_int, mcheck_int;
static hv_error_t crit_err;
static int *test_map;
static int val;

#define BAD_PHYS 0x20000000

void mcheck_interrupt(trapframe_t *frameptr)
{
	mtspr(SPR_MCSR, mfspr(SPR_MCSR));
	frameptr->srr0 += 4;
	mcheck_int++;
}

void crit_int_handler(trapframe_t *regs)
{
	uint32_t bufsize = sizeof(hv_error_t);

	int ret = fh_err_get_info(global_error_queue, &bufsize, 0, virt_to_phys(&crit_err), 0);
	if (!ret)
		crit_int++;
}

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa, int tsize)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | MAS2_I | MAS2_G);
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int len, ret;
	const char *label;
	uint32_t bufsize = sizeof(hv_error_t);

	init(devtree_ptr);

	enable_extint();
	enable_critint();
	enable_mcheck();

	printf("CCF LAE test:\n");

	if (init_error_queues() < 0)
		return;

	printf("Test of error get in illegal guest space: ");
	ret = fh_err_get_info(global_error_queue, &bufsize, 0, BAD_PHYS, 0);
	if (!(global_error_queue == -1 && ret == EV_EINVAL) ||
	    !(global_error_queue != -1 && ret == EV_EFAULT)) {
		printf("PASSED (=%d)\n", ret);
	} else {
		printf("FAILED (=%d)\n", ret);
	}

	label = fdt_getprop(fdt, 0, "label", &len);
	if(!strcmp("/part2", label)) {
		while (!crit_int);

		printf("domain: %s, error: %s, path: %s\n", crit_err.domain, crit_err.error,
			crit_err.hdev_tree_path);
		printf("error detect reg: %x, error enable reg: %x, error attr: %x\n", crit_err.ccf.cedr,
			 crit_err.ccf.ceer, crit_err.ccf.cecar);
		printf("error attribute reg: %x, error address: %llx\n", crit_err.ccf.cmecar,
			crit_err.ccf.cecaddr);

		printf("PASSED\n");
	}  else {

		/* delay allows error manager partition to boot up */
		delay_timebase(500000);

		test_map = valloc(4096, 4096);

		/*Physical address corresponds to gpma1 i.e. pma3*/
		create_mapping(1, 3, test_map, BAD_PHYS, TLB_TSIZE_4K);

		/* load for generating a machine check */
		val = *test_map;

		lwsync();

		printf("%s\n", mcheck_int ? "PASSED" : "FAILED");

	}

	printf("Test Complete\n");
}
