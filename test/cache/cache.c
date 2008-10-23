
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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
#include <libfdt.h>

extern void init(unsigned long devtree_ptr);

int irq;
extern void *fdt;

void start(unsigned long devtree_ptr)
{
	uint32_t ct = 0;
	uint32_t status;
	char str[16] = "cache lock test";
	char buf[16];
	int guest_cache_lock_mode = 0, ret, len;
	const uint32_t *prop;

	printf("cache lock test\n");

	init(devtree_ptr);

	ret = fdt_subnode_offset(fdt, 0, "hypervisor");
	if (ret != -FDT_ERR_NOTFOUND) {
		prop = fdt_getprop(fdt, ret, "fsl,hv-guest-cache-lock", &len);
		if (prop)
			guest_cache_lock_mode = 1;
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
		else
			printf("PASSED\n");
	}

	memcpy(buf, str, 16);

	printf("Test Complete\n");
}
