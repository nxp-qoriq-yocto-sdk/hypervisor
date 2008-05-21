#include <libos/libos.h>
#include <libos/hcalls.h>
#include <libos/trapframe.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/io.h>

void init(unsigned long devtree_ptr);
extern void *fdt;
int fail;

void dtlb_handler(trapframe_t *frameptr)
{
	if (frameptr->gpregs[0] != 0xdead0000) {
		dump_regs(frameptr);

		printf("FAILED\n");
		fh_partition_stop(-1);
 		BUG();
	}

	frameptr->gpregs[0] = 1;
	frameptr->srr0 += 4;
}

static void mmucsr_test(void)
{
	char *mem = alloc(65536 + 4096, 65536);
	char *tlb0 = valloc(4096, 4096);
	char *tlb1 = valloc(65536, 65536);
	char val;
	int r0;

	phys_addr_t memphys = (phys_addr_t)(unsigned long)mem - PHYSBASE;
	
	mem[0] = 0xcc;
	mem[4096] = 0xdd;
	mem[65536] = 0xee;

	/* Can't use tlb1_set_entry because it sets IPROT unconditionally. */
	mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(3));
	mtspr(SPR_MAS1, MAS1_VALID | (TLB_TSIZE_64K << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, (register_t)tlb1);
	mtspr(SPR_MAS3, (uint32_t)memphys | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(memphys >> 32));

	asm volatile("isync; tlbwe; isync" : : : "memory");

	mtspr(SPR_MAS0, 0);
	mtspr(SPR_MAS1, MAS1_VALID | (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, (register_t)tlb0);
	mtspr(SPR_MAS3, (uint32_t)(memphys + 65536) | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)((memphys + 65536) >> 32));

	asm volatile("isync; tlbwe; isync" : : : "memory");
	
	if (tlb0[0] != 0xee) {
		printf("mmucsr: pass 1: tlb0 contains %x, expected 0xee\n",
		       tlb0[0]);
		fail = 1;
	}

	if (tlb1[0] != 0xcc) {
		printf("mmucsr: pass 1: tlb1[0] contains %x, expected 0xcc\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0xdd) {
		printf("mmucsr: pass 1: tlb1[4096] contains %x, expected 0xdd\n",
		       tlb1[0]);
		fail = 1;
	}

	mtspr(SPR_MMUCSR0, 0);
	isync();

	if (tlb0[0] != 0xee) {
		printf("mmucsr: pass 2: tlb0 contains %x, expected 0xee\n",
		       tlb0[0]);
		fail = 1;
	}

	if (tlb1[0] != 0xcc) {
		printf("mmucsr: pass 2: tlb1[0] contains %x, expected 0xcc\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0xdd) {
		printf("mmucsr: pass 2: tlb1[4096] contains %x, expected 0xdd\n",
		       tlb1[0]);
		fail = 1;
	}

	tlb1[0] = 0x22;
	tlb1[4096] = 0x33;
	tlb0[0] = 0x44;

	if (tlb0[0] != 0x44) {
		printf("mmucsr: pass 2: tlb0 contains %x, expected 0x44\n",
		       tlb0[0]);
		fail = 1;
	}

	if (tlb1[0] != 0x22) {
		printf("mmucsr: pass 2: tlb1[0] contains %x, expected 0x22\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0x33) {
		printf("mmucsr: pass 2: tlb1[4096] contains %x, expected 0x33\n",
		       tlb1[0]);
		fail = 1;
	}

	mtspr(SPR_MMUCSR0, MMUCSR_L2TLB0_FI);
	isync();

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb0[0]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 3: tlb0 should have faulted but didn't\n");
		fail = 1;
	}

	if (tlb1[0] != 0x22) {
		printf("mmucsr: pass 3: tlb1[0] contains %x, expected 0x22\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0x33) {
		printf("mmucsr: pass 3: tlb1[4096] contains %x, expected 0x33\n",
		       tlb1[0]);
		fail = 1;
	}

	mtspr(SPR_MAS0, 0);
	mtspr(SPR_MAS1, MAS1_VALID | (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, (register_t)tlb0);
	mtspr(SPR_MAS3, (uint32_t)(memphys + 65536) | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)((memphys + 65536) >> 32));

	asm volatile("isync; tlbwe; isync" : : : "memory");

	if (tlb0[0] != 0x44) {
		printf("mmucsr: pass 4: tlb0 contains %x, expected 0x44\n",
		       tlb0[0]);
		fail = 1;
	}

	if (tlb1[0] != 0x22) {
		printf("mmucsr: pass 4: tlb1[0] contains %x, expected 0x22\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0x33) {
		printf("mmucsr: pass 4: tlb1[4096] contains %x, expected 0x33\n",
		       tlb1[0]);
		fail = 1;
	}

	mtspr(SPR_MMUCSR0, MMUCSR_L2TLB1_FI);
	isync();

	if (tlb0[0] != 0x44) {
		printf("mmucsr: pass 5: tlb0 contains %x, expected 0x44\n",
		       tlb0[0]);
		fail = 1;
	}

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb1[0]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 5: tlb1[0] should have faulted but didn't\n");
		fail = 1;
	}

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb1[4096]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 5: tlb1[4096] should have faulted but didn't\n");
		fail = 1;
	}

	/* Can't use tlb1_set_entry because it sets IPROT unconditionally. */
	mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(3));
	mtspr(SPR_MAS1, MAS1_VALID | (TLB_TSIZE_64K << MAS1_TSIZE_SHIFT));
	mtspr(SPR_MAS2, (register_t)tlb1);
	mtspr(SPR_MAS3, (uint32_t)memphys | MAS3_SR | MAS3_SW);
	mtspr(SPR_MAS7, (uint32_t)(memphys >> 32));

	asm volatile("isync; tlbwe; isync" : : : "memory");

	if (tlb0[0] != 0x44) {
		printf("mmucsr: pass 6: tlb0 contains %x, expected 0x44\n",
		       tlb0[0]);
		fail = 1;
	}

	if (tlb1[0] != 0x22) {
		printf("mmucsr: pass 6: tlb1[0] contains %x, expected 0x22\n",
		       tlb1[0]);
		fail = 1;
	}

	if (tlb1[4096] != 0x33) {
		printf("mmucsr: pass 6: tlb1[4096] contains %x, expected 0x33\n",
		       tlb1[0]);
		fail = 1;
	}

	mtspr(SPR_MMUCSR0, MMUCSR_L2TLB0_FI | MMUCSR_L2TLB1_FI);
	isync();

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb0[0]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 7: tlb0[0] should have faulted but didn't\n");
		fail = 1;
	}

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb1[0]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 7: tlb1[0] should have faulted but didn't\n");
		fail = 1;
	}

	asm volatile("lis %%r0, 0xdead; lwz %0, %2; mr %1, %%r0" :
	             "=r" (val), "=r" (r0) : "m" (tlb1[4096]) : "r0");

	if (r0 != 1) {
		printf("mmucsr: pass 7: tlb1[4096] should have faulted but didn't\n");
		fail = 1;
	}
}

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);
	printf("MMU test:\n");

	mmucsr_test();
	
	if (fail)
		printf("FAILED\n");
	else
		printf("PASSED\n");

	fh_partition_stop(-1);
 	BUG();
}
