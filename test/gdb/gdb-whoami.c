
/*
 * Copyright (C) 2009, 2010 Freescale Semiconductor, Inc.
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
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/bitops.h>
#include <libfdt.h>
#include <hvtest.h>

#define PAGE_SIZE 4096

void init(unsigned long devtree_ptr);

extern void *fdt;

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
static volatile int cpus_complete[CPUCNT];

static void dump(int *a, int n, int pir, int i) __attribute__ ((noinline));
static void dump(int *a, int n, int pir, int i)
{
	int j;
	printf("PIR: %d, iteration: %d\n", pir, i);
	for (j = 0; j < n; j++)
		printf("a[%d] = %d ", j, a[j]);
	printf("\n");
}

static void gorp (void) __attribute__ ((noinline));

static void gorp(void)
{
	int i;
	int a[64];
	for (i = 0; i < 64; i++) {
		a[i] = i;
	}
	return;
}

static volatile int gdb_attached[CPUCNT];
static volatile int a[CPUCNT];
static volatile int X = 9;
static volatile int Y[] = {1, 2, 4, 8, 16, 32, 64, 128};

static void debug_code (int pir) __attribute__ ((noinline));

static void debug_code (int pir)
{
	int j, t;

	while (1) {
		X = 0;
		Y[0] = 0;
		Y[1] = 1;
		Y[2] = 2;
		Y[3] = 3;
		Y[4] = 4;
		Y[5] = 5;
		Y[6] = 6;
		Y[7] = 7;
		for (j = 0; j < 4; j++) {
			t = Y[2*j];
			Y[2*j] = Y[2*j+1];
			Y[2*j+1] = t;
		}
		gorp();
	}
}

static void secondary_entry(void)
{
	uint32_t cpu_index;
	uint32_t pir = mfspr(SPR_PIR);

	printf(" > secondary cpu start-- PIR=%d\n", pir);
	cpus_complete[pir] = 1;

	/* wait for debugger to set gdb_attached
	 * and allow code to continue */
	while (!gdb_attached[pir]);

	debug_code(pir);
}

extern void (*secondary_startp)(void);

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

	printf("gdb-C test\n");
	release_secondary_cores();
	printf(" > boot cpu start-- PIR=%d\n", pir);
	cpus_complete[pir] = 1;

	while (done != CPUCNT) {
		done = 0;
		for (i=0; i < CPUCNT; i++) {
			done += cpus_complete[i];
		}
	}

	printf("Test Complete\n");

	/* wait for debugger to set gdb_attached
	 * and allow code to continue */
	while (!gdb_attached[pir]);

	debug_code(pir);
}
