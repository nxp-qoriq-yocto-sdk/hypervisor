
/** @file
 * Initialization
 */
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
#include <libos/thread.h>
#include <libos/trap_booke.h>

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
#include <ccm.h>
#include <doorbell.h>

#include <limits.h>

extern cpu_t cpu0;

static gcpu_t noguest[MAX_CORES] = {
	{
		.cpu = &cpu0,   /* link back to cpu */
	}
};

extern uint8_t init_stack_top;

cpu_t cpu0 = {
	.kstack = &init_stack_top - FRAMELEN,
	.client.gcpu = &noguest[0],
};

cpu_t secondary_cpus[MAX_CORES - 1];
uint8_t secondary_stacks[MAX_CORES - 1][KSTACK_SIZE];

static void core_init(void);
static void release_secondary_cores(void);
static void partition_init(void);
void init_guest(void);

#define UART_OFFSET		0x11c500

dt_node_t *hw_devtree, *config_tree;
uint32_t hw_devtree_lock;
void *temp_mapping[2];
extern char _end;
uint64_t text_phys, bigmap_phys;
thread_t idle_thread[MAX_CORES];

static void exclude_phys(phys_addr_t addr, phys_addr_t end)
{
	if (addr < bigmap_phys)
		addr = bigmap_phys;

	if (end < addr)
		return;

	addr += BIGPHYSBASE - bigmap_phys;
	end += BIGPHYSBASE - bigmap_phys;

	if (addr > 0x100000000ULL)
		return;

	if (end >= 0x100000000ULL || end < addr)
		end = 0xffffffffUL;

	malloc_exclude_segment((void *)(unsigned long)addr,
	                       (void *)(unsigned long)end);
}

static void exclude_memrsv(void *fdt)
{
	int i, num = fdt_num_mem_rsv(fdt);
	if (num < 0) {
		printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
		         "exclude_memrsv: error %d getting number of entries\n", num);
		return;
	}

	for (i = 0; i < num; i++) {
		uint64_t addr, size, end;
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

		if (size == 0)
			continue;

		end = addr + size - 1;

		if (end < addr) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "rsvmap %d contains invalid region 0x%llx->0x%llx\n",
			         i, addr, addr + size - 1);

			continue;
		}

		exclude_phys(addr, end);
	}
}

static int mpic_probe(driver_t *drv, device_t *dev);

static driver_t __driver mpic_driver = {
	.compatible = "chrp,open-pic",
	.probe = mpic_probe
};

static int mpic_probe(driver_t *drv, device_t *dev)
{
	int coreint = 1;
	dt_node_t *node;

	node = dt_get_first_compatible(config_tree, "hv-config");
	if (node && dt_get_prop(node, "legacy-interrupts", 0))
		coreint = 0;

	mpic_init(coreint);

	dev->driver = &mpic_driver;
	dev->irqctrl = &mpic_ops;
	return 0;
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

		/* We don't handle flat trees larger than 4MiB. */
		assert (len >= sizeof(struct fdt_header));
		assert (len >= fdt_totalsize(vaddr));
	}

	return vaddr;
}

void unmap_fdt(void)
{
	/* We need to unmap the FDT, since we use temp_mapping[0] in
	 * TEMPTLB2, which could otherwise cause a duplicate TLB entry.
	 */
	tlb1_clear_entry(TEMPTLB1);
	tlb1_clear_entry(TEMPTLB2);
}

extern queue_t early_console;

static int get_cfg_addr(phys_addr_t devtree_ptr, phys_addr_t *cfg_addr)
{
	const void *fdt;
	const char *str;
	int offset, len;
	
	fdt = map_fdt(devtree_ptr);

	offset = fdt_subnode_offset(fdt, 0, "chosen");
	if (offset < 0) {
		printf("Cannot find /chosen, error %d\n", offset);
		return offset;
	}

	str = fdt_getprop(fdt, offset, "bootargs", &len);
	if (!str) {
		printf("Cannot find bootargs in /chosen, error %d\n", len);
		return len;
	}

	str = strstr(str, "config-addr=");
	if (!str) {
		printf("config-addr not specified in bootargs\n");
		return ERR_NOTFOUND;
	}

	str += strlen("config-addr=");

	*cfg_addr = get_number64(&early_console, str);
	if (cpu->errno) 
		return cpu->errno;

	unmap_fdt();
	return 0;
}

static const unsigned long hv_text_size = 1024 * 1024;
static int reloc_hv = 1;

static void add_memory(phys_addr_t start, phys_addr_t size)
{
	phys_addr_t vstart = start - bigmap_phys + BIGPHYSBASE;

	if (start + size < start)
		return;

	if (vstart > 0x100000000ULL)
		return;

	if (vstart + size > 0x100000000UL ||
	    vstart + size < vstart) {
		size = 0x100000000ULL - vstart;

		/* Round down to power-of-two */
		size = 1UL << ilog2(size);
	}

	int ret = map_hv_pma(start, size, 0);
	if (ret < 0) {
		printf("%s: error %d mapping PMA %#llx to %#llx\n",
		       __func__, ret, start, size);
		return;
	}

	/* We don't need to relocate the hypervisor if we are granted
	 * the original boot mapping.
	 */
	if (start == 0 && size >= hv_text_size)
		reloc_hv = 0;

	malloc_add_segment((void *)(unsigned long)vstart,
	                   (void *)(unsigned long)(vstart + size - 1));
}

/* On first iteration, call with add == 0 to return
 * the lowest address referenced.
 *
 * On the second iteration, call with add == 1 to add the memory regions.
 */
static void process_hv_mem(void *fdt, int offset, int add)
{
	const uint32_t *prop;
	uint64_t addr, size;
	int pma, len;
	int depth = 0;
	
	if (!add)
		bigmap_phys = ~0ULL;

	while (1) {
		offset = fdt_next_descendant_by_compatible(fdt, offset,
		                                           &depth, "hv-memory");
		if (offset < 0) {
			if (offset != -FDT_ERR_NOTFOUND)
				printf("%s: error %d finding hv-memory\n", __func__, offset);

			break;
		}

		prop = fdt_getprop(fdt, offset, "phys-mem", &len);
		if (prop < 0 || len != 4) {
			printf("%s: error %d getting phys-mem\n", __func__, len);
			continue;
		}

		pma = fdt_node_offset_by_phandle(fdt, *prop);
		if (pma < 0) {
			printf("%s: error %d looking up phys-mem\n", __func__, pma);
			continue;
		}

		prop = fdt_getprop(fdt, pma, "addr", &len);
		if (prop < 0 || len != 8) {
			printf("%s: error %d getting pma addr\n", __func__, len);
			continue;
		}
		addr = (((uint64_t)prop[0]) << 32) | prop[1];

		prop = fdt_getprop(fdt, pma, "size", &len);
		if (prop < 0 || len != 8) {
			printf("%s: error %d getting pma size\n", __func__, len);
			continue;
		}
		size = (((uint64_t)prop[0]) << 32) | prop[1];

		if (size & (size - 1)) {
			printf("%s: pma size %lld not a power of 2\n", __func__, size);
			continue;
		}

		if (addr & (size - 1)) {
			printf("%s: pma addr %llx not aligned to size %llx\n",
			       __func__, addr, size);
			continue;
		}

		if (add)
			add_memory(addr, size);
		else if (addr < bigmap_phys)
			bigmap_phys = addr & ~(BIGPHYSBASE - 1);
	}
}

/* Note that we do not verify that a PMA is covered by a hw memory node,
 * or that guest PMAs are not covered by memreserve areas.
 */
static int init_hv_mem(phys_addr_t devtree_ptr, phys_addr_t cfg_addr)
{
	void *fdt;
	int offset;
	int depth = 0;

	fdt = map_fdt(cfg_addr);
	
	offset = fdt_next_descendant_by_compatible(fdt, 0, &depth, "hv-config");
	if (offset < 0) {
		printf("%s: config tree has no hv-config, err %d\n",
		       __func__, offset);
		return offset;
	}

	process_hv_mem(fdt, offset, 0);
	process_hv_mem(fdt, offset, 1);
	
	unmap_fdt();

	fdt = map_fdt(devtree_ptr);

	exclude_phys(0, (uintptr_t)&_end - 1);
	exclude_phys(devtree_ptr, devtree_ptr + fdt_totalsize(fdt) - 1);
	exclude_memrsv(fdt);

	unmap_fdt();

	malloc_init();

	if (reloc_hv) {
		void *new_text = memalign(hv_text_size, hv_text_size);
		if (!new_text) {
			printf("Cannot relocate hypervisor\n");
			return ERR_NOMEM;
		}

		text_phys = virt_to_phys(new_text);
		barrier();
		memcpy(new_text, (void *)PHYSBASE, (uintptr_t)&_end - PHYSBASE);
		branch_to_reloc(new_text, (text_phys & MAS3_RPN) | TLB_MAS3_KERN,
		                text_phys >> 32);
	}

	return 0;
}

static dt_node_t *stdout_node;
static int stdout_is_device;

static void find_hv_console(dt_node_t *hvconfig)
{
	dt_prop_t *prop = dt_get_prop(hvconfig, "stdout", 0);
	if (prop) {
		if (prop->len == 4)
			stdout_node = dt_lookup_phandle(config_tree,
			                                *(const uint32_t *)prop->data);

		if (stdout_node) {
			const char *dev = dt_get_prop_string(stdout_node, "device");
			if (dev) {
				stdout_node = dt_lookup_alias(hw_devtree, dev);
				stdout_is_device = 1;
			}
		} else {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: bad stdout phandle\n", __func__);
		}
	}

	if (!stdout_node) {
		stdout_node = dt_lookup_alias(hw_devtree, "stdout");
		stdout_is_device = 1;
	}

	if (stdout_is_device)
		open_stdout_chardev(stdout_node);
}

static void assign_hv_devs(void)
{
	dt_node_t *node = dt_get_first_compatible(config_tree, "hv-config");

	dt_read_aliases();
	dt_assign_devices(node, NULL);
	find_hv_console(node);

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);

		dt_lookup_regs(owner->hwnode);
	}

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);

		dt_lookup_irqs(owner->hwnode);
	}

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);

		dt_bind_driver(owner->hwnode);
	}
}

void start(unsigned long devtree_ptr)
{
	phys_addr_t cfg_addr = 0;
	void *fdt;
	int ret;

	cpu->thread = &idle_thread[0];

	printf("=======================================\n");
	printf("Freescale Hypervisor %s\n", CONFIG_HV_VERSION);

	cpu->client.next_dyn_tlbe = DYN_TLB_START;

	valloc_init(VMAPBASE, BIGPHYSBASE);
	temp_mapping[0] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[1] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);

	ret = get_cfg_addr(devtree_ptr, &cfg_addr);
	if (ret < 0)
		return;

	ret = init_hv_mem(devtree_ptr, cfg_addr);
	if (ret < 0)
		return;

	CCSRBAR_VA = (unsigned long)map(CCSRBAR_PA, 16 * 1024 * 1024,
	                                TLB_MAS2_IO, TLB_MAS3_KERN);
	
	cpu->console_ok = 1;
	core_init();

	fdt = map_fdt(devtree_ptr);
	hw_devtree = unflatten_dev_tree(fdt);
	if (!hw_devtree) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
		         "panic: couldn't unflatten hardware device tree.\n");
		return;
	}

	unmap_fdt();

	fdt = map_fdt(cfg_addr);
	config_tree = unflatten_dev_tree(fdt);
	if (!config_tree) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
		         "panic: couldn't unflatten config device tree.\n");
		return;
	}

	unmap_fdt();

	get_addr_format_nozero(hw_devtree, &rootnaddr, &rootnsize);

	assign_hv_devs(); 
	enable_int();

	ccm_init();

#ifdef CONFIG_BYTE_CHAN
#ifdef CONFIG_BCMUX
	create_muxes();
#endif

	open_stdout_bytechan(stdout_node);
#endif

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
	secondary_map_mem();
	cpu->console_ok = 1;
	
	core_init();
	mpic_reset_core();
	enable_int();
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

	if (reg > MAX_CORES) {
		printlog(LOGTYPE_MP, LOGLEVEL_NORMAL,
		         "%s: Ignoring core %d, max cores %d\n",
		         __func__, reg, MAX_CORES);
	}

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

	/* Per-cpu data must be in the text mapping, or else secondaries
	 * won't have it mapped.
	 */
	cpu_t *newcpu = &secondary_cpus[reg - 1];
	newcpu->kstack = secondary_stacks[reg - 1] + KSTACK_SIZE - FRAMELEN;
	newcpu->client.gcpu = &noguest[reg];
	newcpu->client.gcpu->cpu = newcpu;  /* link back to cpu */
	newcpu->thread = &idle_thread[reg];

	/* Terminate the callback chain. */
	newcpu->kstack[KSTACK_SIZE - FRAMELEN] = 0;

	if (start_secondary_spin_table(table_va, reg, newcpu))
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR, "couldn't spin up CPU%u\n", reg);

	return 0;
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
	mtspr(SPR_EPCR,
	      EPCR_EXTGS | EPCR_DSIGS |
	      EPCR_DUVD | EPCR_DGTMI | EPCR_DMIUH);

	tlbcache_init();
#else
	mtspr(SPR_EPCR,
	      EPCR_EXTGS | EPCR_DTLBGS | EPCR_ITLBGS |
	      EPCR_DSIGS | EPCR_DUVD | EPCR_DGTMI);
#endif
}

void new_thread_inplace(thread_t *thread, uint8_t *kstack,
                        void (*func)(trapframe_t *regs, void *arg), void *arg)
{
	cpu->kstack = &kstack[KSTACK_SIZE - FRAMELEN];
	trapframe_t *regs = (trapframe_t *)cpu->kstack;

	thread->pc = ret_from_exception;
	thread->stack = regs;

	regs->gpregs[1] = (register_t)regs;
	regs->gpregs[2] = (register_t)cpu;
	regs->gpregs[3] = (register_t)regs;
	regs->gpregs[4] = (register_t)arg;
	
	regs->srr0 = (register_t)func;
	regs->srr1 = MSR_ME | MSR_CE | MSR_EE;

	regs->lr = (register_t)ret_from_exception;
}

thread_t *new_thread(void (*func)(trapframe_t *regs, void *arg), void *arg)
{
	thread_t *thread = malloc(sizeof(thread_t));
	if (!thread)
		return NULL;
	
	uint8_t *stack = alloc_type(kstack_t);
	if (!stack) {
		free(thread);
		return NULL;
	}

	new_thread_inplace(thread, stack, func, arg);
	return thread;
}

void yield(void)
{
	register_t saved = disable_int_save();

	if (cpu->ret_hook)
		send_doorbell(cpu->coreid);

	switch_thread(&idle_thread[cpu->coreid]);

	restore_int(saved);
}
