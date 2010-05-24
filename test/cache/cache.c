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
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>
#include <hvtest.h>

static volatile char *arr;

/* Eviction buffer, to evict L1 cache. Our L1 cache implements
 * seperate 32K 8 way set associative instruction and data cache.
 * Here we are evicting the data cache. As the L1 is a PLRU cache
 * we would have calcualte the ways per set required for eviction
 * This is given by the following formula :
 * evict(PLRU)(k) = 2k - sqroot(2K): if k = 2(pow (2i + 1))
 *                or 2k - 3/2 sqroot(k) : otherwise
 * As ours is a 32K 8 way set associative cache data cache
 * k = 8, so evict(PLRU)(k) = 2*(8) - sqroot(2 * 8)
 *                          = 16 - 4 = 12
 * so, cache size fo/ evictio
 = numsets * num ways per set * cache block size
 *                             = 64 * 12 * 64
 *                             = 48K
 * The above formula has been obtained from the following research paper :
 * Predictability of Cache Replacement Policies by Jan Reineke, Daniel Grund,
 * Christoph Berg and Reinhard Wilhelm
 */
static volatile char evict_buf[48 * 1024];

static void evict_cache(void)
{
	unsigned int i;

	for(i = 0; i < sizeof(evict_buf); i++)
		evict_buf[i] = i;
}

static int get_diff(void)
{
	volatile register_t val;
	int diff;

	val = mfspr(SPR_ATBL);
	/*Following add operations create a data dependency on load*/
	arr[0] = arr[0] + arr[100];
	arr[1] = arr [1] + arr[200];
	diff  = mfspr(SPR_ATBL) - val;

	return diff;
}

static void cache_lock_perf_test(void)
{
	uint32_t ct = 0;
	int diff, diff1, i;

	arr = valloc(4096, 4096);
	if (!arr) {
		printf("valloc failed \n");
		return;
	}

	tlb1_set_entry(1, (unsigned long)arr, (phys_addr_t)0x1600000, TLB_TSIZE_4K, 0, TLB_MAS3_KERN, 0, 0, 0);

	for (i = 0; i< 4096; i += 64) {
		asm volatile("dcbtls %0, 0, %1" : : "i" (ct),"r" (&arr[i]): "memory");
		if(mfspr(SPR_L1CSR0) & L1CSR0_DCUL) {
			mtspr(SPR_L1CSR0, mfspr(SPR_L1CSR0) & ~L1CSR0_DCUL);
			printf("Unable to lock cache line -- FAILED\n");
			return;
		}
	}

	for(i = 0; i < 4096; i++)
		arr[i] = i + 10;

	/* Following function call ensures that the function is in instruction cache */
	i = get_diff();

	evict_cache();

	diff = get_diff();

	for (i = 0; i< 4096; i += 64)
		asm volatile("dcblc %0, 0, %1" : : "i" (ct),"r" (&arr[i]): "memory");

	evict_cache();

	diff1  = get_diff();

	printf("Cache locking performance test -- %s\n", (diff1 > diff)? "PASSED" : "FAILED");
}

void libos_client_entry(unsigned long devtree_ptr)
{
	uint32_t ct = 0;
	uint32_t status;
	char str[16] = "cache lock test";
	int guest_cache_lock_mode = 1, ret, len;
	const uint32_t *prop;

	printf("cache lock test\n");

	init(devtree_ptr);
	ret = fdt_subnode_offset(fdt, 0, "hypervisor");
	if (ret != -FDT_ERR_NOTFOUND) {
		prop = fdt_getprop(fdt, ret, "fsl,hv-guest-cache-lock-disable", &len);
		if (prop)
			guest_cache_lock_mode = 0;
	}
	printf("guest cache lock mode: %d\n",guest_cache_lock_mode);

	asm volatile("dcbtls %0, 0, %1" : : "i" (ct),"r" (&str): "memory");
	status = mfspr(SPR_L1CSR0);
#ifdef DEBUG
	printf("cache lock result = %x\n", status);
#endif
	if (status & L1CSR0_DCUL) {
		mtspr(SPR_L1CSR0, status & ~L1CSR0_DCUL);
		printf(" > did dcbtls, failed: ");
		if (!guest_cache_lock_mode)
			printf("PASSED\n");
		else
			printf("FAILED\n");
	} else {
		printf("> did dcbtls, success: ");
		if (!guest_cache_lock_mode)
			printf("FAILED\n");
		else {
			printf("PASSED\n");
			asm volatile("dcblc %0, 0, %1" : : "i" (ct),"r" (&str): "memory");
			cache_lock_perf_test();
		}
	}

	printf("Test Complete\n");
}
