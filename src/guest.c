/** @file
 * Guest management
 */
/*
 * Copyright (C) 2007-2008 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <libos/percpu.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/core-regs.h>
#include <libos/io.h>
#include <libos/bitops.h>
#include <libos/mpic.h>

#include <hv.h>
#include <paging.h>
#include <timers.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <pamu.h>
#include <ipi_doorbell.h>
#include <devtree.h>
#include <errors.h>
#include <elf.h>
#include <uimage.h>
#include <events.h>
#include <doorbell.h>
#include <gdb-stub.h>

guest_t guests[MAX_PARTITIONS];
unsigned long last_lpid;

#define MAX_PATH 256

static int vcpu_to_cpu(const uint32_t *cpulist, unsigned int len, int vcpu)
{
	unsigned int i, vcpu_base = 0;
	
	for (i = 0; i < len / 4; i += 2) {
		if (vcpu >= vcpu_base && vcpu < vcpu_base + cpulist[i + 1])
			return cpulist[i] + vcpu - vcpu_base;

		vcpu_base += cpulist[i + 1];
	}

	return ERR_RANGE;
}

static int cpu_in_cpulist(const uint32_t *cpulist, unsigned int len, int cpu)
{
	unsigned int i;
	for (i = 0; i < len / 4; i += 2) {
		if (cpu >= cpulist[i] && cpu < cpulist[i] + cpulist[i + 1])
			return 1;
	}

	return 0;
}

static int get_gcpu_num(const uint32_t *cpulist, unsigned int len, int cpu)
{
	unsigned int i;
	unsigned int total = 0; 

	for (i = 0; i < len / 4; i += 2) {
		unsigned int base = cpulist[i];
		unsigned int num = cpulist[i + 1];

		if (cpu >= base && cpu < base + num)
			return total + cpu - base;

		total += num;
	}

	return ERR_RANGE;
}

static unsigned int count_cpus(const uint32_t *cpulist, unsigned int len)
{
	unsigned int i;
	unsigned int total = 0;

	for (i = 0; i < len / 4; i += 2)
		total += cpulist[i + 1];	

	return total;
}

#ifdef CONFIG_DEVICE_VIRT

typedef struct count_devices_ctx {
	phys_addr_t paddr;
	int count;
} count_devices_ctx_t;

static int count_devices_callback(dt_node_t *node, void *arg)
{
	count_devices_ctx_t *ctx = arg;
	phys_addr_t paddr;

	if (node->parent && node->parent->parent &&
	    !dt_node_is_compatible(node->parent, "simple-bus"))
		return 0;

	// Get the guest physical address for this device
	int ret = dt_get_reg(node, 0, &paddr, NULL);
	if (ret < 0)
		return 0;

	paddr &= ~(PAGE_SIZE - 1);

	if (paddr == ctx->paddr)
		ctx->count++;

	return 0;	
}

/**
 * Determine if there's another device sharing this page
 *
 * This function scans the device tree to determine if there are other
 * devices that shares the same memory page as this one.  It returns a count
 * of these devices, or a negative number if an error occurs.
 *
 * We assume that if there are any two devices that share the same page,
 * the registers of all such devices completely fit within one page.  This
 * eliminates the need to be given a 'size' parameter.
 */
static int count_devices_in_page(dt_node_t *tree, phys_addr_t paddr)
{
	count_devices_ctx_t ctx = {
		// Round down to the nearest page
		.paddr = paddr & ~(PAGE_SIZE - 1)
	};

	dt_for_each_node(tree, &ctx, count_devices_callback, NULL);

	return ctx.count;
}
#endif

static void map_guest_addr_range(guest_t *guest, phys_addr_t gaddr,
                                 phys_addr_t addr, phys_addr_t size)
{
	unsigned long grpn = gaddr >> PAGE_SHIFT;
	unsigned long rpn = addr >> PAGE_SHIFT;
	unsigned long pages = (gaddr + size -
	                       (grpn << PAGE_SHIFT) +
	                       (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "mapping guest %lx to real %lx, %lx pages\n",
	         grpn, rpn, pages);

	vptbl_map(guest->gphys, grpn, rpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, grpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
}

static int map_guest_reg_one(guest_t *guest, dt_node_t *node,
                             dt_node_t *partition, const uint32_t *reg,
                             uint32_t naddr, uint32_t nsize)
{
	phys_addr_t gaddr, size;
	uint32_t addrbuf[MAX_ADDR_CELLS];
	phys_addr_t rangesize, addr, offset = 0;
	int maplen, ret;
	dt_prop_t *prop;
	const uint32_t *physaddrmap;

	prop = dt_get_prop(partition, "fsl,hv-physaddr-map", 0);
	if (!prop || prop->len & 3) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS, "%s: %d %s\n", __func__, __LINE__, node->name);
		return ERR_BADTREE;
	}

	physaddrmap = prop->data;
	maplen = prop->len;

	ret = xlate_reg_raw(node, reg, addrbuf, &size, naddr, nsize);
	if (ret < 0) {
		printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS, "%s: %d %s\n", __func__, __LINE__, node->name);
		return ret;	
	}

	gaddr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

	// Is this an I2C node?
#ifdef CONFIG_VIRTUAL_I2C
	if (dt_node_is_compatible(node, "fsl-i2c")) {
		// We know that I2C devices come in pairs, so if we
		// _don't_ find another device on this page, then we
		// know that we should restrict access in this page.
		if (count_devices_in_page(guest->devtree, gaddr) == 1) {
			ret = register_vf_handler(guest, gaddr, gaddr + size,
			                          i2c_callback);
			return ret;
		}
	}
#endif

	while (offset < size) {
		val_from_int(addrbuf, gaddr + offset);

		ret = xlate_one(addrbuf, physaddrmap, maplen, rootnaddr, rootnsize,
		                guest->naddr, guest->nsize, &rangesize);
		if (ret == ERR_NOTFOUND) {
			// FIXME: It is assumed that if the beginning of the reg is not
			// in physaddrmap, then none of it is.

			map_guest_addr_range(guest, gaddr + offset,
			                     gaddr + offset, size - offset);
			return 0;
		}
		
		if (ret < 0) {
			printlog(LOGTYPE_MISC, LOGLEVEL_ALWAYS, "%s: %d %s\n", __func__, __LINE__, node->name);
			return ret;
		}

		if (addrbuf[0] || addrbuf[1]) {
			printf("%s: %d %x %x\n", __func__, __LINE__, addrbuf[0], addrbuf[1]);
			return ERR_BADTREE;
		}

		addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

		if (rangesize > size - offset)
			rangesize = size - offset;
		
		map_guest_addr_range(guest, gaddr + offset, addr, rangesize);
		offset += rangesize;
	}

	return 0;
}

static int map_guest_ranges(guest_t *guest, dt_node_t *node,
                            dt_node_t *partition)
{
	size_t len;
	uint32_t naddr, nsize, caddr, csize;
	dt_prop_t *prop;
	const uint32_t *ranges;
	int ret;

	prop = dt_get_prop(node, "fsl,hv-map-ranges", 0);
	if (!prop)
		return 0;

	prop = dt_get_prop(node, "ranges", 0);
	if (!prop)
		return 0;

	if (prop->len & 3) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
		         "Unaligned ranges length %d in %s\n", prop->len, prop->name);
		return ERR_BADTREE;
	}

	ranges = prop->data;
	len = prop->len / 4;

	dt_node_t *parent = node->parent;
	if (!parent)
		return ERR_BADTREE;

	ret = get_addr_format_nozero(parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	ret = get_addr_format_nozero(node, &caddr, &csize);
	if (ret < 0)
		return ret;

	for (int i = 0; i < len; i += caddr + naddr + csize) {
		if (i + caddr + naddr + csize > len) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			         "Incomplete ranges entry in %s\n", prop->name);
			return ERR_BADTREE;
		}

		ret = map_guest_reg_one(guest, node, partition,
		                        ranges + i + caddr, naddr, csize);
		if (ret < 0 && ret != ERR_NOTRANS)
			return ret;
	}

	return 0;
}

static int map_guest_reg(guest_t *guest, dt_node_t *node, dt_node_t *partition)
{
	int len, ret;
	uint32_t naddr, nsize;
	const uint32_t *reg;
	dt_prop_t *prop;
	dt_node_t *parent;
	
	prop = dt_get_prop(node, "reg", 0);
	if (!prop)
		return 0;

	if (prop->len & 3) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
		         "Unaligned ranges length %d in %s\n", prop->len, prop->name);
		return ERR_BADTREE;
	}

	reg = prop->data;
	len = prop->len / 4;

	parent = node->parent;
	if (!parent)
		return ERR_BADTREE;

	if (parent->parent && !dt_node_is_compatible(parent, "simple-bus"))
		return 0;

	ret = get_addr_format_nozero(parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	for (int i = 0; i < len; i += naddr + nsize) {
		if (i + naddr + nsize > len) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			         "Incomplete reg entry in %s\n", prop->name);
			return ERR_BADTREE;
		}

		ret = map_guest_reg_one(guest, node, partition,
		                        reg + i, naddr, nsize);
		if (ret < 0 && ret != ERR_NOTRANS)
			return ret;
	}

	return 0;
}

typedef struct map_guest_ctx {
	guest_t *guest;
	dt_node_t *partition;
} map_guest_ctx_t;

static int map_guest_one(dt_node_t *node, void *arg)
{
	map_guest_ctx_t *ctx = arg;
	int ret;
	
	ret = map_guest_reg(ctx->guest, node, ctx->partition);
	if (ret < 0)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "map_guest_reg: error %d in %s\n", ret, node->name);

	ret = map_guest_ranges(ctx->guest, node, ctx->partition); 
	if (ret < 0)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "map_guest_ranges: error %d in %s\n", ret, node->name);

	return 0;
}

static int map_guest_reg_all(guest_t *guest)
{
	map_guest_ctx_t ctx = {
		.guest = guest,
		.partition = guest->partition
	};

	return dt_for_each_node(guest->devtree, &ctx, map_guest_one, NULL);
}

static void reset_spintbl(guest_t *guest)
{
	struct boot_spin_table *spintbl = guest->spintbl;
	int i;

	for (i = 0; i < guest->cpucnt; i++) {
		spintbl[i].addr_hi = 0;
		spintbl[i].addr_lo = 1;
		spintbl[i].pir = i;
		spintbl[i].r3_hi = 0;
		spintbl[i].r3_lo = 0;
	}
}

static const char *cpu_clocks[] = {
	"clock-frequency",
	"timebase-frequency",
	"bus-frequency",
};

static int copy_cpu_clocks(guest_t *guest, dt_node_t *vnode, int vcpu,
                           const uint32_t *cpulist, int cpulist_len)
{
	dt_node_t *node;
	dt_prop_t *prop;
	int pcpu, i;
	
	pcpu = vcpu_to_cpu(cpulist, cpulist_len, vcpu);
	if (pcpu < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "copy_cpu_clocks: partition has no cpu %d\n", vcpu);
		return pcpu;
	}

	node = get_cpu_node(hw_devtree, pcpu);
	if (!node) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "copy_cpu_clocks: vcpu %d maps to non-existent CPU %d\n",
		         vcpu, pcpu);
		return ERR_RANGE;
	}

	for (i = 0; i < sizeof(cpu_clocks) / sizeof(char *); i++) {
		int ret;

		prop = dt_get_prop(node, cpu_clocks[i], 0);
		if (!prop) {
			printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			         "copy_cpu_clocks: failed to read clock property");

			return ERR_BADTREE;
		}

		ret = dt_set_prop(vnode, cpu_clocks[i], prop->data, prop->len);
		if (ret < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "copy_cpu_clocks: failed to set clock property, "
			         "error %d\n", ret);

			return ret;
		}
	}

	return 0;
} 

typedef struct create_guest_spin_table_ctx {
	guest_t *guest;
	const uint32_t *cpulist;
	int cpulist_len;
} create_guest_spin_table_ctx_t;

static int create_guest_spin_table_cpu(dt_node_t *node, void *arg)
{
	create_guest_spin_table_ctx_t *ctx = arg;
	uint64_t spin_addr;
	dt_prop_t *prop;
	int pcpu, ret;
	
	prop = dt_get_prop(node, "reg", 0);
	if (!prop || prop->len != 4) {
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "create_guest_spin_table: missing/bad cpu reg property\n");
		return ERR_BADTREE;
	}
	pcpu = *(const uint32_t *)prop->data;

	ret = copy_cpu_clocks(ctx->guest, node, pcpu,
	                      ctx->cpulist, ctx->cpulist_len);
	if (ret < 0)
		return ret;

	if (pcpu == 0)
		return dt_set_prop_string(node, "status", "okay");

	ret = dt_set_prop_string(node, "status", "disabled");
	if (ret < 0)
		return ret;

	ret = dt_set_prop_string(node, "enable-method", "spin-table");
	if (ret < 0)
		return ret;

	spin_addr = 0xfffff000 + pcpu * sizeof(struct boot_spin_table);
	return dt_set_prop(node, "cpu-release-addr", &spin_addr, 8);
}

static int create_guest_spin_table(guest_t *guest,
                                   const uint32_t *cpulist,
                                   int cpulist_len)
{
	unsigned long rpn;
	int ret, i;

	create_guest_spin_table_ctx_t	ctx = {
		.guest = guest,
		.cpulist = cpulist,
		.cpulist_len = cpulist_len
	};

	guest->spintbl = alloc(PAGE_SIZE, PAGE_SIZE);
	if (!guest->spintbl)
		return ERR_NOMEM;

	/* FIXME: hardcoded cache line size */
	for (i = 0; i < guest->cpucnt * sizeof(struct boot_spin_table); i += 32)
		asm volatile("dcbf 0, %0" : : "r" ((unsigned long)guest->spintbl + i ) :
		             "memory");

	rpn = virt_to_phys(guest->spintbl) >> PAGE_SHIFT;

	vptbl_map(guest->gphys, 0xfffff, rpn, 1, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, 0xfffff, 1, PTE_ALL, PTE_PHYS_LEVELS);

	ret = dt_for_each_prop_value(guest->devtree, "device_type", "cpu", 4,
	                             create_guest_spin_table_cpu, &ctx);

	if (ret < 0)
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "create_guest_spin_table: error %d\n", ret);

	return ret;
}

/**
 * create_sdbell_handle - craete the receive handles for a special doorbell
 * @kind - the doorbell name
 * @guest - guest device tree
 * @offset - pointer to the partition node in the guest device tree
 * @dbell - pointer to special doorbell to use
 *
 * This function creates a doorbell receive handle node (under the partition
 * node for each managed partition in a the manager's device tree) for a
 * particular special doorbell.
 *
 * 'kind' is a string that is used to both name the special doorbell and to
 * create the compatible property for the receive handle node.
 *
 * This function must be called after recv_dbell_partition_init() is called,
 * because it creates receive doorbell handles that do not have an fsl,endpoint
 * property.  recv_dbell_partition_init() considers receive doorbell handle
 * nodes without and endpoint to be an error.  And endpoint is required in
 * doorbell handle nodes only when the doorbell is defined in the hypervisor
 * DTS.  Special doorbells are created by the hypervisor in
 * create_guest_special_doorbells(), so they don't exist in any DTS.
 */
static int create_sdbell_handle(const char *kind,
                                guest_t *guest, dt_node_t *node,
                                struct ipi_doorbell *dbell)
{
	char s[96];	// Should be big enough
	int ret, length;

	// Create the special doorbell receive handle node.
	node = dt_get_subnode(node, kind, 1);
	if (!node)
		return ERR_NOMEM;

	// 'offset' is now the offset of the new doorbell receive handle node

	// Write the 'compatible' property to the doorbell receive handle node
	// We can't embed a \0 in the format string, because that will confuse
	// snprintf (it will stop scanning when it sees the \0), so we use %c.
	length = snprintf(s, sizeof(s),
		"fsl,hv-%s-doorbell%cfsl,hv-doorbell-receive-handle", kind, 0);

	ret = dt_set_prop(node, "compatible", s, length + 1);
	if (ret < 0)
		return ret;

	ret = attach_receive_doorbell(guest, dbell, node);

	return ret;
}

/**
 * create_guest_special_doorbells - create the special doorbells for this guest
 *
 * Each guest gets a number of special doorbells.  These doorbells are run
 * when certain specific events occur, and the manager needs to be notified.
 *
 * For simplicity, these doorbells are always created, even if there is no
 * manager.  The doorbells will still be rung when the corresponding event
 * occurs, but no interrupts will be sent.
 */
static int create_guest_special_doorbells(guest_t *guest)
{
	assert(!guest->dbell_state_change);

	guest->dbell_state_change = alloc_type(ipi_doorbell_t);
	if (!guest->dbell_state_change) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s: out of memory\n", __func__);
		goto error;
	}

	guest->dbell_watchdog_expiration = alloc_type(ipi_doorbell_t);
	if (!guest->dbell_watchdog_expiration) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s: out of memory\n", __func__);
		goto error;
	}

	guest->dbell_restart_request = alloc_type(ipi_doorbell_t);
	if (!guest->dbell_restart_request) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s: out of memory\n", __func__);
		goto error;
	}

	return 0;

error:
	free(guest->dbell_restart_request);
	free(guest->dbell_watchdog_expiration);
	free(guest->dbell_restart_request);

	guest->dbell_restart_request = NULL;
	guest->dbell_watchdog_expiration = NULL;
	guest->dbell_restart_request = NULL;

	return ERR_NOMEM;
}

/**
 * Load an image into guest memory
 *
 * @guest: guest data structure
 * @image: real physical address of the image
 * @guest_phys: guest physical address to load the image to
 * @length: size of the target window, can be -1 if unspecified
 *
 * If the image is a plain binary, then 'length' must be the exact size of
 * the image.
 *
 * If the image is an ELF, then 'length' is used only to verify the image
 * data.  To skip verification, set length to -1.
 */
static int load_image_from_flash(guest_t *guest, phys_addr_t image,
                                 phys_addr_t guest_phys, size_t length,
                                 register_t *entry)
{
	int ret;

	ret = load_elf(guest, image, length, guest_phys, entry);
	if (ret != ERR_UNHANDLED)
		return ret;

	ret = load_uimage(guest, image, length, guest_phys, entry);
	if (ret != ERR_UNHANDLED)
		return ret;

	/* Neither an ELF image nor uImage, so it must be a binary. */

	if (!length || (length == (size_t) -1)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "load_image_from_flash: missing or invalid image size\n");
		return ERR_BADTREE;
	}

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "loading binary image from flash\n");

	if (copy_phys_to_gphys(guest->gphys, guest_phys, image, length) != length)
		return ERR_BADADDR;

	if (entry)
		*entry = guest_phys;

	return 0;
}

/**
 * If an ELF or binary image exists, load it.  This must be called after
 * map_guest_reg_all(), because that function creates some of the TLB
 * mappings we need.
 *
 * Returns 1 if the image is loaded successfully, 0 if no image, negative on
 * error.
 */
static int load_image(guest_t *guest)
{
	int ret, first = 1;
	dt_prop_t *prop;
	const uint32_t *entry, *end;
	uint64_t image_addr;
	uint64_t guest_addr;
	uint64_t length;

	prop = dt_get_prop(guest->partition, "fsl,hv-load-image-table", 0);
	if (!prop)
		return 0;

	entry = prop->data;
	end = (const uint32_t *)((uintptr_t)prop->data + prop->len);

	while (entry + rootnaddr + guest->naddr + guest->nsize <= end) {
		image_addr = int_from_tree(&entry, rootnaddr);
		guest_addr = int_from_tree(&entry, guest->naddr);
		length = int_from_tree(&entry, guest->nsize);

		if (length != (size_t)length) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_image: guest %s: invalid length %#llx\n",
			         guest->name, length);
			continue;
		}

		ret = load_image_from_flash(guest, image_addr, guest_addr,
		                            length ? length : -1,
		                            first ? &guest->entry : NULL);
		if (ret < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "guest %s: could not load image\n", guest->name);
			return ret;
		}

		first = 0;
	}
	
	return 1;
}
		
static const uint32_t hv_version[4] = {
	CONFIG_HV_MAJOR_VERSION,
	CONFIG_HV_MINOR_VERSION,
	FH_API_VERSION,
	FH_API_COMPAT_VERSION
};

static int process_partition_handle(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	int ret;

	// Find the end-point partition node in the hypervisor device tree
	const char *s = dt_get_prop_string(node, "fsl,endpoint");
	if (!s) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "process_partition_handle: partition node missing fsl,endpoint\n");
		return 0;
	}

	dt_node_t *endpoint = dt_lookup_alias(hw_devtree, s);
	if (!endpoint) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "process_partition_handle: partition %s does not exist\n", s);
		return 0;
	}

	// Get the guest_t for the partition, or create one if necessary
	guest_t *target_guest = node_to_partition(endpoint);
	if (!target_guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "process_partition_handle: %s is not a partition\n", s);
		return 0;
	}

	// Store the pointer to the target guest in our list of handles
	target_guest->handle.guest = target_guest;

	// Allocate a handle
	int32_t ghandle = alloc_guest_handle(guest, &target_guest->handle);
	if (ghandle < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "guest %s: too many handles\n", guest->name);
		return ghandle;
	}

	// Insert a 'reg' property into the partition-handle node of the
	// guest device tree
	ret = dt_set_prop(node, "reg", &ghandle, sizeof(ghandle));
	if (ret)
		return ret;

	ret = dt_set_prop_string(node, "label", target_guest->name);
	if (ret)
		return ret;

	// Create special doorbells

	ret = create_sdbell_handle("state-change", guest, node,
	                           target_guest->dbell_state_change);
	if (ret)
		return ret;

	ret = create_sdbell_handle("watchdog-expiration", guest, node,
	                           target_guest->dbell_watchdog_expiration);
	if (ret)
		return ret;

	ret = create_sdbell_handle("reset-request", guest, node,
	                           target_guest->dbell_restart_request);
	if (ret)
		return ret;

	return 0;
}

static int register_gcpu_with_guest(guest_t *guest, const uint32_t *cpus,
                                    int len)
{
	int gpir = get_gcpu_num(cpus, len, mfspr(SPR_PIR));
	assert(gpir >= 0);

	while (!guest->gcpus)
		barrier();
	
	guest->gcpus[gpir] = get_gcpu();
	get_gcpu()->gcpu_num = gpir;
	return gpir;
}

int restart_guest(guest_t *guest)
{
	int ret = 0;
	unsigned int i;

	spin_lock(&guest->state_lock);

	if (guest->state != guest_running)
		ret = ERR_INVALID;
	else
		guest->state = guest_stopping;

	spin_unlock(&guest->state_lock);

	if (!ret)
		for (i = 0; i < guest->cpucnt; i++)
			setgevent(guest->gcpus[i], GEV_RESTART);

	return ret;
}

/* Process configuration options in the hypervisor's
 * chosen and hypervisor nodes.
 */
static int partition_config(guest_t *guest)
{
	dt_node_t *node;
	int ret;
	
	node = dt_get_subnode(guest->devtree, "hypervisor", 1);
	if (!node)
		return ERR_NOMEM;

	/* guest cache lock mode */
	if (dt_get_prop(node, "fsl,hv-guest-cache-lock", 0))
		guest->guest_cache_lock = 1;

	/* guest debug mode */
	if (dt_get_prop(node, "fsl,hv-guest-debug", 0))
		guest->guest_debug_mode = 1;

	if (mpic_coreint) {
		ret = dt_set_prop(node, "fsl,hv-pic-coreint", NULL, 0);
		if (ret < 0)
			return ret;
	}

	return dt_set_prop(node, "fsl,hv-partition-label",
	                   guest->name, strlen(guest->name) + 1);
}

uint32_t start_guest_lock;

guest_t *node_to_partition(dt_node_t *partition)
{
	int i, ret;
	char *name;

	// Verify that 'partition' points to a compatible node
	if (!dt_node_is_compatible(partition, "fsl,hv-partition")) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: invalid partition node", __FUNCTION__);
		return NULL;
	}

	spin_lock(&start_guest_lock);
	
	for (i = 0; i < last_lpid; i++) {
		assert(guests[i].lpid == i + 1);
		if (guests[i].partition == partition)
			break;
	}
	
	if (i == last_lpid) {
		if (last_lpid >= MAX_PARTITIONS) {
			spin_unlock(&start_guest_lock);
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "node_to_partition: too many partitions\n");
			return NULL;
		}

		// We create the special doorbells here, instead of in
		// init_guest_primary(), because it guarantees that the
		// doorbells will be created for all partitions before the
		// managers start looking for their managed partitions.
		ret = create_guest_special_doorbells(&guests[i]);
		if (ret < 0) {
			spin_unlock(&start_guest_lock);
			return NULL;
		}

		name = dt_get_prop_string(partition, "label");
		if (!name) {
			/* If no label, use the partition node path. */
			name = malloc(MAX_PATH);
			if (!name) {
				spin_unlock(&start_guest_lock);
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "node_to_partition: out of memory\n");
				return NULL;
			}

			ret = dt_get_path(partition, name, MAX_PATH);
			if (ret > MAX_PATH)
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "node_to_partition: path name too long\n");
		}

		guests[i].name = name;
		guests[i].state = guest_starting;
		guests[i].partition = partition;
		guests[i].lpid = ++last_lpid;
	}
	
	spin_unlock(&start_guest_lock);
	return &guests[i];
}

static void guest_core_init(guest_t *guest)
{
	register_t msrp = 0;

	/* Reset the timer control register, reset the watchdog state, and
	 * clear all pending timer interrupts.  This ensures that timers won't
	 * carry over a partition restart.
	 */
	mtspr(SPR_TCR, 0);
	mtspr(SPR_TSR, TSR_ENW | TSR_DIS | TSR_FIS | TSR_WIS);

	mtspr(SPR_MAS5, MAS5_SGS | guest->lpid);
	mtspr(SPR_PID, 0);

	if (!guest->guest_cache_lock)
		msrp |= MSRP_UCLEP;
	if (!guest->guest_debug_mode)
		msrp |= MSRP_DEP;

	mtspr(SPR_MSRP, msrp);
}

static void start_guest_primary_nowait(void)
{
	register register_t r3 asm("r3");
	register register_t r4 asm("r4");
	register register_t r5 asm("r5");
	register register_t r6 asm("r6");
	register register_t r7 asm("r7");
	register register_t r8 asm("r8");
	register register_t r9 asm("r9");
	guest_t *guest = get_gcpu()->guest; 
	int ret, i;
	
	disable_critint();

	if (cpu->ret_user_hook)
		return;

	assert(guest->state == guest_starting);

	guest_core_init(guest);
	reset_spintbl(guest);

#ifdef CONFIG_GDB_STUB
	gdb_stub_init();
#endif

	void *fdt = malloc(guest->dtb_window_len);
	if (!fdt) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: cannot allocate %llu bytes for dtb window\n",
		         __func__, (unsigned long long)guest->dtb_window_len);

		return;
	}

	ret = flatten_dev_tree(guest->devtree, fdt, guest->dtb_window_len);
	if (ret < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: cannot unflatten dtb\n", __func__);

		return;
	}

	assert(fdt_totalsize(fdt) <= guest->dtb_window_len);

	ret = copy_to_gphys(guest->gphys, guest->dtb_gphys,
	                    fdt, fdt_totalsize(fdt));
	if (ret != fdt_totalsize(fdt)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: cannot copy device tree to guest\n", __func__);
		return;
	}

	guest_set_tlb1(0, MAS1_VALID | (TLB_TSIZE_1G << MAS1_TSIZE_SHIFT) |
	                  MAS1_IPROT, 0, 0, TLB_MAS2_MEM, TLB_MAS3_KERN);

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "branching to guest %s, %d cpus\n", guest->name, guest->cpucnt);

	assert(guest->active_cpus == 0);
	enable_critint();

	for (i = 1; i < guest->cpucnt; i++) {
		if (!guest->gcpus[i]) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			         "guest %s waiting for cpu %d...\n", guest->name, i);
		
			while (!guest->gcpus[i]) {
				if (cpu->ret_user_hook) {
					disable_critint();
					return;
				}

				barrier();
			}
		}

		setgevent(guest->gcpus[i], GEV_START);
	}

	atomic_add(&guest->active_cpus, 1);

	while (guest->active_cpus != guest->cpucnt)
		barrier();

	disable_critint();

	guest->state = guest_running;
	smp_mbar();
	send_doorbells(guest->dbell_state_change);
	cpu->traplevel = 0;

	if (dt_get_prop(guest->partition, "fsl,hv-dbg-wait-at-start", 0)
	    && guest->stub_ops && guest->stub_ops->wait_at_start_hook)
		guest->stub_ops->wait_at_start_hook(guest->entry, MSR_GS);

	// FIXME: This isn't exactly ePAPR compliant.  For starters, we only
	// map 1GiB, so we don't support loading/booting an OS above that
	// address.  Also, we pass the guest physical address even though it
	// should be a guest virtual address, but since we program the TLBs
	// such that guest virtual == guest physical at boot time, this works. 

	r3 = guest->dtb_gphys;
	r4 = 0;
	r5 = 0;
	r6 = 0x45504150; // ePAPR Magic for Book-E
	r7 = 1 << 30;  // 1GB - This must match the TLB_TSIZE_xx value above
	r8 = 0;
	r9 = 0;

	asm volatile("mtsrr0 %0; mtsrr1 %1; rfi" : :
		     "r" (guest->entry), "r" (MSR_GS),
		     "r" (r3), "r" (r4), "r" (r5), "r" (r6), "r" (r7),
		     "r" (r8), "r" (r9)
		     : "memory");

	BUG();
}

static void start_guest_primary(void)
{
	guest_t *guest = get_gcpu()->guest; 
	int ret;

	enable_critint();

	if (cpu->ret_user_hook)
		return;

	assert(guest->state == guest_starting);

	ret = load_image(guest);
	if (ret <= 0) {
		guest->state = guest_stopped;

		/* No hypervisor-loadable image; wait for a manager to start us. */
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "Guest %s waiting for manager start\n", guest->name);

		// Notify the manager(s) that it needs to load images and
		// start this guest.
		send_doorbells(guest->dbell_restart_request);

		return;
	}

	start_guest_primary_nowait();
}

static void start_guest_secondary(void)
{
	register register_t r3 asm("r3");
	register register_t r4 asm("r4");
	register register_t r5 asm("r5");
	register register_t r6 asm("r6");
	register register_t r7 asm("r7");
	register register_t r8 asm("r8");
	register register_t r9 asm("r9");

	unsigned long page;
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	int pir = mfspr(SPR_PIR);
	int gpir = gcpu->gcpu_num;

	enable_critint();
		
	mtspr(SPR_GPIR, gpir);

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "cpu %d/%d spinning on table...\n", pir, gpir);

	atomic_add(&guest->active_cpus, 1);

	while (guest->spintbl[gpir].addr_lo & 1) {
		asm volatile("dcbi 0, %0" : : "r" (&guest->spintbl[gpir]) : "memory");
		asm volatile("dcbi 0, %0" : : "r" (&guest->spintbl[gpir + 1]) : "memory");
		smp_mbar();

		if (cpu->ret_user_hook)
			break;
	}

	disable_critint();
	if (cpu->ret_user_hook)
		return;

	guest_core_init(guest);

#ifdef CONFIG_GDB_STUB
	gdb_stub_init();
#endif
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "secondary %d/%d spun up, addr %lx\n",
	         pir, gpir, guest->spintbl[gpir].addr_lo);

	if (guest->spintbl[gpir].pir != gpir)
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "WARNING: cpu %d (guest cpu %d) changed spin-table "
		         "PIR to %ld, ignoring\n",
		         pir, gpir, guest->spintbl[gpir].pir);

	/* Mask for 256M mapping */
	page = ((((uint64_t)guest->spintbl[gpir].addr_hi << 32) |
		guest->spintbl[gpir].addr_lo) & ~0xfffffff) >> PAGE_SHIFT;

	guest_set_tlb1(0, MAS1_VALID | (TLB_TSIZE_1G << MAS1_TSIZE_SHIFT) |
	                  MAS1_IPROT, page, page, TLB_MAS2_MEM, TLB_MAS3_KERN);

	if (dt_get_prop(guest->partition, "fsl,hv-dbg-wait-at-start", 0)
	    && guest->stub_ops && guest->stub_ops->wait_at_start_hook)
		guest->stub_ops->wait_at_start_hook(guest->entry, MSR_GS);

	r3 = guest->spintbl[gpir].r3_lo;   // FIXME 64-bit
	r4 = 0;
	r5 = 0;
	r6 = 0;
	r7 = 1 << 30;  // 1GB - This must match the TLB_TSIZE_xx value above
	r8 = 0;
	r9 = 0;

	cpu->traplevel = 0;
	asm volatile("mtsrr0 %0; mtsrr1 %1; rfi" : :
	             "r" (guest->spintbl[gpir].addr_lo), "r" (MSR_GS),
	             "r" (r3), "r" (r4), "r" (r5), "r" (r6), "r" (r7),
		     "r" (r8), "r" (r9)
	             : "memory");

	BUG();
}

void start_core(trapframe_t *regs)
{
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG, "start core %lu\n", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;

	assert(guest->state == guest_starting);

	if (gcpu == guest->gcpus[0])
		start_guest_primary_nowait();
	else
		start_guest_secondary();

	wait_for_gevent(regs);
}

void start_wait_core(trapframe_t *regs)
{
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "start wait core %lu\n", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	assert(gcpu == guest->gcpus[0]);
	assert(guest->state == guest_starting);

	start_guest_primary();

	assert(guest->state != guest_running);
	wait_for_gevent(regs);
}

void do_stop_core(trapframe_t *regs, int restart)
{
	int i;
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "%s core %lu\n", restart ? "restart" : "stop", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	assert(guest->state == guest_stopping);

	guest_reset_tlb();

	if (atomic_add(&guest->active_cpus, -1) == 0) {
		for (i = 0; i < MAX_HANDLES; i++) {
			handle_t *h = guest->handles[i];

			if (h && h->ops && h->ops->reset)
				h->ops->reset(h);
		}

		if (restart) {
			guest->state = guest_starting;
			setgevent(guest->gcpus[0], GEV_START_WAIT);
		} else {
			guest->state = guest_stopped;
			send_doorbells(guest->dbell_state_change);
		}
	}

	mpic_reset_core();
	memset(&gcpu->gdbell_pending, 0,
	       sizeof(gcpu_t) - offsetof(gcpu_t, gdbell_pending));

	wait_for_gevent(regs);
}

void stop_core(trapframe_t *regs)
{
	do_stop_core(regs, 0);
}

int stop_guest(guest_t *guest)
{
	unsigned int i, ret = 0;
	spin_lock(&guest->state_lock);

	if (guest->state != guest_running)
		ret = ERR_INVALID;
	else
		guest->state = guest_stopping;

	spin_unlock(&guest->state_lock);
	
	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], GEV_STOP);

	return ret;
}

int start_guest(guest_t *guest)
{
	int ret = 0;
	spin_lock(&guest->state_lock);

	if (guest->state != guest_stopped)
		ret = ERR_INVALID;
	else
		guest->state = guest_starting;

	spin_unlock(&guest->state_lock);
	
	if (ret)
		return ret;

	setgevent(guest->gcpus[0], GEV_START);
	return ret;
}

static int init_guest_primary(guest_t *guest, const uint32_t *cpus,
                              int cpus_len)
{
	int ret;
	dt_prop_t *prop;
	const uint32_t *propdata;
	int gpir;

	/* count number of cpus for this partition and alloc data struct */
	guest->cpucnt = count_cpus(cpus, cpus_len);
	guest->gcpus = alloc(sizeof(long) * guest->cpucnt, sizeof(long));
	if (!guest->gcpus)
		goto nomem;

	gpir = register_gcpu_with_guest(guest, cpus, cpus_len);
	assert(gpir == 0);

	prop = dt_get_prop(guest->partition, "fsl,hv-dtb", 0);
	if (!prop) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: guest missing property fsl,hv-dtb\n");
		return ERR_BADTREE;
	}

	guest->devtree = unflatten_dev_tree(prop->data);
	if (!guest->devtree) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: Can't unflatten guest device tree\n");
		return ERR_BADTREE;
	}

	ret = get_addr_format_nozero(guest->devtree, &guest->naddr, &guest->nsize);
	if (ret < 0)
		goto fail;

	prop = dt_get_prop(guest->partition, "fsl,hv-dtb-window", 0);
	if (!prop) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: Guest missing property fsl,hv-dtb-window\n");
		return ERR_BADTREE;
	}

	if (prop->len != (guest->naddr + guest->nsize) * 4) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: Invalid property len for fsl,hv-dtb-window\n");
		return ERR_BADTREE;
	}

	propdata = prop->data;
	guest->dtb_gphys = int_from_tree(&propdata, guest->naddr);
	guest->dtb_window_len = int_from_tree(&propdata, guest->nsize);
	
	ret = dt_set_prop(guest->devtree, "fsl,hv-version", hv_version, 16);
	if (ret < 0)
		goto fail;

	guest->gphys = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	guest->gphys_rev = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	if (!guest->gphys || !guest->gphys_rev)
		goto nomem;

	ret = create_guest_spin_table(guest, cpus, cpus_len);
	if (ret < 0)
		goto fail;

	send_dbell_partition_init(guest);
	recv_dbell_partition_init(guest);

	ret = dt_for_each_compatible(guest->devtree, "fsl,hv-partition-handle",
	                             process_partition_handle, guest);
	if (ret < 0)
		goto fail;

	ret = partition_config(guest);
	if (ret < 0)
		goto fail;

#ifdef CONFIG_BYTE_CHAN
	byte_chan_partition_init(guest);
#endif

	vmpic_partition_init(guest);

#ifdef CONFIG_DEVICE_VIRT
	list_init(&guest->vf_list);
#endif

	ret = map_guest_reg_all(guest);
	if (ret < 0)
		goto fail;

#ifdef CONFIG_PAMU
	pamu_partition_init(guest);
#endif

	// Get the watchdog timeout options
	prop = dt_get_prop(guest->partition, "fsl,hv-wd-mgr-notify", 0);
	guest->wd_notify = !!prop;

	start_guest_primary();
	return 0;

fail:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "init_guest_primary: error %d\n", ret);

	return ret;

nomem:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "init_guest_primary: out of memory\n");

	return ERR_NOMEM;
}

typedef struct init_guest_ctx {
	guest_t *guest;
	dt_prop_t *cpulist;
} init_guest_ctx_t;

static int init_guest_one(dt_node_t *node, void *arg)
{
	init_guest_ctx_t *ctx = arg;
	int pir = mfspr(SPR_PIR);
	guest_t *guest;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "fsl,hv-cpus", 0);
	if (!prop) {
		char buf[MAX_PATH];
		dt_get_path(node, buf, sizeof(buf));
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest: No fsl,hv-cpus in guest %s\n", buf);
		return 0;
	}

	if (!cpu_in_cpulist(prop->data, prop->len, pir))
		return 0;

	guest = node_to_partition(node);
	if (!guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest: node_to_partition failed\n");
		return ERR_NOMEM;
	}

	if (ctx->guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest: extra guest %s on core %d\n", guest->name, pir);
		return 0;
	}

	ctx->guest = guest;
	ctx->cpulist = prop;
	return 0;
} 

__attribute__((noreturn)) void init_guest(void)
{
	int pir = mfspr(SPR_PIR);
	init_guest_ctx_t ctx = {};

	dt_for_each_compatible(hw_devtree, "fsl,hv-partition",
	                       init_guest_one, &ctx);

	if (ctx.guest) {
		const uint32_t *cpus = ctx.cpulist->data;
		int cpus_len = ctx.cpulist->len;
		guest_t *guest = ctx.guest;

		get_gcpu()->guest = guest;

		mtspr(SPR_LPIDR, guest->lpid);

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "guest at %s on core %d\n", guest->name, pir);

		if (pir == cpus[0]) {
			/* Boot CPU */
			init_guest_primary(guest, cpus, cpus_len);
		} else {
			register_gcpu_with_guest(guest, cpus, cpus_len);
		}
	}

	/* Like wait_for_gevent(), but without a
	 * stack frame to return on.
	 */
	register_t new_r1 = (register_t)&cpu->kstack[KSTACK_SIZE - FRAMELEN];

	/* Terminate the callback chain. */
	cpu->kstack[KSTACK_SIZE - FRAMELEN] = 0;

	get_gcpu()->waiting_for_gevent = 1;

	asm volatile("mtsrr0 %0; mtsrr1 %1; mr %%r1, %2; rfi" : :
	             "r" (wait_for_gevent_loop),
	             "r" (mfmsr() | MSR_CE),
	             "r" (new_r1) : "memory");
	BUG();
}

/* This is a hack to find an upper bound of the hypervisor's memory.
 * It assumes that the main memory segment of each partition will
 * be listed in its physaddr map, and that no shared memory region
 * appears below the lowest private guest memory.
 *
 * Once we have explicit coherency domains, we could perhaps include
 * one labelled for hypervisor use, and maybe one designated for dynamic
 * memory allocation.
 */
phys_addr_t find_lowest_guest_phys(void *fdt)
{
	int off = -1, ret;
	phys_addr_t low = (phys_addr_t)-1;
	const uint32_t *prop;
	const void *gtree;

	while (1) {
		uint32_t gnaddr, gnsize;
		int entry_len;
		int i = 0;
	
		off = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-partition");
		if (off < 0)
			return low;

		gtree = fdt_getprop(fdt, off, "fsl,hv-dtb", &ret);
		if (!gtree)
			continue;

		ret = fdt_get_addr_format(fdt, 0, &gnaddr, &gnsize);
		if (ret < 0)
			continue;

		prop = fdt_getprop(fdt, off, "fsl,hv-physaddr-map", &ret);
		if (!prop)
			continue;
		
		entry_len = gnaddr + rootnaddr + gnsize;
		while (i + entry_len <= ret >> 2) {
			phys_addr_t real;
			
			real = prop[i + gnaddr];
			
			if (gnaddr == 2 && sizeof(phys_addr_t) > 4) {
				real <<= 32;
				real += prop[i + gnaddr + 1];
			}
			
			if (real < low)
				low = real;

			i += entry_len;
		}
	}
}
