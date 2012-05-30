/** @file
 * Initialization
 */

/*
 * Copyright (C) 2007-2011 Freescale Semiconductor, Inc.
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
#include <libos/queue.h>

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
static phys_addr_t devtree_ptr;

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
#define COMMAND_LINE_SIZE	64

dt_node_t *hw_devtree, *config_tree, *virtual_tree;
uint32_t hw_devtree_lock;
void *temp_mapping[2];
extern char _end, _start;
uint64_t text_phys, bigmap_phys;

char *displacement_flush_area[MAX_CORES];

int auto_sys_reset_on_stop;

#ifdef CONFIG_WARM_REBOOT
int warm_reboot;
phys_addr_t pamu_mem_addr, pamu_mem_size;
pamu_hv_mem_t *pamu_mem_header;
#endif

static int get_cfg_addr(char *args, void *ctx)
{
	phys_addr_t *cfg_addr = ctx;
	char *str, *numstr;

	str = strstr(args, "config-addr=");
	if (!str) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "config-addr not specified in bootargs\n");
		return ERR_NOTFOUND;
	}

	str += strlen("config-addr=");
	numstr = nextword(NULL, &str);

	*cfg_addr = get_number64(numstr);
	if (cpu->errno) {
		print_num_error(&consolebuf, numstr);
		return cpu->errno;
	}

	return 0;
}

boot_param_t config_addr_param = {
	.name = "config-addr",
	.action = get_cfg_addr
};
boot_param(config_addr_param);

#ifdef CONFIG_WARM_REBOOT
static int get_warm_reboot(char *args, void *ctx)
{
	int *flag = ctx;

	*flag = 1;

	return 0;
}

boot_param_t warm_reboot_param = {
	.name = "warm-reboot",
	.ctx = &warm_reboot,
	.action = get_warm_reboot
};
boot_param(warm_reboot_param);
#endif

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

static void *map_fdt(phys_addr_t treephys)
{
	const size_t mapsize = 4 * 1024 * 1024;
	phys_addr_t mapaddr = treephys & ~((phys_addr_t)mapsize - 1);
	size_t len = mapsize;
	size_t total = 0;
	void *vaddr;

	/* Map guarded, as we don't know where the end of memory is yet. */
	vaddr = map_phys(TEMPTLB1, mapaddr, temp_mapping[0], &len,
	                 TLB_TSIZE_4M, TLB_MAS2_MEM | MAS2_G, TLB_MAS3_KDATA);
	map_phys(TEMPTLB2, mapaddr + mapsize, temp_mapping[0] + mapsize,
	         &len, TLB_TSIZE_4M, TLB_MAS2_MEM | MAS2_G, TLB_MAS3_KDATA);

	vaddr += treephys & (mapsize - 1);

	/* We handle trees that straddle one 4MiB boundary, but we
	 * don't support trees larger than 4MiB.
	 */
	if (mapsize * 2 - (treephys & (mapsize - 1)) < fdt_totalsize(vaddr)) {
		if (cpu->crashing)
			return NULL;

		panic("fdt too large: %zu bytes\n", fdt_totalsize(vaddr));
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

/*
 * Commands are separated by spaces and formatted as:
 *	param=value
 *	param=<list_of_comma_separated_values>
 *	param
 */
static int parse_bootargs(void)
{
	void *fdt;
	int offset, len;
	extern boot_param_t *bootparam_begin, *bootparam_end;
	boot_param_t **i;
	static char cmdline[COMMAND_LINE_SIZE];
	char *str, *cmd_iter = cmdline;

	fdt = map_fdt(devtree_ptr);

	offset = fdt_subnode_offset(fdt, 0, "chosen");
	if (offset < 0) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		        "Cannot find /chosen, error %d\n", offset);
		return offset;
	}

	str = fdt_getprop_w(fdt, offset, "bootargs", &len);
	if (!str || len == 0 || str[len - 1] != 0) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "Bad/missing bootargs in /chosen, error %d\n", len);
		return len;
	}

	if (len > COMMAND_LINE_SIZE) {	/* len includes \0 */
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "Too long command line: %d bytes\n", len);
		return len;
	}

	printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
	         "Hypervisor command line: %s\n", str);
	strncpy(cmdline, str, len - 1);

	while ((str = nextword(NULL, &cmd_iter))) {
		char *end_name;
		int size;

		/* FIXME: it is an excessive string parsing */
		if ((end_name = strchr(str, '=')))
			size = end_name - str;
		else
			size = strlen(str);

		for (i = &bootparam_begin; i < &bootparam_end; i++) {
			if (strlen((*i)->name) == size &&
			    !strncmp(str, (*i)->name, size)) {
				int ret = (*i)->action(str, (*i)->ctx);

				if (ret) {
					printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
						 "Error parsing boot args: %d\n",
						 ret);
				}

				break;
			}
		}
	}

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
		printlog(LOGTYPE_MMU, LOGLEVEL_ERROR,
		         "%s: error %d mapping PMA %#llx to %#llx\n",
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
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: error %d finding hv-memory\n",
				         __func__, offset);

			break;
		}

		prop = fdt_getprop(fdt, offset, "phys-mem", &len);
		if (!prop || len != 4) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d getting phys-mem\n",
			         __func__, len);
			continue;
		}

		pma = fdt_node_offset_by_phandle(fdt, *prop);
		if (pma < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d looking up phys-mem\n",
			         __func__, pma);
			continue;
		}

		prop = fdt_getprop(fdt, pma, "addr", &len);
		if (!prop || len != 8) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d getting pma addr\n",
			         __func__, len);
			continue;
		}
		addr = (((uint64_t)prop[0]) << 32) | prop[1];

		prop = fdt_getprop(fdt, pma, "size", &len);
		if (!prop || len != 8) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d getting pma size\n",
			         __func__, len);
			continue;
		}
		size = (((uint64_t)prop[0]) << 32) | prop[1];

		if (size & (size - 1)) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: pma size %lld not a power of 2\n",
			         __func__, size);
			continue;
		}

		if (addr & (size - 1)) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: pma addr %llx not aligned to size %llx\n",
			         __func__, addr, size);
			continue;
		}

		if (add) {
#ifdef CONFIG_WARM_REBOOT
			prop = fdt_getprop(fdt, offset, "hv-persistent-data",
					   &len);
			if (prop && !pamu_mem_size) {
				if (prop && len != 4 * sizeof(uint32_t)) {
					printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
					        "%s: wrong length %d for "
					        "hv-persistent-data\n",
					        __func__, len);
					warm_reboot = 0;
					continue;
				}

				pamu_mem_addr = addr;
				pamu_mem_addr += (((uint64_t)prop[0]) << 32)
						 | prop[1];
				pamu_mem_size = (((uint64_t)prop[2]) << 32)
						| prop[3];

				if ((pamu_mem_addr + pamu_mem_size > addr + size) ||
				    (pamu_mem_addr + pamu_mem_size < pamu_mem_addr) ||
				    (pamu_mem_addr > addr + size) ||
				    (pamu_mem_addr < addr)) {
					printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
					         "%s: wrong offset/size for "
					         "hv-persistent-data\n",
					         __func__);
					pamu_mem_size = 0;
				} else {
					pamu_mem_header = (void *)(uintptr_t)pamu_mem_addr +
							  BIGPHYSBASE -
							  bigmap_phys;
				}
			} else if (prop) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: discard multiple "
				         "hv-persistent-data\n", __func__);
			}
#endif
			add_memory(addr, size);
		} else if (addr < bigmap_phys) {
			bigmap_phys = addr & ~((phys_addr_t)BIGPHYSBASE - 1);
		}
	}

#ifdef CONFIG_WARM_REBOOT
	if (add && !pamu_mem_size) {
		/* Override a possible command line argument */
		if (warm_reboot) {
			printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				 "ignoring parameter \"warm-reboot\" because "
				 "hv-persistent-data was not found\n");
			warm_reboot = 0;
		}
	}
	if (add && !warm_reboot && pamu_mem_header) {
		memset(pamu_mem_header, 0, pamu_mem_size);
		pamu_mem_header->magic = HV_MEM_MAGIC;
		pamu_mem_header->version = 0;
	}
#endif
}

/* Note that we do not verify that a PMA is covered by a hw memory node,
 * or that guest PMAs are not covered by memreserve areas.
 */
static int init_hv_mem(phys_addr_t cfg_addr)
{
	void *fdt;
	int offset;
	int depth = 0;

	fdt = map_fdt(cfg_addr);

	offset = fdt_next_descendant_by_compatible(fdt, 0, &depth, "hv-config");
	if (offset < 0) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: config tree has no hv-config, err %d\n",
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
#ifdef CONFIG_WARM_REBOOT
	if (pamu_mem_size)
		exclude_phys(pamu_mem_addr, pamu_mem_addr + pamu_mem_size - 1);
#endif

	unmap_fdt();

	if (reloc_hv) {
		void *new_text = malloc_alloc_segment(&_end - &_start,
		                                      hv_text_size);
		if (!new_text) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "Cannot relocate hypervisor\n");
			return ERR_NOMEM;
		}

		text_phys = virt_to_phys(new_text);
		barrier();
		memcpy(new_text, (void *)PHYSBASE, (uintptr_t)&_end - PHYSBASE);
		branch_to_reloc(new_text, (text_phys & MAS3_RPN) | TLB_MAS3_KERN,
		                text_phys >> 32);
	}

	malloc_init();
	return 0;
}

static void early_bind_devices(void)
{
	dt_prop_t *prop;
	dt_node_t *node;
	const char *compat_srch_str[] = {
		"fsl,qoriq-device-config-1.0",
		 "fsl,corenet-law",	/* before corenet-cf driver */
		 "fsl,corenet-cf"
	};
	dt_node_t *sorted[sizeof(compat_srch_str) / sizeof(compat_srch_str[0])] = {};

	list_for_each(&hv_devs, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, guest_node);
		prop = dt_get_prop(owner->hwnode, "compatible", 0);
		if (!prop)
			continue;
		if (!strcmp((const char *)prop->data, compat_srch_str[0]))
			sorted[0] = owner->hwnode;
		else if (!strcmp((const char *)prop->data, compat_srch_str[1]))
			sorted[1] = owner->hwnode;
		else if (!strcmp((const char *)prop->data, compat_srch_str[2]))
			sorted[2] = owner->hwnode;
	}

	/* Execution dependence: law driver before ccm driver */
	for (int i = 0; i < sizeof(compat_srch_str) / sizeof(compat_srch_str[0]);
	     i++) {
		if (!sorted[i])
			continue;

		dt_lookup_regs(sorted[i]);
		dt_lookup_irqs(sorted[i]);
		dt_bind_driver(sorted[i]);
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

	early_bind_devices();

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


static void setup_error_manager(void)
{
	int ret, errmgr_handle;
	uint32_t propdata;
	dt_node_t *node;

	error_log_init(&global_event_queue);

	node = dt_get_subnode(virtual_tree, "error-manager", 1);
	if (!node)
		goto nomem;

	/* TODO: Clean the addr/len sizes for future usage of the
	 * virtual tree. For now, only the error-manager node is kept in
	 * and this will be merged into "/hypervisor/handles" subtree
	 * which has 1/0 for addr/size. If future virtual devices should
	 * be merged into "/devices", different pair might have to be
	 * accomodated.
	 */
	propdata = 1;
	ret = dt_set_prop(virtual_tree, "#address-cells", &propdata, 4);
	if (ret < 0)
		goto nomem;

	propdata = 0;
	ret = dt_set_prop(virtual_tree, "#size-cells", &propdata, 4);
	if (ret < 0)
		goto nomem;

	ret = dt_set_prop_string(node, "compatible",
				 "fsl,hv-error-manager");
	if (ret < 0)
		goto nomem;

	return;
nomem:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR, "%s: out of memory\n",
		 __func__);
}

#ifdef CONFIG_HV_WATCHDOG
/**
 * watchdog_init - enable and initialize the watchdog, if requested
 *
 * This function looks for a watchdog-enable property in the HV config tree,
 * and if found, enables and initializes the watchdog.
 */
static void watchdog_init(void)
{
	const dt_prop_t *prop;
	unsigned int period;
	uint32_t tcr = 0;

	prop = dt_get_prop(hvconfig, "watchdog-enable", 0);
	if (!prop) {
		// No watchdog-enable property?  Just exit quietly
		return;
	}

	if (prop->len != sizeof(uint32_t)) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			 "watchdog: invalid watchdog-enable property\n");
		return;
	}

	period = *(const uint32_t *)prop->data;
	if (period > TCR_WP_TO_INT(TCR_WP_MASK)) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			 "watchdog: period must be between 0 and %u\n",
			 TCR_WP_TO_INT(TCR_WP_MASK));
		return;
	}

	printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		 "watchdog enabled with period %u\n", period);

	// Set the TCR bits
	tcr = mfspr(SPR_TCR);
	tcr &= ~(TCR_WP_MASK | TCR_WRC);
	tcr |= TCR_WIE | TCR_INT_TO_WP(period) | TCR_WRC_REQ;
	mtspr(SPR_TCR, tcr);
}
#endif

/* partition_init_counter is atomically decremented each time a core
 * completes its partition init (or lack thereof if it has no
 * partition).  Once this reaches zero, gevents will be sent
 * to start the partitions.
 */
unsigned long partition_init_counter;

/* For each core found in the device tree a bit is set starting
 * with the most significant one
 */
uint32_t cpus_mask;

static int count_cores(dt_node_t *node, void *arg)
{
	dt_prop_t *prop = dt_get_prop(node, "reg", 0);
	if (prop && prop->len >= 4) {
		uint32_t reg = *(const uint32_t *)prop->data;

		if (reg <= MAX_CORES) {
			partition_init_counter++;
			cpus_mask |= 1 << (31 - reg);
		}
	}

	return 0;
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

/* Push output directly through a serial port if the configured
 * console is not set up yet.  Once a real console is configured,
 * set_crashing()
 */
void __attribute__((noreturn)) panic_flush(void)
{
	phys_addr_t phys, size;
	uint8_t *addr;
	int len;
	size_t mapsize = 8;
	int ret;

	set_crashing(1);

	void *fdt = map_fdt(devtree_ptr);
	if (!fdt)
		goto guess;

	/* stdout alias? */
	int node = fdt_path_offset(fdt, "stdout");
	if (node >= 0)
		goto gotnode;

	/* linux,stdout-path? */
	node = fdt_path_offset(fdt, "/chosen");
	if (node >= 0) {
		const char *path = fdt_getprop(fdt, node, "linux,stdout-path",
		                               &len);
		if (path) {
			node = fdt_path_offset(fdt, path);
			if (node >= 0)
				goto gotnode;
		}
	}

	/* Any 16550 at all? */
	node = fdt_node_offset_by_compatible(fdt, -1, "ns16550");
	if (node < 0)
		goto guess;

gotnode:
	if (!fdt_get_reg(fdt, node, 0, &phys, &size))
		goto gotphys;

guess:
	/* It's worth a shot... */
	phys = 0xffe11c500ULL;
gotphys:
	addr = map_phys(TEMPTLB1, phys, temp_mapping[0], &mapsize,
	                TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KDATA);

	int ch;
	while ((ch = queue_readchar(&consolebuf, 0)) >= 0) {
		while (!(in8(&addr[NS16550_LSR]) & NS16550_LSR_THRE))
			;

		out8(&addr[NS16550_THR], ch);
	}

die:
	for (;;)
		;
}

void __attribute__((noreturn)) panic(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS,"panic: ");
	int ret = vprintf(fmt, args);

	va_end(args);

	panic_flush();
}

void libos_client_entry(unsigned long treephys)
{
	phys_addr_t cfg_addr = 0;
	size_t ccsr_size;
	void *fdt;
	int ret;

	devtree_ptr = treephys;

	sched_init();
	sched_core_init(cpu);

	printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		 "=======================================\n");
#ifdef CONFIG_LIBOS_64BIT
	printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		 "Freescale Hypervisor %s ppc64\n", CONFIG_HV_VERSION);
#else
	printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		 "Freescale Hypervisor %s\n", CONFIG_HV_VERSION);
#endif

	cpu->client.next_dyn_tlbe = DYN_TLB_START;

	valloc_init(VMAPBASE, BIGPHYSBASE);
	temp_mapping[0] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);
	temp_mapping[1] = valloc(16 * 1024 * 1024, 16 * 1024 * 1024);

	config_addr_param.ctx = &cfg_addr;
	ret = parse_bootargs();
	if (ret < 0)
		panic("couldn't get config tree address\n");

	ret = init_hv_mem(cfg_addr);
	if (ret < 0)
		panic("init_hv_mem failed\n");

	cpu->console_ok = 1;
	core_init();

	fdt = map_fdt(devtree_ptr);
	hw_devtree = unflatten_dev_tree(fdt);
	if (!hw_devtree)
		panic("couldn't unflatten hardware device tree.\n");

	unmap_fdt();

	fdt = map_fdt(cfg_addr);
	config_tree = unflatten_dev_tree(fdt);
	if (!config_tree)
		panic("couldn't unflatten config device tree.\n");

	unmap_fdt();

	virtual_tree = create_dev_tree();
	if (!virtual_tree)
		panic("couldn't allocate virtual tree\n");

	hvconfig = dt_get_first_compatible(config_tree, "hv-config");
	if (!hvconfig)
		panic("no hv-config node.\n");

	CCSRBAR_PA = get_ccsr_phys_addr(&ccsr_size);
	if (CCSRBAR_PA)
		CCSRBAR_VA = (unsigned long)map(CCSRBAR_PA, ccsr_size,
                                                TLB_MAS2_IO, TLB_MAS3_KDATA);

	get_addr_format_nozero(hw_devtree, &rootnaddr, &rootnsize);

	assign_hv_devs();

	error_log_init(&hv_global_event_queue);

	enable_int();
	enable_mcheck();

	init_gevents();

	ccm_init();

	dt_for_each_prop_value(hw_devtree, "device_type", "cpu", 4, count_cores, NULL);

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

#ifdef CONFIG_HV_WATCHDOG
	watchdog_init();
#endif

#ifdef CONFIG_SHELL
	shell_init();
#endif

	create_doorbells();

	setup_error_manager();

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
#ifdef CONFIG_HV_WATCHDOG
	watchdog_init();
#endif
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
	if (!prop || prop->len < 4) {
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
	                          &len, TLB_TSIZE_16M, TLB_MAS2_IO,
	                          TLB_MAS3_KDATA);

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
		register_t tb = mfspr(SPR_TBL);
		int lock = L2CSR0_L2IO | L2CSR0_L2DO;

		/* The lock bits need to be set, with a read-back
		 * to ensure completion, before requesting a flush.
		 */
		set_cache_reg(SPR_L2CSR0, l2csr0 | lock);

		while ((mfspr(SPR_L2CSR0) & lock) != lock) {
			if (mfspr(SPR_TBL) - tb > timeout) {
				mtspr(SPR_L2CSR0, l2csr0);
				printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
				         "%s: L2 cache flush timeout\n",
				         __func__);
				return ERR_HARDWARE;
			}
		}

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
	uint32_t l2csr0, l1csr0, l1csr1, l1csr2;
	int ret;

	flush_disable_l2_cache(timeout, 1, &l2csr0);

	l1csr0 = mfspr(SPR_L1CSR0);
	l1csr1 = mfspr(SPR_L1CSR1);
	l1csr2 = mfspr(SPR_L1CSR2);

	ret = flush_disable_l1_cache(displacement_flush_area[cpu->coreid],
	                             timeout);

	set_cache_reg(SPR_L1CSR0, l1csr0);
	set_cache_reg(SPR_L1CSR1, l1csr1);
	set_cache_reg(SPR_L1CSR2, l1csr2);
	set_cache_reg(SPR_L2CSR0, l2csr0);

	if (ret < 0)
		printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
		         "%s: L1 cache invalidate timeout\n", __func__);
}
