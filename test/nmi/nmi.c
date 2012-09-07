
/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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
#include <libos/epapr_hcalls.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/bitops.h>
#include <libos/fsl-booke-tlb.h>

#define MAX_CPUCNT 8
static volatile int cpus_complete[MAX_CPUCNT];
static volatile int cpus_ready[MAX_CPUCNT];

void init(unsigned long devtree_ptr);

void mcheck_interrupt(trapframe_t *frameptr)
{
	uint32_t pir = mfspr(SPR_PIR);

	if (mfspr(SPR_MCSR) & MCSR_NMI) {
		pir = mfspr(SPR_PIR);
		cpus_complete[pir] = 1;
		mtspr(SPR_MCSR, mfspr(SPR_MCSR));
	} else
		printf("Invalid Machine Check Interrupt, pir %d\n", pir);
}

int release_secondary_cores(void);
extern void (*secondary_startp)(void);


static void secondary_entry(void)
{
	uint32_t pir = mfspr(SPR_PIR);
	cpus_ready[pir] = 1;
	while (1);
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int ret;
	int vcpu_mask;
	int count = 100;
	int cpucnt;

	init(devtree_ptr);

	printf("NMI test\n");

	secondary_startp = secondary_entry;
	cpucnt = release_secondary_cores();

	/* wait for secondary cores to be ready */
	while (1) {
		int i;
		for (i = 1; i < cpucnt + 1; i++) {
			if (!cpus_ready[i])
				break;
			barrier();
		}
		if (i == (cpucnt + 1))
			break;
	}

	vcpu_mask = (1 << cpucnt) - 1;
	ret = fh_send_nmi(vcpu_mask);

	if (ret == EV_EINVAL) {
		printf("NMI Test: Invalid vcpu mask 0x%x\n", vcpu_mask);
		printf("NMI test Failed\n");
		return;
	}

	printf(" > vcpu mask 0x%x\n", vcpu_mask);
	if (!ret) {
		while (count--) {
			for (int i = 0; i < (cpucnt + 1); i++) {
				if (vcpu_mask & (1 << i))
					if (cpus_complete[i]) {
						printf(" > NMI Test pir %d:  PASSED\n", i);
						vcpu_mask &= ~(1 << i);
					}
			}

			if (!vcpu_mask)
				break;
		}

		if (vcpu_mask)
			printf("\nvcpu mask = %08x: FAILED\n", vcpu_mask);
	}
	printf("Test Complete\n");
}
