
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
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

#define PAGE_SIZE 4096

static unsigned long devtree;

void ext_int_handler(trapframe_t *frameptr)
{
}

static void dump_dev_tree(void)
{
#ifdef DEBUG
	int node = -1;
	const char *s;
	int len;

	printf("dev tree ------\n");
	while ((node = fdt_next_node(fdt, node, NULL)) >= 0) {
		s = fdt_get_name(fdt, node, &len);
		printf("   node = %s\n", s);
	}
	printf("------\n");
#endif
}

#define CPUCNT 4
static int cpus_complete[CPUCNT];

static void secondary_entry(void)
{
	uint32_t cpu_index;
	uint32_t pir = mfspr(SPR_PIR);

	fh_cpu_whoami(&cpu_index);
	printf(" > secondary cpu start-- PIR=%d, fh_whoami=%d: ", pir, cpu_index);
	if (cpu_index == pir) {
		cpus_complete[pir] = 1;
		printf("PASSED\n");
	} else {
		printf("FAILED\n");
	}
}

void libos_client_entry(unsigned long devtree_ptr)
{
	int i;
	int done = 0;
	uint32_t cpu_index;
	uint32_t pir = mfspr(SPR_PIR);

	devtree = devtree_ptr;

	secondary_startp = secondary_entry;

	init(devtree);

	dump_dev_tree();

	enable_extint();

	printf("whoami test\n");
	release_secondary_cores();
	fh_cpu_whoami(&cpu_index);
	printf(" > boot cpu start-- PIR=%d, fh_whoami=%d: ", pir, cpu_index);
	if (cpu_index == pir) {
		cpus_complete[pir] = 1;
		printf("PASSED\n");
	} else {
		printf("FAILED\n");
	}

	while (done != CPUCNT) {
		done = 0;
		for (i=0; i < CPUCNT; i++) {
			done += cpus_complete[i];
		}
	}

	printf("Test Complete\n");

}
