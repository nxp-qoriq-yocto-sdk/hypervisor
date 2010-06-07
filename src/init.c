/** @file
 * Initialization
 */

/*
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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

#include <limits.h>

#include <libos/console.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/uart.h>
#include <libos/mpic.h>
#include <libos/ns16550.h>
#include <libos/interrupts.h>
#include <libos/alloc.h>

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
#include <events.h>
#include <thread.h>
#include <error_log.h>
#include <error_mgmt.h>

queue_t hv_global_event_queue;
uint32_t hv_queue_prod_lock;
uint32_t hv_queue_cons_lock;

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
static uint8_t secondary_stacks[MAX_CORES - 1][KSTACK_SIZE];

static void core_init(void);
static void release_secondary_cores(void);
static void partition_init(void);

#define UART_OFFSET		0x11c500

dt_node_t *hw_devtree, *config_tree;
uint32_t hw_devtree_lock;
void *temp_mapping[2];
extern char _end, _start;
uint64_t text_phys, bigmap_phys;

char *displacement_flush_area[MAX_CORES];

int auto_sys_reset_on_stop;

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

static void *map_fdt(phys_addr_t devtree_ptr)
{
	const size_t mapsize = 4 * 1024 * 1024;
	size_t len = mapsize;
	void *vaddr;

	/* Map guarded, as we don't know where the end of memory is yet. */
	vaddr = map_phys(TEMPTLB1, devtree_ptr & ~((phys_addr_t)mapsize - 1),
	                 temp_mapping[0], &len, TLB_TSIZE_16M,
	                 TLB_MAS2_MEM | MAS2_G);

	vaddr += devtree_ptr & (mapsize - 1);

	/* If the fdt was near the end of a 4MiB page, then we need
	 * another mapping.
	 */
	if (len < sizeof(struct fdt_header) ||
	    len < fdt_totalsize(vaddr)) {
		devtree_ptr += len;
		len = mapsize;

		map_phys(TEMPTLB2, devtree_ptr, temp_mapping[0] + mapsize,
		         &len, TLB_TSIZE_16M, TLB_MAS2_MEM | MAS2_G);

		/* We don't handle flat trees larger than 4MiB. */
		assert (len >= sizeof(struct fdt_header));
		assert (len >= fdt_totalsize(vaddr));
	}

	return vaddr;
}

static void unmap_fdt(void)
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
	void *fdt;
	char *str, *numstr;
	int offset, len;

	fdt = map_fdt(devtree_ptr);

	offset = fdt_subnode_offset(fdt, 0, "chosen");
	if (offset < 0) {
		printf("Cannot find /chosen, error %d\n", offset);
		return offset;
	}

	str = fdt_getprop_w(fdt, offset, "bootargs", &len);
	if (!str || len == 0 || str[len - 1] != 0) {
		printf("Bad/missing bootargs in /chosen, error %d\n", len);
		return len;
	}

	printf("Hypervisor command line: %s\n", str);

	str = strstr(str, "config-addr=");
	if (!str) {
		printf("config-addr not specified in bootargs\n");
		return ERR_NOTFOUND;
	}

	str += strlen("config-addr=");
	numstr = nextword(&str);

	*cfg_addr = get_number64(&consolebuf, numstr);
	if (cpu->errno)
		return cpu->errno;

	unmap_fdt();
	return 0;
}

static const unsigned long hv_text_size = 4 * 1024 * 1024;
static int reloc_hv = 1;

static void add_memory(phys_addr_t start, phys_addr_t size)
{
	phys_addr_t vstart = start - bigmap_phys + BIGPHYSBASE;

	if (start + size < start)
		return;

	if (vstart > 0x100000000ULL)
		return;

	if (vstart + size > 0x100000000ULL ||
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
		if (!prop || len != 4) {
			printf("%s: error %d getting phys-mem\n", __func__, len);
			continue;
		}

		pma = fdt_node_offset_by_phandle(fdt, *prop);
		if (pma < 0) {
			printf("%s: error %d looking up phys-mem\n", __func__, pma);
			continue;
		}

		prop = fdt_getprop(fdt, pma, "addr", &len);
		if (!prop || len != 8) {
			printf("%s: error %d getting pma addr\n", __func__, len);
			continue;
		}
		addr = (((uint64_t)prop[0]) << 32) | prop[1];

		prop = fdt_getprop(fdt, pma, "size", &len);
		if (!prop || len != 8) {
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
			bigmap_phys = addr & ~((phys_addr_t)BIGPHYSBASE - 1);
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

	exclude_phys(0, &_end - &_start - 1);
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

static void early_bind_ccm_law_devices(void)
{
	dt_prop_t *prop;
	dt_node_t *node;
	const char *compat_srch_str[] = {"fsl,corenet-cf", "fsl,corenet-law"};

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);
		prop = dt_get_prop(owner->hwnode, "compatible", 0);
		if (!prop)
			continue;
		if (!strcmp((const char *)prop->data, compat_srch_str[0]) ||
			!strcmp((const char *)prop->data, compat_srch_str[1])) {

				dt_lookup_regs(owner->hwnode);
				dt_lookup_irqs(owner->hwnode);
				dt_bind_driver(owner->hwnode);
		}
	}
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

static int claim_hv_pma(dt_node_t *node, void *arg)
{
	dt_node_t *pma = get_pma_node(node);
	if (!pma) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: no pma node for %s\n", __func__, node->name);
		return 0;
	}

	dev_owner_t *owner = alloc_type(dev_owner_t);
	if (!owner) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return ERR_NOMEM;
	}

	owner->cfgnode = pma;

	spin_lock_int(&dt_owner_lock);
	list_add(&hv_devs, &owner->guest_node);
	list_add(&pma->owners, &owner->dev_node);
	spin_unlock_int(&dt_owner_lock);

	add_all_cpus_to_csd(pma);
	return 0;
}

static dt_node_t *hvconfig;

static void assign_hv_devs(void)
{
	dt_read_aliases();
	dt_assign_devices(hvconfig, NULL);
	find_hv_console(hvconfig);

	if (dt_get_prop(hvconfig, "sysreset-on-partition-stop", 0))
		auto_sys_reset_on_stop = 1;

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);

		set_error_policy(owner);
	}

	early_bind_ccm_law_devices();

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


/**
 * get_ccsr_phys_addr - get the CCSR physical address from the device tree
 * @param[out] size returned size of CCSR, in bytes
 *
 * This function looks up the CCSR base physical address from the device
 * tree.  If 'ccsr_size' is not NULL, then the size of CCSR space is
 * returned through it, if this function does not return 0.
 *
 * Returns the CCSR base physical address, or 0 if error.
 */
phys_addr_t get_ccsr_phys_addr(size_t *ccsr_size)
{
	unsigned int parent_addr_cells, parent_size_cells;
	unsigned int child_addr_cells, child_size_cells;
	dt_node_t *node;
	const dt_prop_t *prop;
	uint32_t addrbuf[MAX_ADDR_CELLS] = { 0 };
	phys_addr_t addr, size;
	int ret;

	// Find the CCSR node
	node = dt_lookup_alias(hw_devtree, "ccsr");
	if (!node)
		node = dt_get_subnode(hw_devtree, "soc", 0);

	if (!node) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
			 "%s: no ccsr alias or /soc node\n", __func__);
		return 0;
	}

	// Get the address format info for the CCSR node
	ret = get_addr_format(node, &child_addr_cells, &child_size_cells);
	if (ret) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
			 "%s: could not get address format info from CCSR node\n", __func__);
		return 0;
	}

	// Get the address format info for the parent of the CCSR node
	ret = get_addr_format(node->parent, &parent_addr_cells, &parent_size_cells);
	if (ret) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
			 "%s: could not get address format info from parent of CCSR node\n",
			 __func__);
		return 0;
	}

	// Get the 'ranges' property
	prop = dt_get_prop(node, "ranges", 0);
	if (!prop) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
			 "%s: no 'ranges' property in the CCSR node\n", __func__);
		return 0;
	}

	ret = xlate_one(addrbuf, prop->data, prop->len,
			parent_addr_cells, parent_size_cells,
			child_addr_cells, child_size_cells, &size);
	if (ret < 0) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
			 "%s: could not obtain physical address from CCSR node\n",
			 __func__);
		return 0;
	}

	addr = ((phys_addr_t)addrbuf[2] << 32) | addrbuf[3];

	printlog(LOGTYPE_MISC, LOGLEVEL_VERBOSE,
		 "%s: CCSR found at physical address %llx, size %llu\n", __func__, addr, size);

	*ccsr_size = size;
	return addr;
}

void libos_client_entry(unsigned long devtree_ptr)
{
	phys_addr_t cfg_addr = 0;
	size_t ccsr_size;
	void *fdt;
	int ret;

	sched_init();
	sched_core_init(cpu);

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

	hvconfig = dt_get_first_compatible(config_tree, "hv-config");
	if (!hvconfig) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,
		         "panic: no hv-config node.\n");
		return;
	}

	CCSRBAR_PA = get_ccsr_phys_addr(&ccsr_size);
	if (CCSRBAR_PA)
		CCSRBAR_VA = (unsigned long)map(CCSRBAR_PA, ccsr_size,
                                                TLB_MAS2_IO, TLB_MAS3_KERN);

	get_addr_format_nozero(hw_devtree, &rootnaddr, &rootnsize);

	assign_hv_devs();

	error_log_init(&hv_global_event_queue);

	enable_int();
	enable_mcheck();

	init_gevents();

	ccm_init();

	dt_for_each_compatible(hvconfig, "hv-memory", claim_hv_pma, NULL);

#ifdef CONFIG_BYTE_CHAN
#ifdef CONFIG_BCMUX
	create_muxes();
#endif

	open_stdout_bytechan(stdout_node);
#endif

#ifdef CONFIG_GCOV
	gcov_config(config_tree);
#endif

	vmpic_global_init();

#ifdef CONFIG_SHELL
	shell_init();
#endif

	create_doorbells();

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
	enable_mcheck();
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
	                          &len, TLB_TSIZE_16M, TLB_MAS2_IO);

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
	newcpu->coreid = reg;

	sched_core_init(newcpu);

	/* Terminate the callback chain. */
	newcpu->kstack[KSTACK_SIZE - FRAMELEN] = 0;

	if (start_secondary_spin_table(table_va, reg, newcpu))
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR, "couldn't spin up CPU%u\n", reg);

	return 0;
}

/* partition_init_done is atomically decremented each time a core
 * completes its partition init (or lack thereof if it has no
 * partition).  Once this reaches zero, gevents will be sent
 * to start the partitions.
 */
unsigned long partition_init_counter;

static int count_cores(dt_node_t *node, void *arg)
{
	dt_prop_t *prop = dt_get_prop(node, "reg", 0);
	if (prop && prop->len == 4) {
		uint32_t reg = *(const uint32_t *)prop->data;

		if (reg <= MAX_CORES)
			partition_init_counter++;
	}

	return 0;
}

static void release_secondary_cores(void)
{
	dt_for_each_prop_value(hw_devtree, "device_type", "cpu", 4,
	                       count_cores, NULL);
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
	int l1_cache_size = (mfspr(SPR_L1CFG0) & 0x7ff) * 1024;

	/* PIR was set by firmware-- record in cpu_t struct */
	cpu->coreid = mfspr(SPR_PIR);

	displacement_flush_area[cpu->coreid] =
		memalign(l1_cache_size, l1_cache_size);

	if (!displacement_flush_area[cpu->coreid]) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "Couldn't allocate displacement flush area\n");
	}

	/* dec init sequence
	 *  -disable DEC interrupts
	 *  -disable DEC auto reload
	 *  -set DEC to zero
	 *  -clear any pending DEC interrupts */
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_DIE);
	mtspr(SPR_TCR, mfspr(SPR_TCR) & ~TCR_ARE);
	mtspr(SPR_DEC, 0x0);
	mtspr(SPR_TSR, TSR_DIS);

	// Enable FIT hardware interrupts.  We fully emulate the FIT for the
	// guest, but we need interrupts enabled to do so.
	mtspr(SPR_TCR, mfspr(SPR_TCR) | TCR_FIE);

	mtspr(SPR_HID0, HID0_EMCP | HID0_DPM | HID0_ENMAS7);

#ifdef CONFIG_TLB_CACHE
	mtspr(SPR_EPCR,
	      mfspr(SPR_EPCR) | (EPCR_EXTGS | EPCR_DSIGS |
	      EPCR_DUVD | EPCR_DGTMI | EPCR_DMIUH));

	tlbcache_init();
#else
	mtspr(SPR_EPCR,
	      mfspr(SPR_EPCR) | (EPCR_EXTGS | EPCR_DTLBGS | EPCR_ITLBGS |
	      EPCR_DSIGS | EPCR_DUVD | EPCR_DGTMI));
#endif
}

/** Flush and disable core L2 cache
 * 
 * The cache is disabled by restricting allocations before flushing;
 * the actual cache enable bit is not cleared.
 *
 * @param[in] timeout timeout in timebase ticks
 * @param[in] unlock Clear locks if non-zero.
 *    If the cache supports persistent locks (as on e500mc), not clearing
 *    the locks keeps that cache line slot reserved until the "locked"
 *    cache line is referenced again.  If the cache does not support
 *    persistent locks, then locks will be cleared regardless of the
 *    unlock parameter (implicitly by the invalidate operation).
 * @param[out] old_l2csr0
 *    The old L2CSR0 is stored here, even if the operation fails.
 * @return negative on error
 */
int flush_disable_l2_cache(uint32_t timeout, int unlock, uint32_t *old_l2csr0)
{
	uint32_t l2csr0 = mfspr(SPR_L2CSR0);
	int inval = L2CSR0_L2FI;

	if (old_l2csr0)
		*old_l2csr0 = l2csr0;

	if (unlock)
		inval |= L2CSR0_L2LFC;

	if (l2csr0 & L2CSR0_L2E) {
		uint32_t tb = mfspr(SPR_TBL);
		int lock = L2CSR0_L2IO | L2CSR0_L2DO;

		/* The lock bits need to be set, with a read-back
		 * to ensure completion, before requesting a flush.
		 */
		set_cache_reg(SPR_L2CSR0, l2csr0 | lock);
		mfspr(SPR_L2CSR0);

		set_cache_reg(SPR_L2CSR0, l2csr0 | L2CSR0_L2FL | lock);

		while ((mfspr(SPR_L2CSR0) & (L2CSR0_L2FL | lock)) != lock) {
			if (mfspr(SPR_TBL) - tb > timeout) {
				mtspr(SPR_L2CSR0, l2csr0);
				printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				         "%s: L2 cache flush timeout\n",
				         __func__);
				return ERR_HARDWARE;
			}
		}

		set_cache_reg(SPR_L2CSR0, l2csr0 | inval | lock);

		while ((mfspr(SPR_L2CSR0) & (inval | lock)) != lock) {
			if (mfspr(SPR_TBL) - tb > timeout) {
				mtspr(SPR_L2CSR0, l2csr0);
				printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				         "%s: L2 cache invalidate timeout\n",
				         __func__);
				return ERR_HARDWARE;
			}
		}
	}

	return 0;
}

/** Flush core L1 and L2 caches, leaving them enabled.
 */
void flush_caches(void)
{
	/* Arbitrary 100ms timeout for cache to flush */
	uint32_t timeout = dt_get_timebase_freq() / 10;
	uint32_t l2csr0, l1csr0, l1csr1;
	int ret;

	flush_disable_l2_cache(timeout, 1, &l2csr0);

	l1csr0 = mfspr(SPR_L1CSR0);
	l1csr1 = mfspr(SPR_L1CSR1);

	ret = flush_disable_l1_cache(displacement_flush_area[cpu->coreid],
	                             timeout);

	set_cache_reg(SPR_L1CSR0, l1csr0);
	set_cache_reg(SPR_L1CSR1, l1csr1);
	set_cache_reg(SPR_L2CSR0, l2csr0);

	if (ret < 0)
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: L1 cache invalidate timeout\n", __func__);
}
