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

#include <libos/libos.h>
#include <libos/fsl_hcalls.h>
#include <libos/alloc.h>
#include <libos/console.h>
#include <paging.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trapframe.h>
#include <libos/platform_error.h>
#include <libfdt.h>
#include <hvtest.h>
#include <stdbool.h>

#define MAXCPU		8
/* These offsets are relative to the ranges defined in HW DT for the DMA
 * controller. Each of 4 channels have guest physical already defined offset
 * from 0x100xx0
 */
#define DMA_MR		0x0
#define DMA_SR		0x1
#define DMA_SATR	0x4
#define DMA_SAR		0x5
#define DMA_DATR	0x6
#define DMA_DAR		0x7
#define DMA_BCR		0x8
#define DMA_CBs_MASK	0x04040404

#define TEST_PASSED	0x00001
#define TEST_INVALID	0x00100
#define TEST_FAILED	0x10000

/* Handler for Libos client function of secondary cores */
void secondary_core_handler(void);
static void do_illegal_dma(void);

static void print_test_summary(void);
static void end_test_and_print(register_t pir);
static int test_orig_interrupt(register_t pir);
static int test_claim_its_own(register_t pir);
static int test_claim_error_manager(register_t pir);
static void execute_tests(register_t pir);

static hv_error_t global_err, mcheck_err;
static phys_addr_t dma_phys, mem_phys, mem_size;
static uint32_t *dma_virt, *p_dma_dgsr, dma_dgsr;
static uint32_t liodn, error_manager_handle, slave_part_handle[2];
static bool is_error_manager, is_dma_owner[MAXCPU], was_restarted;
static int error_manager_node;
static register_t error_manager_cpu;
static volatile unsigned int mcheck_cnt[MAXCPU], crit_cnt[MAXCPU],
			     old_crit_cnt[MAXCPU];
static volatile unsigned int *test_stat;
static unsigned long cpulive_cnt;
static const char *label;
static unsigned long *shared_memory;

/* General description of the unit test
 * The "unit" test includes three partitions with the following roles:
 * - /part2 is the initial error manager, claimed as active in config tree
 * - /part1 is a slave guest launched by /part2 and which has a DMA channel.
 *   It has one of the two DMA controller allocated to it which will be used
 *   for an illegal DMA access.
 * - /part3 is a slave guest launched by /part2 after which the manager
 *   partition will end itself. The slave also has a second DMA controller which
 *   will be used to initiate an illegal access.
 * - /part1 and /part3 will remain alive and race toward claiming the error
 *   manager and verifying that indeed the new critical interrupts with global
 *   errors arrive to the new owner.
 * More details about each test are included above the function definition.
 */

static void map_shared_memory(void)
{
	if (shared_memory) {
		tlb1_set_entry(2, (uintptr_t)shared_memory, 0x2000000,
			       TLB_TSIZE_4K, MAS2_M | MAS2_G, TLB_MAS3_KERN,
			       0, 0, 0);
	} else {
		printf("Unable to map shared memory\n");
	}
}

static int dma_init(void)
{
	int node, len;
	const uint32_t *p_liodn;

	/* Get a DMA controller */
	node = fdt_node_offset_by_compatible(fdt, 0, "fsl,eloplus-dma");
	if (node < 0)
		return -1;

	p_liodn = fdt_getprop(fdt, node, "fsl,hv-dma-handle", &len);
	if (!p_liodn) {
		printf("fsl,hv-dma-handle property not found %d\n", len);
		return -5;
	}
	if (len != 4) {
		printf("Unexpected array of LIODNs with %d elements\n", len>>2);
		return -6;
	}

	if (dt_get_reg(fdt, node, 0, &dma_phys, NULL) < 0)
		return -2;
	printf("Retrieved a DMA controller @ 0x%llx\n",
	       (unsigned long long)dma_phys);

	/* Allocate a virtual space to access DMA config regs */
	p_dma_dgsr = valloc(4096, 4096);
	if (!p_dma_dgsr)
		return -4;

	/* Go to offset of the regs into the virtual space */
	p_dma_dgsr = (void *)p_dma_dgsr + (dma_phys & (PAGE_SIZE - 1));

	tlb1_set_entry(3, (uintptr_t)p_dma_dgsr, dma_phys,
	               TLB_TSIZE_4K, TLB_MAS2_IO,
	               TLB_MAS3_KERN, 0, 0, 0);

	/* Get reference to a DMA channel from that controller */
	node = fdt_node_offset_by_compatible(fdt, node, "fsl,eloplus-dma-channel");
	if (node < 0)
		return -1;

	if (dt_get_reg(fdt, node, 0, &dma_phys, NULL) < 0)
		return -2;

	printf("Retrieved a DMA channel @ 0x%llx\n", (unsigned long long)dma_phys);

	/* Allocate a virtual space to access DMA config regs */
	dma_virt = valloc(4096, 4096);
	if (!dma_virt)
		return -4;

	/* Go to offset of the regs into the virtual space */
	dma_virt = (void *)dma_virt + (dma_phys & (PAGE_SIZE - 1));

	tlb1_set_entry(4, (uintptr_t)dma_virt, dma_phys,
	               TLB_TSIZE_4K, TLB_MAS2_IO,
	               TLB_MAS3_KERN, 0, 0, 0);

	/* Read the status of DMA channels from the current DMA controller */
	printf("DMA DGSR: %08x\n", dma_dgsr = in32(p_dma_dgsr));
	printf("Chan  MR: %08x\n", in32(&dma_virt[DMA_MR]));
	printf("Chan  SR: %08x\n", in32(&dma_virt[DMA_SR]));

	liodn = *p_liodn;
	printf("LIODN   : %d\n", liodn);

	/* Are all the channels idle? */
	if (dma_dgsr & DMA_CBs_MASK) {
		printf("Conflicting with some DMA channels already busy\n");
		return -6;
	}

	/* Enable PAMU entry related to this DMA transfer based on DTS dma-window */
	if (fh_dma_enable(liodn)) {
		printf("fh_dma_enable: failed %d\n", liodn);
		return -6;
	}

	return 0;
}

static bool is_active_error_manager(bool check_this_core)
{
	int len;
	bool ret = 0;
	register_t pir;
	const void *p_handle;

	if (error_manager_node < 0)
		return 0;

	pir = mfspr(SPR_PIR);

	p_handle = fdt_getprop(fdt, error_manager_node, "fsl,hv-claimable",
			       &len);
	if (!p_handle || !strncmp("active", p_handle, len)) {
		printf("[%ld] Active error manager partition\n", pir);
		ret = 1;
	} else {
		printf("[%ld] Standby error manager partition\n", pir);
	}

	if (!check_this_core)
		return ret;

	p_handle = fdt_getprop(fdt, error_manager_node,
			       "fsl,hv-error-manager-cpu", &len);
	if (p_handle && len == 4 && *(uint32_t *)p_handle == pir) {
		if (ret)
			printf("[%ld] Error manager CPU\n", pir);
		ret &= 1;
		error_manager_cpu = pir;
	} else {
		ret &= 0;
	}

	return ret;
}

/* Error management in driver-like format
*/
static int error_manager_probe(void)
{
	int len, ret;
	register_t pir;
	const void *p_handle;

	pir = mfspr(SPR_PIR);

	/* Check the path towards error manager */
	error_manager_node = fdt_path_offset(fdt, "/hypervisor/handles/error-manager");
	if (error_manager_node < 0) {
		printf("[%ld] This guest is not error-manager\n", pir);
		return -2;
	} else {
		printf("[%ld] Found the error-manager\n", pir);
	}

	p_handle = fdt_getprop(fdt, error_manager_node, "reg", &len);
	if (!p_handle || len != 4) {
		printf("[%ld] Error getting error manager handle: %d\n", pir,
		       len);
		return -2;
	}
	error_manager_handle = *(uint32_t*)p_handle;
	printf("[%ld] Error manager handle: %d\n", pir, error_manager_handle);

	is_error_manager = is_active_error_manager(0);

	return 0;
}

/* The following code setup a legal DMA transfer but which is outside the
 * dma-window defined in the HV config tree. There is no machine check but only
 * a PAMU access violation.
*/
static void do_illegal_dma(void)
{
#define DMA_PROG_MODE	0x00000004
	phys_addr_t src, dst;
	const unsigned int trans_size = 0x200;	/* counter of bytes */

	/* Compute arbitrary phys addresses in guest space */
	src = mem_phys + mem_size - 0x800;
	dst = mem_phys + mem_size - 0x200;

	/* P4080RM: MRn[CTM], to indicate direct mode and clear CS */
	out32(&dma_virt[DMA_MR], DMA_PROG_MODE);

	/* P4080RM: Initialize SARn, SATRn, DARn, DATRn and BCRn */
	/* src addr */
	out32(&dma_virt[DMA_SAR],  (uint32_t)src);
	/* src attr: rd */
	out32(&dma_virt[DMA_SATR], (uint32_t)(src >> 32) | 0x00040000);
	out32(&dma_virt[DMA_DAR],  (uint32_t)dst);
	/* dst attr: wr */
	out32(&dma_virt[DMA_DATR], (uint32_t)(dst >> 32) | 0x00040000);
	out32(&dma_virt[DMA_BCR],  trans_size);

	/* P4080RM: set MRn[CS], to start the DMA transfer*/
	out32(&dma_virt[DMA_MR], DMA_PROG_MODE | 1);
}

static void print_error(hv_error_t *p_err)
{
	if (!p_err) return;
	printf("error domain: %s, error: %s,\n",
		p_err->domain, p_err->error);
	printf(" -> HV path: %s, Guest path: %s\n",
		p_err->hdev_tree_path, p_err->gdev_tree_path);
	if (strcmp(p_err->domain, "pamu"))
		return;
	printf(" -> PAMU access violation avs1 = %08x, avs2 = %08x, av_addr = %09llx\n",
		p_err->pamu.avs1, p_err->pamu.avs2,
		p_err->pamu.access_violation_addr);
	printf(" -> PAMU access violation LPID = %d, LIODN = %d\n",
		p_err->pamu.lpid, p_err->pamu.liodn_handle);
}

void secondary_core_handler(void)
{
	int ret;
	register_t pir;

	map_shared_memory();

	pir = mfspr(SPR_PIR);

	enable_extint();
	enable_critint();
	enable_mcheck();

	puts("Secondary core online");

	ret = dma_init();
	if (ret == 0)
		is_dma_owner[pir] = 1;

	atomic_add(&cpulive_cnt, +1);

	execute_tests(pir);

	assert(0);	/* should never get here */
}

void libos_client_entry(unsigned long devtree_ptr)
{
	uint32_t bufsize;
	int ret, node, len;
	register_t pir;

	pir = mfspr(SPR_PIR);

	/* Link the second VCPU of the partition */
	secondary_startp = secondary_core_handler;

	init(devtree_ptr);
	init_error_queues();

	puts("Testing the error-manager switch between partitions");

	label = fdt_getprop(fdt, 0, "label", &len);
	if (!label) {
		printf("Error finding \"label\" property: %d\n", len);
		return;
	}
	printf("Partition %s started\n", label);

	node = fdt_subnode_offset(fdt, 0, "memory");
	if (node < 0) {
		puts("No memory for partition");
		return;
	}

	/* read a single pair of addr/size */
	if (dt_get_reg(fdt, node, 0, &mem_phys, &mem_size) < 0) {
		puts("Invalid memory range");
		return;
	}
	printf("Mem start/size: 0x%llx, 0x%llx\n", mem_phys, mem_size);

	if (!shared_memory) /* boot core allocate virtual space */
		shared_memory = valloc(4*1024, 4*1024);
	map_shared_memory(); /* each core maps its TLB entry */

	/* Map test results into a "persistent" area: guests are restarted */
	test_stat = (unsigned int *)((void *)shared_memory + 0x100);
	if (strcmp(label, "/part1") == 0)
		test_stat += 0*MAXCPU;
	else if (strcmp(label, "/part2") == 0)
		test_stat += 1*MAXCPU;
	else if (strcmp(label, "/part3") == 0)
		test_stat += 2*MAXCPU;
	else
		assert(0);

	int hv_node = fdt_path_offset(fdt, "/hypervisor");
	const char *why_stopped = fdt_getprop(fdt, hv_node,
					      "fsl,hv-reason-stopped", NULL);
	if (why_stopped && !strcmp(why_stopped, "restart")) {
		printf("Restarted %s\n", label);
		was_restarted = 1;
		if (strcmp(label, "/part2") == 0) {
			if (was_restarted)
				/* Only clean the barriers */
				memset(shared_memory, 0, 2*sizeof(long));
			else
				/* Init once; only 1 guest runs, with CPU0 */
				memset(shared_memory, 0, 4096);
		}
	}

	enable_extint();
	enable_critint();
	enable_mcheck();

	error_manager_probe();

	/* Check the path towards slave partition, if any */
	node = fdt_path_offset(fdt, "/hypervisor/handles");
	if (node < 0)
		printf("[%ld] 'handles' node not found\n", pir);
	else {
		const void * p_handle;
		for (int i = 0; i < 2; i++) {
			node = fdt_node_offset_by_compatible(fdt, node,
						"fsl,hv-partition-handle");
			if (node <= 0)
				continue;

			p_handle = fdt_getprop(fdt, node, "reg", &len);
			if (!p_handle || len != 4) {
				printf("[%ld] Error getting slave "
					"partition handle: %d\n", pir,
					len);
				return;
			}
			slave_part_handle[i] = *(uint32_t*)p_handle;
			printf("[%ld] Found the slave partition handle: %d\n",
				pir, slave_part_handle[i]);
		}
	}

	atomic_add(&cpulive_cnt, +1);
	ret = release_secondary_cores(); /* returns # of 2ndary CPUs started */
	if (ret >= 0) {	/* barrier at guest level */
		while (atomic_add(&cpulive_cnt, 0) != ret + 1);
	}

	execute_tests(pir);

	assert(0);	/* should never get here */
}

/* Handler defined by test/common/libos_client.h
 * Error manager is informed of errors in the global queue by a critical interrupt
 * Need to retrieve the error info otherwise it will be raised again and again
 * The only source of critical interrupt is the global error queue, not VMPIC,
 * so no need to check the source of interrupt
 */
void crit_int_handler(trapframe_t *regs)
{
	uint32_t bufsize = sizeof(hv_error_t);
	int ret;
	register_t pir;
	char str[30];

	pir = mfspr(SPR_PIR);
	/* Print in ISR for unit test only, no use of printf/puts => deadlock risk */
	console_write_nolock(str,
			snprintf(str, sizeof(str), "Crit int recv by: %ld\n",
				 pir));

	ret = fh_err_get_info(global_error_queue, &bufsize, 0,
			      virt_to_phys(&global_err), 0);
	if (ret)
		console_write_nolock(str,
				snprintf(str, sizeof(str),
					"Err read part err queue: %d\n", ret));
	else
		crit_cnt[pir]++;
}

/* Handler defined by test/common/libos_client.h
 * Guest executing an illegal memory access itself or by DMA gets a machine
 * check interrupt. Need to retrieve the error info because if not, it will be
 * raised repeatedly.
 */
void mcheck_interrupt(trapframe_t *regs)
{
	int ret;
	register_t mcsr, pir;
        uint32_t bufsize = sizeof(hv_error_t);
	char str[30];

	pir = mfspr(SPR_PIR);

	/* Read Machine Check Syndrome Reg */
	mcsr = mfspr(SPR_MCSR);

	if (!(mcsr & MCSR_MCP)) {
		console_write_nolock(str,
			snprintf(str, sizeof(str), "Unexp src of MCSR: %08lx\n",
				 mcsr));
		BUG();
	}

	ret = fh_err_get_info(guest_error_queue, &bufsize, 0,
			      virt_to_phys(&mcheck_err), 0);
	if (ret)
		console_write_nolock(str,
			snprintf(str, sizeof(str), "Err read part err queue: %d\n",
				 ret));
	else
		mcheck_cnt[pir]++;

	/* Clear cause */
	mtspr(SPR_MCSR, mcsr);
}

static void test_barrier(int which, int cpu_cnt)
{
	register_t pir = mfspr(SPR_PIR);

	if (atomic_add(&shared_memory[which], 1) == cpu_cnt) {
		atomic_and(&shared_memory[which], 0); /* reset to 0 */
	} else {
		while (atomic_add(&shared_memory[which], 0) != 0);
	}

	old_crit_cnt[pir] = crit_cnt[pir]; /* not very safe */
}

static void execute_tests(register_t pir)
{
	if (strcmp(label, "/part3") != 0) { /* except for /part3 */
		test_stat[pir] += test_orig_interrupt(pir);

		test_barrier(0, 4);
		if (!was_restarted)
			fh_partition_restart(-1);

		test_stat[pir] += test_claim_its_own(pir);
	}
	test_stat[pir] += test_claim_error_manager(pir);

	end_test_and_print(pir);
}

static void test_common_do_dma(register_t pir, bool is_err_owner)
{
	if (is_dma_owner[pir]) { /* dma_init is called from secondary cpu */
		/* Slave partitions have DMA to cause machine check */
		printf("[%ld] Initiate DMA transfer\n", pir);
		do_illegal_dma();
	}

	/* Wait for a global error in the queue or a machine check caused by
	 * illegal access
	 */
	if (is_err_owner)
		while (crit_cnt[pir] == 0);
	else if (is_dma_owner[pir] && pir == 0)
		/* if not error owner, wait for machine check on boot cpu */
		while (mcheck_cnt[pir] == 0);

	delay_timebase(5000000); /* wait for whatever interrupts to be delivered */

	if (crit_cnt[pir]) {
		printf("[%ld] Received critical interrupt\n", pir);
		printf("Critical interrupt "); print_error(&global_err);
	}
	if (mcheck_cnt[pir]) {
		printf("[%ld] Received machine check interrupt\n", pir);
		printf("Machine check "); print_error(&mcheck_err);
	}
	printf("[%ld] # of int/MC: %d/%d\n", pir, crit_cnt[pir],
	       mcheck_cnt[pir]);

	if (is_dma_owner[pir])
		/* After an illegal PAMU access, the PAACE is invalid */
		fh_dma_enable(liodn);
}

/* Test that we receive critical interrupt for the initial error manager.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - Start the slave guest which owns the DMA (DMA is owned by one single core).
 *   Only one core of the master partition will succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and one core has DMA.
 * - Once the test is executed, all guests restart themselves to verify that the
 *   error manager is reassigned when alive (see execute_tests()). The first
 *   test executes again.
 * Results validation:
 * - The core owning the error manager must receive 1 critical interrupt.
 * Not tested (outside of test scope):
 * - The boot core of the slave guest having DMA must receive 1 machine check.
 */
static int test_orig_interrupt(register_t pir)
{
	int ret;
	/* is this CPU the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	if (!dma_virt) {
		/* This partition does not have DMA, must be the master */
		assert(slave_part_handle[0] && slave_part_handle[1]);

		/* Whichever core is first */
		ret = fh_partition_start(slave_part_handle[0], 0);
		if (!ret) {/* This colision between cores is expected */
			printf("[%ld] Started partition %d\n", pir,
			       slave_part_handle[0]);
		}
	}

	test_common_do_dma(pir, is_err_owner);

	/* Expect one crit int because there only one guest has DMA */
	if ((is_err_owner && crit_cnt[pir] == old_crit_cnt[pir] + 1) ||
			(!is_err_owner && crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 1: PASSED\n", pir);
		ret = TEST_PASSED;
	} else {
		printf("[%ld] Test 1: FAILED\n", pir);
		ret = TEST_FAILED;
	}

	/* barrier is outside the test to save the results */
	return ret;
}

/* Test that a guest can claim the error manager that already owns and others
 * don't since the owner is not stopped. Check that critical interrupts still
 * arrive where they should.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - The slave /part1 is alive, and all resources are mapped/initialized:
 *   shared memory, DMA handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - All 4 cores claim the error manager. No core should succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and one core has DMA.
 * Results validation:
 * - All cores should receive an invalid state error code for hcall.
 * - The core owning the error manager must receive 1 critical interrupt.
 * Not tested (outside of test scope):
 * - The boot core of the slave guest having DMA must receive 1 machine check.
 */
static int test_claim_its_own(register_t pir)
{
	int ret;
	/* is this CPU the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	/* Might have been nice to allow a current owner to claim its own
	 * error manager, but it could be just an useless corner case; so all
	 * return codes are EV_INVALID_STATE. */
	ret = fh_claim_device(error_manager_handle);

	test_common_do_dma(pir, is_err_owner);

	/* Expect one crit int because only one guest has DMA */
	if ((is_err_owner && ret == EV_INVALID_STATE &&
				crit_cnt[pir] == old_crit_cnt[pir] + 1) ||
				(!is_err_owner && ret == EV_INVALID_STATE &&
				crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 2: PASSED\n", pir);
		ret = TEST_PASSED;
	} else {
		if (ret == EV_UNIMPLEMENTED)
			/* Abandon remaining tests as there is no claim device */
			end_test_and_print(pir);

		printf("[%ld] Test 2: FAILED with return code %d\n", pir, ret);
		ret = TEST_FAILED;
	}

	test_barrier(1, 4);
	return ret;
}

/* Test that a guest can claim the error manager once the previous owner is
 * stopped.
 * Initial state:
 * - The manager partition is alive, and all resources are mapped/initialized:
 *   shared memory, slave handles, error manager handle.
 * - The slave /part1 is alive, and all resources are mapped/initialized:
 *   shared memory, DMA handle.
 * - Each guest is multicore and all cores perform similar actions (not equal).
 * Test action:
 * - The manager guest (without DMA) starts the second slave /part3. One of its
 *   core will succeed, the other should fail.
 * - The manager guest, currently owning the error manager, stop itself.
 * - All 4 cores (2 from each slave guest) synchronize at a barrier.
 * - Subtest 3_1 tests that any core that was not declared in the config DTS
 *   as being allowed to claim the error manager, will not be allowed to claim
 *   it. 2 cores (one from each guest) execute this test.
 * - Subtest 3_2 tests that a core can claim the error manager. All 4 cores
 *   claim the error manager. One should succeed. All perform a small stress
 *   test claiming the error manager for 6 times in a row. Only once the hcall
 *   should succeed.
 * - All 4 cores (2 from master and 2 from slave) will perform the illegal DMA
 *   test. One core is error manager and two cores have DMA channels.
 * Results validation:
 * - The not-registered cores should not be allowed to claim the error manager.
 * - The core owning the error manager must receive 2 critical interrupts since
 *   there are two cores doing illegal DMA accesses. It is noted in the comments
 *   that precise allignment of these accesses may lead to one exception being
 *   lost in the interrupt controller, and a delay was added to one guest.
 * Not tested (outside of test scope):
 * - The boot core of each slave guest having DMA must receive 1 machine check.
 */
static int test_claim_error_manager(register_t pir)
{
	int ret, result = 0;
	/* is this the initial error manager guest */
	bool is_err_owner = is_active_error_manager(1);

	if (!dma_virt) {
		/* This partition does not have DMA, must be the master */
		assert(slave_part_handle[0] && slave_part_handle[1]);

		/* Whichever core is first */
		ret = fh_partition_start(slave_part_handle[1], 0);
		if (!ret) {/* This colision between cores is expected */
			printf("[%ld] Started partition %d\n", pir,
			       slave_part_handle[1]);
		}
	}

	/* CPUs of the current error manager stop themselves */
	if (is_error_manager)
		end_test_and_print(pir);

	test_barrier(0, 4); /* 4 concurrent CPUs from 2 guests */

	/* At this point, there should not be any error owner, as the previous
	 * error manager partition is stopped */
	if (error_manager_cpu != pir) {
		/* Try any guest with GCPU != that from dts, expect to fail */
		ret = fh_claim_device(error_manager_handle);
		if (ret != EV_EPERM) {
			printf("[%ld] Test 3_1: FAILED with return code %d\n",
			       pir, ret);
			result += TEST_FAILED;
		} else {
			printf("[%ld] Test 3_1: PASSED\n", pir);
			result += TEST_PASSED;
		}
	}

	/* Never mind the function that parses the FDT, it is out of sync */
	is_error_manager = 0;
	is_err_owner = 0;
	/* Sync all cores to reset the is_ variables */
	test_barrier(1, 4); /* 4 concurrent CPUs from 2 guests */

	/* 4 CPUs to claim the error manager */
	for (int i = 0; i < 6; i++) {
		ret = fh_claim_device(error_manager_handle);
		if (!ret) {
			is_error_manager = 1; /* Guest global var */
			is_err_owner = 1; /* CPU local var */
			printf("[%ld] Claimed error manager\n", pir);
		}
	}

	test_barrier(0, 4); /* 4 concurrent CPUs from 2 guests */

	/* Do illegal DMA to check that the new error manager is functional */
	if (is_error_manager) {
		/* TODO: investigate if the real problem is the loss of
		 * PAMU illegal interrupts in case of simultaneous causes.
		 */
		delay_timebase(500000);
	}
	test_common_do_dma(pir, is_err_owner);

	/* Two guests, each doing illegal DMA */
	if ((is_err_owner && crit_cnt[pir] == old_crit_cnt[pir] + 2) ||
				(!is_err_owner &&
				crit_cnt[pir] == old_crit_cnt[pir])) {
		printf("[%ld] Test 3_2: PASSED\n", pir);
		result += TEST_PASSED;
	} else {
		printf("[%ld] Test 3_2: FAILED\n", pir);
		result += TEST_FAILED;
	}

	return result;
}

static void end_test_and_print(register_t pir)
{
	atomic_add(&cpulive_cnt, -1);

	if (!pir) { /* boot CPU ends last */
		printf("# Waiting for %ld CPUs\n", cpulive_cnt);
		while (atomic_add(&cpulive_cnt, 0)); /* wait for all cores */
	} else {
		while(1);
	}

	print_test_summary();
	fh_partition_stop(-1);
}

static void print_test_summary(void)
{
	uint8_t passed, failed, invalid;
	int cnt;

	puts("============");
	puts("Test summary");
	for (cnt = 0; cnt < MAXCPU; cnt++) {
		if (test_stat[cnt] == 0)
			continue;
		passed  = (test_stat[cnt]      ) & 0xff;
		invalid = (test_stat[cnt] >>  8) & 0xff;
		failed  = (test_stat[cnt] >> 16) & 0xff;
		printf("[%d] Tests run    : % 3d\n", cnt, passed + invalid + failed);
		printf("[%d] Tests passed : % 3d\n", cnt, passed);
		printf("[%d] Tests failed : % 3d\n", cnt, failed);
		printf("[%d] Tests invalid: % 3d\n", cnt, invalid);
	}
	puts("Test Complete");
}
