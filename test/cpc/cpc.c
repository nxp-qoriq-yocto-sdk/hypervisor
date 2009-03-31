/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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
#include <libos/hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);

extern void *fdt;

/* insert simics markpoints */
static inline void markpoint(const uint32_t markid)
{
        asm volatile(
                "and %0, %0, %0"
                : :     "i"     (markid)
                :       "memory"
        );
}

void init_buf(char *buf)
{
	int i;

	for(i = 0; i < 256 * 1024; i++)
		buf[i] = i;
}

void read_buf(char *arr)
{
	volatile uint32_t ch = 0;
	int i;

	if (!arr)
		return;
	for(i = 0; i < 256 * 1024; i++)
		ch += arr[i];
}

void cpc_perf_test(void)
{
	void *arr, *arr1;

	arr = valloc(256 * 1024, 256 * 1024);
	if (!arr) {
		printf("valloc failed \n");
		return;
	}

	tlb1_set_entry(1, (unsigned long)arr, (phys_addr_t)0x16000000, TLB_TSIZE_256K, 0, TLB_MAS3_KERN, 0, 0, 0);

	init_buf(arr);

	arr1 = valloc(256 * 1024, 256 * 1024);
	if (!arr1) {
		printf("valloc failed \n");
		return;
	}

	tlb1_set_entry(2, (unsigned long)arr1, (phys_addr_t)0x26000000, TLB_TSIZE_256K, 0, TLB_MAS3_KERN, 0, 0, 0);
	init_buf(arr1);
	/* Following function call ensures that the function is in instruction cache */
	read_buf(NULL);

	markpoint(0);
	read_buf(arr);
	markpoint(1);
	read_buf(arr1);
	markpoint(2);
}

void start(unsigned long devtree_ptr)
{
	init(devtree_ptr);

	printf("cpc test\n");
	cpc_perf_test();

	printf("Test Complete\n");
	markpoint(3);
}
