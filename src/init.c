
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
#include <libos/console.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/mpic.h>
#include <libos/ns16550.h>
#include <libos/interrupts.h>
 
#include <hv.h>
#include <percpu.h>
#include <paging.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <pamu.h>
#include <bcmux.h>
#include <devtree.h>
#include <ipi_doorbell.h>
#include <shell.h>

#include <limits.h>

extern cpu_t cpu0;

static gcpu_t noguest = {
	.cpu = &cpu0,   /* link back to cpu */
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest,
};

static void core_init(void);
static void release_secondary_cores(void);
static void partition_init(void);
void init_guest(void);

#define UART_OFFSET		0x11c500

dt_node_t *hw_devtree;
static phys_addr_t mem_end;
void *temp_mapping[2];
extern char _end;
uint64_t bigmap_phys;

static void exclude_memrsv(void *fdt)
{
	int i, num = fdt_num_mem_rsv(fdt);
	if (num < 0) {
		printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
		         "exclude_memrsv: error %d getting number of entries\n", num);
		return;
	}

	for (i = 0; i < num; i++) {
		uint64_t addr, size;
		int ret;
		
		ret = fdt_get_mem_rsv(fdt, i, &addr, &size);
		if (ret < 0) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "exclude_memrsv: error %d getting entry %d\n", ret, i);
			return;
		}

		printlog(LOGTYPE_MALLOC, LOGLEVEL_DEBUG,
		         "memreserve 0x%llx->0x%llx\n",
		         addr, addr + size - 1);

		if (addr + size < addr) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "rsvmap %d contains invalid region 0x%llx->0x%llx\n",
			         i, addr, addr + size - 1);

			return;
		}

		addr += PHYSBASE;

		if (addr > 0x100000000ULL)
			return;

		if (addr + size + PHYSBASE > 0x100000000ULL)
			size = 0x100000000ULL - addr;

		malloc_exclude_segment((void *)(unsigned long)addr,
		                       (void *)(unsigned long)(addr + size - 1));
	}
}

static void pic_init(void)
{
	int coreint = 0;
	dt_node_t *chosen;

	chosen = dt_get_subnode(hw_devtree, "chosen", 0);
	if (chosen && dt_get_prop(chosen, "fsl,hv-pic-coreint", 0))
		coreint = 1;

	printlog(LOGTYPE_IRQ, LOGLEVEL_NORMAL,
		"coreint mode is %d\n", coreint);
	mpic_init(coreint);
}

uint32_t rootnaddr, rootnsize;

void *map_fdt(phys_addr_t devtree_ptr)
{
	const size_t mapsize = 4 * 1024 * 1024;
	size_t len = mapsize;
	void *vaddr;

	/* Map guarded, as we don't know where the end of memory is yet. */
	vaddr = map_phys(TEMPTLB1, devtree_ptr & ~(mapsize - 1), temp_mapping[0],
	                 &len, TLB_MAS2_MEM | MAS2_G);

	vaddr += devtree_ptr & (mapsize - 1);

	/* If the fdt was near the end of a 4MiB page, then we need
	 * another mapping.
	 */
	if (len < sizeof(struct fdt_header) ||
	    len < fdt_totalsize(vaddr)) {
		devtree_ptr += len;
		len = mapsize;
		
		map_phys(TEMPTLB2, devtree_ptr, temp_mapping[0] + mapsize,
		         &len, TLB_MAS2_MEM | MAS2_G);

		printf("len %zd\n", len);

		/* We don't handle flat trees larger than 4MiB. */
		assert (len >= sizeof(struct fdt_header));
		assert (len >= fdt_totalsize(vaddr));
	}

	return vaddr;
}

void start(unsigned long devtree_ptr)
{
	phys_addr_t lowest_guest_addr;
	void *fdt;

	printf("=======================================\n");
	printf("Freescale Hypervisor %s\n", CONFIG_HV_VERSION);

	valloc_init(1024 * 1024, PHYSBASE);
	temp_mapping[0] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[1] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);

	fdt = map_fdt(devtree_ptr);

	mem_end = find_memory(fdt);
	lowest_guest_addr = find_lowest_guest_phys(fdt);
	if (mem_end > lowest_guest_addr - 1)
		mem_end = lowest_guest_addr - 1;

	exclude_memrsv(fdt);
	malloc_exclude_segment((void *)PHYSBASE, &_end - 1);
	malloc_exclude_segment((void *)PHYSBASE + devtree_ptr,
	                       (void *)PHYSBASE + devtree_ptr +
	                       fdt_totalsize(fdt) - 1);
	
	/* Only access memory in the boot mapping for now */
	malloc_exclude_segment((void *)(PHYSBASE + 256 * 1024 * 1024),
	                       (void *)ULONG_MAX);
	
	if (mem_end <= ULONG_MAX)
		malloc_exclude_segment((void *)(PHYSBASE + (uintptr_t)mem_end + 1),
		                       (void *)ULONG_MAX);
	malloc_init();
	tlb1_init();

	CCSRBAR_VA = (unsigned long)map(CCSRBAR_PA, 16 * 1024 * 1024,
	                                TLB_MAS2_IO, TLB_MAS3_KERN, 1);
	
	cpu->console_ok = 1;
	core_init();

	hw_devtree = unflatten_dev_tree(fdt);
	if (!hw_devtree) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
		         "panic: couldn't unflatten hardware device tree.\n");
		return;
	}

	/* We need to unmap the FDT, since we use temp_mapping[0] in
	 * TEMPTLB2, which could otherwise cause a duplicate TLB entry.
	 */
	tlb1_set_entry(TEMPTLB1, 0, 0, 0, 0, 0, 0, 0, 0);
	tlb1_set_entry(TEMPTLB2, 0, 0, 0, 0, 0, 0, 0, 0);

	get_addr_format_nozero(hw_devtree, &rootnaddr, &rootnsize);

	pic_init();
	enable_critint();

#ifdef CONFIG_LIBOS_NS16550
	create_ns16550();
#endif

#ifdef CONFIG_BYTE_CHAN
	create_byte_channels();
#ifdef CONFIG_BCMUX
	create_muxes();
#endif
	connect_global_byte_channels();
#endif

	open_stdout();

	vmpic_global_init();

#ifdef CONFIG_SHELL
	shell_init();
#endif

	create_doorbells();

#ifdef CONFIG_PAMU
	pamu_global_init();
#endif

	/* Main device tree must be const after this point. */
	release_secondary_cores();
	partition_init();
}

void secondary_init(void)
{
	tlb1_init();
	core_init();
	mpic_reset_core();
	enable_critint();
	partition_init();
}

static int release_secondary(dt_node_t *node, void *arg)
{
	dt_prop_t *prop;
	const char *str;
	uint32_t reg;
	phys_addr_t table;

	str = dt_get_prop_string(node, "status");
	if (!str || strcmp(str, "disabled") != 0)
		return 0;

	str = dt_get_prop_string(node, "enable-method");
	if (!str) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "release_secondary: Missing enable-method on disabled cpu node\n");
		return 0;
	}

	if (strcmp(str, "spin-table") != 0) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "release_secondary: Unknown enable-method \"%s\"; not enabling\n",
		         str);
		return 0;
	} 

	prop = dt_get_prop(node, "reg", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "release_secondary: Missing/bad reg property in cpu node\n");
		return 0;
	}
	reg = *(const uint32_t *)prop->data;

	prop = dt_get_prop(node, "cpu-release-addr", 0);
	if (!prop || prop->len != 8) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "release_secondary: Missing/bad cpu-release-addr\n");
		return 0;
	}
	table = *(const uint64_t *)prop->data;

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "starting cpu %u, table %llx\n", reg, (unsigned long long)table);

	size_t len = sizeof(struct boot_spin_table);
	void *table_va = map_phys(TEMPTLB1, table, temp_mapping[0],
	                          &len, TLB_MAS2_MEM);

	if (len != sizeof(struct boot_spin_table)) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "release_secondary: spin-table %llx spans page boundary\n",
		         (unsigned long long)table);
		return 0;
	}	

	cpu_t *cpu = alloc_type(cpu_t);
	if (!cpu)
		goto nomem;

	cpu->kstack = memalign(16, KSTACK_SIZE);
	if (!cpu->kstack)
		goto nomem;
	cpu->kstack += KSTACK_SIZE - FRAMELEN;

	cpu->client.gcpu = alloc_type(gcpu_t);
	if (!cpu->client.gcpu)
		goto nomem;
	cpu->client.gcpu->cpu = cpu;  /* link back to cpu */

	if (start_secondary_spin_table(table_va, reg, cpu))
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR, "couldn't spin up CPU%u\n", reg);

	return 0;

nomem:
	printlog(LOGTYPE_MP, LOGLEVEL_ERROR, "release_secondary: out of memory\n");
	return ERR_NOMEM;
}

static void release_secondary_cores(void)
{
	dt_for_each_prop_value(hw_devtree, "device_type", "cpu", 4,
	                       release_secondary, NULL);
}

static void partition_init(void)
{
	init_guest();
	BUG();
}

static void core_init(void)
{
	/* PIR was set by firmware-- record in cpu_t struct */
	cpu->coreid = mfspr(SPR_PIR);

	/* dec init sequence 
	 *  -disable DEC interrupts
	 *  -disable DEC auto reload
	 *  -set DEC to zero 
	 *  -clear any pending DEC interrupts */
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_DIE);
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_ARE);
	mtspr(SPR_DEC, 0x0);
	mtspr(SPR_TSR, TSR_DIS);


	mtspr(SPR_HID0, HID0_EMCP | HID0_DPM | HID0_ENMAS7);

#ifdef CONFIG_TLB_CACHE
	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DSIGS |
	      EHCSR_DUVD | EHCSR_DGTMI | EHCSR_DMIUH);

	tlbcache_init();
#else
	mtspr(SPR_EHCSR,
	      EHCSR_EXTGS | EHCSR_DTLBGS | EHCSR_ITLBGS |
	      EHCSR_DSIGS | EHCSR_DUVD | EHCSR_DGTMI);
#endif
}
