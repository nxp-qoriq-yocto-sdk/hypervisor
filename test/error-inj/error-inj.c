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

#define ONEMB (1024 * 1024)

typedef struct inj_regs {
	uint32_t inj_lo, inj_ctl;
} inj_regs_t;

static volatile int crit_int, mcheck_int;
static hv_error_t crit_err;
static uint32_t *test_map, *cpc_map;
static inj_regs_t *err_inj_reg;

#define CPC_ERRINJ_LO 0xE04
#define CPC_ERRINJ_CTL 0xE08
#define CPCCSR_CPCFI 0x200000

#define SB_ECC 0
#define MB_ECC 1

void mcheck_interrupt(trapframe_t *frameptr)
{
	mtspr(SPR_MCSR, mfspr(SPR_MCSR));
	frameptr->srr0 = frameptr->lr;
	mcheck_int++;
}

void crit_int_handler(trapframe_t *regs)
{
	uint32_t bufsize = sizeof(hv_error_t);

	int ret = fh_err_get_info(global_error_queue, &bufsize, 0, virt_to_phys(&crit_err), 0);
	if (!ret)
		crit_int++;
}

static inline void poll_reg_bit_clear(void *reg, uint32_t reg_val)
{
	out32(reg, in32(reg) | reg_val);
	while (in32(reg) & reg_val);
}

static void inv_cpc(void)
{
	out32(&err_inj_reg->inj_ctl, 0);
	poll_reg_bit_clear(cpc_map, CPCCSR_CPCFI);
}

static void dump_cpc_error(hv_error_t *err)
{
	cpc_error_t *cpc = &err->cpc;

	printf("domain:%s\n", err->domain);
	printf("error:%s\n", err->error);
	printf("device path:%s\n", err->hdev_tree_path);
	printf("cpc errdet : %x, cpc errinten : %x\n", cpc->cpcerrdet, cpc->cpcerrinten);
	printf("cpc errdis : %x\n", cpc->cpcerrdis);
	printf("cpc errattr : %x, cpc captecc : %x\n", cpc->cpcerrattr, cpc->cpccaptecc);
	printf("cpc erraddr : %llx, cpc errctl : %x\n", cpc->cpcerraddr, cpc->cpcerrctl);
}

static void inject_error(int type)
{
	switch (type) {
	case SB_ECC:
		out32(&err_inj_reg->inj_lo, 0x1000);
		out32(&err_inj_reg->inj_ctl, 0x100);		
		break;

	case MB_ECC:
		out32(&err_inj_reg->inj_lo, 0x1100);
		out32(&err_inj_reg->inj_ctl, 0x100);		
		break;
	}
}

static void create_mapping(int tlb, int entry, void *va, phys_addr_t pa, int tsize, int cache)
{
	mtspr(SPR_MAS0, MAS0_TLBSEL(tlb) | MAS0_ESEL(entry));
	mtspr(SPR_MAS1, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, ((register_t)va) | ((cache == 0) ? (MAS2_I | MAS2_G) : MAS2_M));
	mtspr(SPR_MAS3, (uint32_t)pa | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(pa >> 32));

	asm volatile("isync; tlbwe; msync; isync" : : : "memory");
}

__attribute__((noinline)) static int error_gen(int val, int type)
{
	for (int i = 0; i < ONEMB/sizeof(uint32_t); i++) {
		if (type == SB_ECC && crit_int)
			break;

		/* load required to generate the error */
		if (test_map[i] == val)
			return 1;
	}

	return 0;
}

static void init_data(void)
{
	for (int i = 0; i < ONEMB/sizeof(uint32_t); i++)
		test_map[i] = i;
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int i;

	init(devtree_ptr);

	enable_extint();
	enable_critint();
	enable_mcheck();

	printf("Error Injection test:\n");

	if (init_error_queues() < 0)
		return;

	cpc_map = valloc(4096, 4096);
	create_mapping(1, 3, cpc_map, 0xffe011000, TLB_TSIZE_4K, 0);
	err_inj_reg = (inj_regs_t *) ((uintptr_t) cpc_map + CPC_ERRINJ_LO);

	test_map = valloc(ONEMB, ONEMB);
	create_mapping(1, 4, test_map, 0x40000000, TLB_TSIZE_1M, 1);

	init_data();	

	inject_error(SB_ECC);
	delay_timebase(5000000);
	error_gen(ONEMB, SB_ECC);
	inv_cpc();
	printf("Single bit ecc test %s\n",
		 (crit_int) ? "PASSED" : "FAILED");

	dump_cpc_error(&crit_err);

	crit_int = 0;

	init_data();	

	inject_error(MB_ECC);
	delay_timebase(5000000);
	error_gen(ONEMB, MB_ECC);
	/* delay to ensure we have the critical interrupt */
	delay_timebase(5000000);
	inv_cpc();

	printf("Multi bit ecc test %s\n",
		(crit_int && mcheck_int) ? "PASSED" : "FAILED");

	dump_cpc_error(&crit_err);

	printf("Test Complete\n");
}
