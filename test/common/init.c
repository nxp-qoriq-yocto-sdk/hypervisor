
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
#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/io.h>
#include <libos/ns16550.h>
#include <libos-client.h>
#include <libfdt.h>

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client = 0,
};

static void tlb1_init(void);
static void  core_init(void);

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M
#define UART_OFFSET 0x11d500

int get_uart_offset(void);

void *fdt;
int coreint;

/* FIXME: do proper reg/ranges parsing, and don't assume it's in CCSR */
int get_uart_offset()
{
	int ret;
	const uint32_t *prop;
	const char *path;

	ret = fdt_subnode_offset(fdt, 0, "aliases");
	if (ret < 0)
		return ret;

	path = fdt_getprop(fdt, ret, "stdout", &ret);
	if (!path)
		return ret;

	ret = fdt_path_offset(fdt, path);

	prop = fdt_getprop(fdt, ret, "reg", &ret);
	if (!prop)
		return 0;

	return prop[1] - CCSRBAR_PA;
}

void init(unsigned long devtree_ptr)
{
	int node, len;
	const uint32_t *prop;
	core_init();

	/* alloc the heap */
	fdt = (void *)(devtree_ptr + PHYSBASE);

	uintptr_t heap = (unsigned long)fdt + fdt_totalsize(fdt);
	heap = (heap + 15) & ~15;

	simple_alloc_init((void *)heap, 0x100000); // FIXME: hardcoded 1MB heap
	valloc_init(1024 * 1024, PHYSBASE);
	node = get_uart_offset();

	if (node >= 0)
		console_init(ns16550_init((uint8_t *)CCSRBAR_VA + node, 0, 0, 16));

	node = fdt_subnode_offset(fdt, 0, "hypervisor");
	if (node >= 0) {
		prop = fdt_getprop(fdt, node, "fsl,hv-pic-legacy", &len);
		if (prop)
			coreint = 0;
		else
			coreint = 1;
	}

	/* FIXME: tmp hack because the hypervisor patch to support
	 * the "legacy-interrupts" property is not applied.
	 */
	coreint = 0;

}

static void core_init(void)
{
	/* set up a TLB entry for CCSR space */
	tlb1_init();
}

void (*secondary_startp)(void) = NULL;

void secondary_init(void)
{
	core_init();
	if (secondary_startp) {
		secondary_startp();
	}
}


/*
 *    after tlb1_init:
 *        TLB1[0]  = CCSR
 *        TLB1[15] = OS image 16M
 */



static void tlb1_init(void)
{
	tlb1_set_entry(0, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, TLB_MAS2_IO,
		TLB_MAS3_KERN, 0, 0, 0);
}

__attribute__((weak)) void dec_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void ext_int_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void mcheck_interrupt(trapframe_t *frameptr)
{
}

__attribute__((weak)) void debug_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void fit_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void ext_doorbell_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void ext_critical_doorbell_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void dtlb_handler(trapframe_t *frameptr)
{
}

__attribute__((weak)) void watchdog_handler(trapframe_t *frameptr)
{
}

#define PAGE_SIZE 4096

extern int start_secondary_spin_table(struct boot_spin_table *table, int num,
				      cpu_t *cpu);

void release_secondary_cores(void)
{
	int node = fdt_subnode_offset(fdt, 0, "cpus");
	int depth = 0;
	void *map = valloc(PAGE_SIZE, PAGE_SIZE);

	if (node < 0) {
		printf("BROKEN: Missing /cpus node\n");
		goto fail;
	}

	while ((node = fdt_next_node(fdt, node, &depth)) >= 0) {
		int len;
		const char *status;

		if (node < 0)
			break;
		if (depth > 1)
			continue;
		if (depth < 1)
			return;

		status = fdt_getprop(fdt, node, "status", &len);
		if (!status) {
			if (len == -FDT_ERR_NOTFOUND)
				continue;

			node = len;
			goto fail_one;
		}

		if (len != strlen("disabled") + 1 || strcmp(status, "disabled"))
			continue;

		const char *enable =
		    fdt_getprop(fdt, node, "enable-method", &len);
		if (!status) {
			printf("BROKEN: Missing enable-method on disabled cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != strlen("spin-table") + 1
		    || strcmp(enable, "spin-table")) {
			printf("BROKEN: Unknown enable-method \"%s\"; not enabling\n",
			       enable);
			continue;
		}

		const uint32_t *reg = fdt_getprop(fdt, node, "reg", &len);
		if (!reg) {
			printf("BROKEN: Missing reg property in cpu node\n");
			node = len;
			goto fail_one;
		}

		if (len != 4) {
			printf("BROKEN: Bad length %d for cpu reg property; core not released\n",
			       len);
			return;
		}

		const uint64_t *table =
		    fdt_getprop(fdt, node, "cpu-release-addr", &len);
		if (!table) {
			printf("BROKEN: Missing cpu-release-addr property in cpu node\n");
			node = len;
			goto fail_one;
		}

		tlb1_set_entry(1, (unsigned long)map,
			       (*table) & ~(PAGE_SIZE - 1),
			       TLB_TSIZE_4K, TLB_MAS2_IO,
			       TLB_MAS3_KERN, 0, 0, 0);

		char *table_va = map;
		table_va += *table & (PAGE_SIZE - 1);
		cpu_t *cpu = alloc_type(cpu_t);
		if (!cpu)
			goto nomem;

		cpu->kstack = alloc(KSTACK_SIZE, 16);
		if (!cpu->kstack)
			goto nomem;

		cpu->kstack += KSTACK_SIZE - FRAMELEN;

		if (start_secondary_spin_table((void *)table_va, *reg, cpu))
			printf("BROKEN: couldn't spin up CPU%u\n", *reg);

next_core:
		;
	}

fail:
	printf("BROKEN: error %d (%s) reading CPU nodes, "
	       "secondary cores may not be released.\n",
	       node, fdt_strerror(node));

	return;

nomem:
	printf("BROKEN: out of memory reading CPU nodes, "
	       "secondary cores may not be released.\n");

	return;

fail_one:
	printf("BROKEN: error %d (%s) reading CPU node, "
	       "this core may not be released.\n", node, fdt_strerror(node));

	goto next_core;
}
