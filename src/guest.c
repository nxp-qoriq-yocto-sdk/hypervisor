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
#include <events.h>
#include <doorbell.h>

guest_t guests[MAX_PARTITIONS];
unsigned long last_lpid;

static int cpu_in_cpulist(const uint32_t *cpulist, int len, int cpu)
{
	int i;
	for (i = 0; i < len / 4; i += 2) {
		if (cpu >= cpulist[i] && cpu < cpulist[i] + cpulist[i + 1])
			return 1;
	}

	return 0;
}

static int get_gcpu_num(const uint32_t *cpulist, int len, int cpu)
{
	int i;
	int total = 0; 

	for (i = 0; i < len / 4; i += 2) {
		int base = cpulist[i];
		int num = cpulist[i + 1];

		if (cpu >= base && cpu < base + num)
			return total + cpu - base;

		total += num;
	}

	return -1;
}

static int count_cpus(const uint32_t *cpulist, int len)
{
	int i;
	int total = 0;

	for (i = 0; i < len / 4; i += 2)
		total += cpulist[i + 1];	

	return total;
}

static void map_guest_range(guest_t *guest, phys_addr_t gaddr,
                            phys_addr_t addr, phys_addr_t size)
{
	unsigned long grpn = gaddr >> PAGE_SHIFT;
	unsigned long rpn = addr >> PAGE_SHIFT;
	unsigned long pages = (gaddr + size -
	                       (grpn << PAGE_SHIFT) +
	                       (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "cpu%ld mapping guest %lx to real %lx, %lx pages\n",
	         mfspr(SPR_PIR), grpn, rpn, pages);

	vptbl_map(guest->gphys, grpn, rpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, grpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
}

static int map_guest_reg_one(guest_t *guest, int node, int partition,
                             const uint32_t *reg)
{
	phys_addr_t gaddr, size;
	uint32_t addrbuf[MAX_ADDR_CELLS], rootnaddr = 0;
	phys_addr_t rangesize, addr, offset = 0;
	int hvrlen;

	const uint32_t *physaddrmap = fdt_getprop(fdt, partition,
	                                       "fsl,hv-physaddr-map", &hvrlen);
	if (!physaddrmap && hvrlen != -FDT_ERR_NOTFOUND)
		return hvrlen;
	if (!physaddrmap || hvrlen % 4)
		return ERR_BADTREE;

	int ret = xlate_reg_raw(guest->devtree, node, reg, addrbuf,
	                        &rootnaddr, &size);
	if (ret < 0)
		return ret;	

	if (rootnaddr < 0 || rootnaddr > 2)
		return ERR_BADTREE;

	gaddr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

	while (offset < size) {
		val_from_int(addrbuf, gaddr + offset);

		ret = xlate_one(addrbuf, physaddrmap, hvrlen, 2, 1, 2, 1, &rangesize);
		if (ret == -FDT_ERR_NOTFOUND) {
			// FIXME: It is assumed that if the beginning of the reg is not
			// in physaddrmap, then none of it is.

			map_guest_range(guest, gaddr + offset,
			                gaddr + offset, size - offset);
			return 0;
		}
		
		if (ret < 0)
			return ret;

		if (addrbuf[0] || addrbuf[1])
			return ERR_BADTREE;

		addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

		if (rangesize > size - offset)
			rangesize = size - offset;
		
		map_guest_range(guest, gaddr + offset, addr, rangesize);
		offset += rangesize;
	}

	return 0;
}

static int map_guest_reg(guest_t *guest, int node, int partition)
{
	int len, ret;
	uint32_t naddr, nsize;

	const uint32_t *reg = fdt_getprop(guest->devtree, node, "reg", &len);
	if (!reg) {
		if (len == -FDT_ERR_NOTFOUND)
			return 0;
	
		return len;
	}

	if (len % 4)
		return ERR_BADTREE;

	len /= 4;

	char path[256];
	ret = fdt_get_path(guest->devtree, node, path, sizeof(path));
	if (ret < 0)
		return ret;
	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG, "found reg in %s\n", path);

	int parent = fdt_parent_offset(guest->devtree, node);
	if (parent < 0)
		return parent;

	ret = get_addr_format(guest->devtree, parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	if (naddr == 0 || nsize == 0)
		return 0;

	for (int i = 0; i < len; i += naddr + nsize) {
		if (i + naddr + nsize > len)
			return ERR_BADTREE;

		ret = map_guest_reg_one(guest, node, partition, reg + i);
		if (ret < 0 && ret != ERR_NOTRANS)
			return ret;
	}

	return 0;
}

static int map_guest_reg_all(guest_t *guest, int partition)
{
	int node = -1;

	while ((node = fdt_next_node(guest->devtree, node, NULL)) >= 0) {
		int ret = map_guest_reg(guest, node, partition);
		if (ret < 0)
			return ret;
	}
	
	if (node != -FDT_ERR_NOTFOUND)
		return node;

	return 0;
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

static int create_guest_spin_table(guest_t *guest)
{
	unsigned long rpn;
	unsigned int num_cpus = guest->cpucnt;
	uint64_t spin_addr;
	int ret, i;
	
	guest->spintbl = alloc(PAGE_SIZE, PAGE_SIZE);
	if (!guest->spintbl)
		return ERR_NOMEM;

	/* FIXME: hardcoded cache line size */
	for (i = 0; i < num_cpus * sizeof(struct boot_spin_table); i += 32)
		asm volatile("dcbf 0, %0" : : "r" ((unsigned long)guest->spintbl + i ) :
		             "memory");

	rpn = ((phys_addr_t)(unsigned long)guest->spintbl - PHYSBASE) >> PAGE_SHIFT;

	vptbl_map(guest->gphys, 0xfffff, rpn, 1, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, 0xfffff, 1, PTE_ALL, PTE_PHYS_LEVELS);

	int off = 0;
	while (1) {
		uint32_t cpu;
		ret = fdt_node_offset_by_prop_value(guest->devtree, off,
		                                    "device_type", "cpu", 4);
		if (ret == -FDT_ERR_NOTFOUND)
			break;
		if (ret < 0)
			goto fail;

		off = ret;
		const uint32_t *reg = fdt_getprop(guest->devtree, off, "reg", &ret);
		if (!reg)
			goto fail;
		if (ret != 4) {
			printf("create_guest_spin_table(%s): bad cpu reg property\n",
			       guest->name);
			return ERR_BADTREE;
		}

		cpu = *reg;
		if (cpu >= num_cpus) {
			printf("partition %s has no cpu %u\n", guest->name, *reg);
			continue;
		}

		if (cpu == 0) {
			ret = fdt_setprop_string(guest->devtree, off, "status", "okay");
			if (ret < 0)
				goto fail;
			
			continue;
		}

		ret = fdt_setprop_string(guest->devtree, off, "status", "disabled");
		if (ret < 0)
			goto fail;

		ret = fdt_setprop_string(guest->devtree, off,
		                         "enable-method", "spin-table");
		if (ret < 0)
			goto fail;

		spin_addr = 0xfffff000 + cpu * sizeof(struct boot_spin_table);
		ret = fdt_setprop(guest->devtree, off,
		                  "cpu-release-addr", &spin_addr, 8);
		if (ret < 0)
			goto fail;

		printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
		         "cpu-release-addr of CPU%u: %llx\n", cpu, spin_addr);
	}

	return 0;

fail:
	printf("create_guest_spin_table(%s): libfdt error %d (%s)\n",
	       guest->name, ret, fdt_strerror(ret));

	return ret;
}

#define MAX_PATH 256

// FIXME: we're hard-coding the phys address of flash here, it should
// read from the DTS instead.
#define FLASH_ADDR      0xe8000000
#define FLASH_SIZE      (128 * 1024 * 1024)

/*
 * Map flash into memory and return a hypervisor virtual address for the
 * given physical address.
 *
 * @phys: physical address inside flash space
 *
 * The TLB entry created by this function is temporary.
 * FIXME: implement a more generalized I/O mapping mechanism.
 */
static void *map_flash(phys_addr_t phys)
{
	/* Make sure 'phys' points to flash */
	if ((phys < FLASH_ADDR) || (phys > (FLASH_ADDR + FLASH_SIZE - 1))) {
		printf("%s: phys addr %llx is out of range\n", __FUNCTION__, phys);
		return NULL;
	}

	/*
	 * There is no permanent TLB entry for flash, so we create a temporary
	 * one here. TEMPTLB2/3 are slots reserved for temporary mappings.  We
	 * can't use TEMPTLB1, because map_gphys() is using that one.
	 */

	tlb1_set_entry(TEMPTLB2, (unsigned long)temp_mapping[1],
	               FLASH_ADDR, TLB_TSIZE_64M, TLB_MAS2_MEM,
	               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);
	tlb1_set_entry(TEMPTLB3, (unsigned long)temp_mapping[1] + 64 * 1024 * 1024,
	               FLASH_ADDR, TLB_TSIZE_64M, TLB_MAS2_MEM,
	               TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	return temp_mapping[1] + (phys - FLASH_ADDR);
}

/**
 * Load a binary or ELF image into guest memory
 *
 * @guest: guest data structure
 * @image_phys: real physical address of the image
 * @guest_phys: guest physical address to load the image to
 * @length: size of the image, can be -1 if image is an ELF
 *
 * If the image is a plain binary, then 'length' must be the exact size of
 * the image.
 *
 * If the image is an ELF, then 'length' is used only to verify the image
 * data.  To skip verification, set length to -1.
 */
static int load_image_from_flash(guest_t *guest, phys_addr_t image_phys, phys_addr_t guest_phys, size_t length)
{
	void *image = map_flash(image_phys);
	if (!image) {
		printf("guest %s: image source address %llx not in flash\n",
		       guest->name, image_phys);
		return ERR_BADTREE;
	}

	if (is_elf(image)) {
		printf("guest %s: loading ELF image from flash\n", guest->name);
		return load_elf(guest, image, length, guest_phys, &guest->entry);
	}

	/* It's not an ELF image, so it must be a binary. */

	if (!length || (length == (size_t) -1)) {
		printf("guest %s: missing or invalid image size\n",
			guest->name);
		return ERR_BADTREE;
	}

	printf("guest %s: loading binary image from flash\n", guest->name);

	if (copy_to_gphys(guest->gphys, guest_phys, image, length) != length)
		return ERR_BADADDR;

	guest->entry = guest_phys;

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
	const struct {
		uint64_t image_addr;
		uint64_t guest_addr;
		uint32_t length;
	} __attribute__ ((packed)) *table;
	int node = guest->partition;
	int ret, size;

	table = fdt_getprop(fdt, node, "fsl,hv-load-image-table", &size);
	if (!table) {
		/* 'size' will never equal zero if table is NULL */
		if (size == -FDT_ERR_NOTFOUND)
			return 0;

		printf("guest %s: could not read fsl,hv-load-image-table property\n",
			guest->name);
		return size;
	}

	if (!size || (size % sizeof(*table))) {
		printf("guest %s: invalid fsl,hv-load-image-table property\n",
			guest->name);
		return ERR_BADTREE;
	}

	while (size) {
		ret = load_image_from_flash(guest, table->image_addr,
			table->guest_addr, table->length ? table->length : -1);
		if (ret < 0) {
			printf("guest %s: could not load image\n", guest->name);
			return ret;
		}

                table++;
		size -= sizeof(*table);
	}
	
	return 1;
}
		
static const uint32_t hv_version[4] = {
	CONFIG_HV_MAJOR_VERSION,
	CONFIG_HV_MINOR_VERSION,
	FH_API_VERSION,
	FH_API_COMPAT_VERSION
};

static int process_guest_devtree(guest_t *guest, int partition,
                                 const uint32_t *cpulist,
                                 int cpulist_len)
{
	int ret;
	int off = -1;
	uint32_t gfdt_size;
	const void *guest_origtree;
	const uint32_t *prop;

	prop = fdt_getprop(fdt, partition, "fsl,hv-dtb-window", &ret);
	if (!prop)
		goto fail;
	if (ret < 12) {
		ret = ERR_BADTREE;
		goto fail;
	}

	guest->dtb_gphys = ((uint64_t)prop[0] << 32) | prop[1];
	gfdt_size = prop[2];

	guest_origtree = fdt_getprop(fdt, partition, "fsl,hv-dtb", &ret);
	if (!guest_origtree)
		goto fail;
	
	gfdt_size += ret;
	
	guest->devtree = alloc(gfdt_size, 16);
	if (!guest->devtree) {
		ret = ERR_NOMEM;
		goto fail;
	}

	ret = fdt_open_into(guest_origtree, guest->devtree, gfdt_size);
	if (ret < 0)
		goto fail;

	ret = fdt_setprop(guest->devtree, 0, "fsl,hv-version",
	                  hv_version, 16);
	if (ret < 0)
		goto fail;

	guest->gphys = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	guest->gphys_rev = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	if (!guest->gphys || !guest->gphys_rev) {
		ret = ERR_NOMEM;
		goto fail;
	}

	ret = create_guest_spin_table(guest);
	if (ret < 0)
		goto fail;

	ret = map_guest_reg_all(guest, partition);
	if (ret < 0)
		goto fail;

	// Add a 'reg' property to every partition-handle node

	while (1) {
		// get a pointer to the first/next partition-handle node
		off = fdt_node_offset_by_compatible(guest->devtree, off, "fsl,hv-partition-handle");
		if (off < 0)
			break;

		// Find the end-point partition node in the hypervisor device tree

		const char *s = fdt_getprop(guest->devtree, off, "fsl,endpoint", &ret);
		if (!s) {
			printf("guest %s: missing fsl,endpoint property or value\n",
				guest->name);
			goto fail;
		}

		int endpoint = fdt_path_offset(fdt, s);
		if (endpoint < 0) {
			printf("guest %s: partition %s does not exist\n",
				guest->name, s);
			goto fail;
		}

		// Get the guest_t for the partition, or create one if necessary
		guest_t *target_guest = node_to_partition(endpoint);
		if (!target_guest) {
			printf("guest %s: partition %s does not exist\n",
				guest->name, s);
			goto fail;
		}

		// Allocate a handle
		int32_t ghandle = alloc_guest_handle(guest, &target_guest->handle);
		if (ghandle < 0) {
			printf("guest %s: too many handles\n", guest->name);
			goto fail;
		}

		// Store the pointer to the target guest in our list of handles
		guest->handles[ghandle]->guest = target_guest;

		// Insert a 'reg' property into the partition-handle node of the
		// guest device tree
		ret = fdt_setprop(guest->devtree, off, "reg", &ghandle, sizeof(ghandle));
		if (ret) {
			printf("guest %s: could not insert 'reg' property\n", guest->name);
			goto fail;
		}
	}

	return 0;
fail:
	printf("error %d (%s) building guest device tree for %s\n",
	       ret, fdt_strerror(ret), guest->name);

	return ret;
}

uint32_t start_guest_lock;

guest_t *node_to_partition(int partition)
{
	int i;

	// Verify that 'partition' points to a compatible node
	if (fdt_node_check_compatible(fdt, partition, "fsl,hv-partition")) {
		printf("%s: invalid offset %u\n", __FUNCTION__, partition);
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
			printf("too many partitions\n");
		} else {
			guests[i].state = guest_starting;
			guests[i].partition = partition;
			guests[i].lpid = ++last_lpid;
		}
	}
	
	spin_unlock(&start_guest_lock);
	return &guests[i];
}

static int register_gcpu_with_guest(guest_t *guest, const uint32_t *cpus,
                                    int len)
{
	int gpir = get_gcpu_num(cpus, len, mfspr(SPR_PIR));
	assert(gpir >= 0);

	while (!guest->gcpus)
		smp_mbar();
	
	guest->gcpus[gpir] = get_gcpu();
	get_gcpu()->gcpu_num = gpir;
	return gpir;
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
	int i;
	
	disable_critint();

	if (cpu->ret_user_hook)
		return;

	assert(guest->state == guest_starting);
	reset_spintbl(guest);

	/* FIXME: append device tree to image, or use address provided by
	 * the device tree, or something sensible like that.
	 */
	int ret = copy_to_gphys(guest->gphys, guest->dtb_gphys,
	                        guest->devtree, fdt_totalsize(guest->devtree));
	if (ret != fdt_totalsize(guest->devtree)) {
		printf("Couldn't copy device tree to guest %s, %d\n",
		       guest->name, ret);
		return;
	}

	guest_set_tlb1(0, MAS1_VALID | (TLB_TSIZE_1G << MAS1_TSIZE_SHIFT) |
	                  MAS1_IPROT, 0, 0, TLB_MAS2_MEM, TLB_MAS3_KERN);

	printf("branching to guest %s, %d cpus\n", guest->name, guest->cpucnt);
	guest->active_cpus = guest->cpucnt;
	guest->state = guest_running;
	smp_mbar();

	for (i = 1; i < guest->cpucnt; i++) {
		if (!guest->gcpus[i]) {
			enable_critint();
			printf("guest %s waiting for cpu %d...\n", guest->name, i);
		
			while (!guest->gcpus[i]) {
				if (cpu->ret_user_hook)
					break;

				smp_mbar();
			}

			disable_critint();

			if (cpu->ret_user_hook)
				return;
		}

		setgevent(guest->gcpus[i], GEV_START);
	}

	cpu->traplevel = 0;

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

	spin_lock(&guest->lock);
	assert(guest->state == guest_starting);

	ret = load_image(guest);
	if (ret <= 0) {
		guest->state = guest_stopped;

		// TODO: send signal to manager
		spin_unlock(&guest->lock);

		/* No hypervisor-loadable image; wait for a manager to start us. */
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "Guest %s waiting for manager start\n", guest->name);
		return;
	}

	spin_unlock(&guest->lock);
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

	if (gcpu == guest->gcpus[0]) {
		assert(guest->state == guest_starting);
		start_guest_primary_nowait();
	} else {
		assert(guest->state == guest_running);
		start_guest_secondary();
	}

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

	for (i = 0; i < MAX_HANDLES; i++) {
		handle_t *h = guest->handles[i];

		if (h && h->ops && h->ops->reset)
			h->ops->reset(h);
	}

	mpic_reset_core();
	memset(&gcpu->gdbell_pending, 0,
	       sizeof(gcpu_t) - offsetof(gcpu_t, gdbell_pending));

	if (atomic_add(&guest->active_cpus, -1) == 0) {
		if (restart) {
			guest->state = guest_starting;
			setgevent(guest->gcpus[0], GEV_START_WAIT);
		} else {
			guest->state = guest_stopped;
		}
	}

	wait_for_gevent(regs);
}

void stop_core(trapframe_t *regs)
{
	do_stop_core(regs, 0);
}

int stop_guest(guest_t *guest)
{
	unsigned int i, ret = 0;
	spin_lock(&guest->lock);

	if (guest->state != guest_running)
		ret = ERR_INVALID;
	else
		guest->state = guest_stopping;

	spin_unlock(&guest->lock);
	
	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], GEV_STOP);

	return ret;
}

int start_guest(guest_t *guest)
{
	int ret = 0;
	spin_lock(&guest->lock);

	if (guest->state != guest_stopped)
		ret = ERR_INVALID;
	else
		guest->state = guest_starting;

	spin_unlock(&guest->lock);
	
	if (ret)
		return ret;

	setgevent(guest->gcpus[0], GEV_START);
	return ret;
}

/* Process configuration options in the hypervisor's
 * chosen and hypervisor nodes.
 */
static void partition_config(guest_t *guest)
{
	int hv;
	
	hv = fdt_subnode_offset(guest->devtree, 0, "hypervisor");
	if (hv < 0)
		hv = fdt_add_subnode(guest->devtree, 0, "hypervisor");
	if (hv < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "Couldn't create hypervisor node: %d\n", hv);
		return;
	}

	if (mpic_coreint)
		fdt_setprop(guest->devtree, hv, "fsl,hv-pic-coreint",
		            NULL, 0);
}

__attribute__((noreturn)) void init_guest(void)
{
	int off = -1, found = 0, ret;
	int pir = mfspr(SPR_PIR);
	const uint32_t *cpus = NULL;
	char buf[MAX_PATH];
	guest_t *guest = NULL;
	int gpir, len;
	
	while (1) {
		off = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-partition");
		if (off < 0)
			goto wait;

		cpus = fdt_getprop(fdt, off, "fsl,hv-cpus", &len);
		if (!cpus) {
			ret = fdt_get_path(fdt, off, buf, sizeof(buf));
			printf("No fsl,hv-cpus in guest %s\n", buf);
			continue;
		}

		if (!cpu_in_cpulist(cpus, len, pir))
			continue;

		ret = fdt_get_path(fdt, off, buf, sizeof(buf));
		if (ret < 0) {
			printf("start_guest: error %d (%s) getting path at offset %d\n",
			       ret, fdt_strerror(ret), off);
			continue;
		}
		
		if (found) {
			printf("extra guest %s on core %d\n", buf, pir);
			continue;
		}

		printf("guest at %s on core %d\n", buf, pir);
		found = 1;
		
		guest = node_to_partition(off);
		mtspr(SPR_LPIDR, guest->lpid);
	
		get_gcpu()->guest = guest;
		break;
	};

	if (pir == cpus[0]) {
		int name_len;
	
		/* Boot CPU */
		/* count number of cpus for this partition and alloc data struct */
		guest->cpucnt = count_cpus(cpus, len);
		guest->gcpus = alloc(sizeof(long) * guest->cpucnt, sizeof(long));
		if (!guest->gcpus)
			goto nomem;

		gpir = register_gcpu_with_guest(guest, cpus, len);
		assert(gpir == 0);
		
		name_len = strlen(buf) + 1;
		guest->name = alloc(name_len, 1);
		if (!guest->name)
			goto nomem;
		
		memcpy(guest->name, buf, name_len);
		
		ret = process_guest_devtree(guest, off, cpus, len);
		if (ret < 0)
			goto wait;

		partition_config(guest);

#ifdef CONFIG_BYTE_CHAN
		byte_chan_partition_init(guest);
#endif
#ifdef CONFIG_IPI_DOORBELL
		send_dbell_partition_init(guest);
		recv_dbell_partition_init(guest);
#endif

		vmpic_partition_init(guest);
#ifdef CONFIG_PAMU
		pamu_partition_init(guest);
#endif
		start_guest_primary();
	} else {
		gpir = register_gcpu_with_guest(guest, cpus, len);
	}

wait: {
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

nomem:
	printf("out of memory in start_guest\n");
	goto wait;
} 

void *map_gphys(int tlbentry, pte_t *tbl, phys_addr_t addr,
                void *vpage, size_t *len, int maxtsize, int write)
{
	size_t offset, bytesize;
	unsigned long attr;
	unsigned long rpn;
	phys_addr_t physaddr;
	int tsize;

	rpn = vptbl_xlate(tbl, addr >> PAGE_SHIFT, &attr, PTE_PHYS_LEVELS);
	if (!(attr & PTE_VALID) || (attr & PTE_VF))
		return NULL;
	if (write & !(attr & PTE_UW))
		return NULL;

	tsize = attr >> PTE_SIZE_SHIFT;
	if (tsize > maxtsize)
		tsize = maxtsize;

	bytesize = (uintptr_t)tsize_to_pages(tsize) << PAGE_SHIFT;
	offset = addr & (bytesize - 1);
	physaddr = (phys_addr_t)rpn << PAGE_SHIFT;

	if (len)
		*len = bytesize - offset;

	tlb1_set_entry(tlbentry, (unsigned long)vpage, physaddr & ~(bytesize - 1),
	               tsize, TLB_MAS2_MEM, TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	return vpage + offset;
}

/** Copy from a hypervisor virtual address to a guest physical address
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Guest physical address to copy to
 * @param[in] src Hypervisor virtual address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_to_gphys(pte_t *tbl, phys_addr_t dest, void *src, size_t len)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vdest;
		
		vdest = map_gphys(TEMPTLB1, tbl, dest, temp_mapping[0],
		                  &chunk, TLB_TSIZE_16M, 1);
		if (!vdest)
			break;

		if (chunk > len)
			chunk = len;

		memcpy(vdest, src, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}

/** Fill a block of guest physical memory with zeroes
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Guest physical address to copy to
 * @param[in] len Bytes to zero
 * @return number of bytes successfully zeroed
 */
size_t zero_to_gphys(pte_t *tbl, phys_addr_t dest, size_t len)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vdest;

		vdest = map_gphys(TEMPTLB1, tbl, dest, temp_mapping[0],
		                  &chunk, TLB_TSIZE_16M, 1);
		if (!vdest)
			break;

		if (chunk > len)
			chunk = len;

		memset(vdest, 0, chunk);

		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}


/** Copy from a guest physical address to a hypervisor virtual address 
 *
 * @param[in] tbl Guest physical page table
 * @param[in] dest Hypervisor virtual address to copy to
 * @param[in] src Guest physical address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_from_gphys(pte_t *tbl, void *dest, phys_addr_t src, size_t len)
{
	size_t ret = 0;

	while (len > 0) {
		size_t chunk;
		void *vsrc;
		
		vsrc = map_gphys(TEMPTLB1, tbl, src, temp_mapping[0],
		                 &chunk, TLB_TSIZE_16M, 0);
		if (!vsrc)
			break;

		if (chunk > len)
			chunk = len;

		memcpy(dest, vsrc, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
	}

	return ret;
}

/** Copy from a guest physical address to another guest physical address
 *
 * @param[in] dtbl Guest physical page table of the destination
 * @param[in] dest Guest physical address to copy to
 * @param[in] stbl Guest physical page table of the source
 * @param[in] src Guest physical address to copy from
 * @param[in] len Bytes to copy
 * @return number of bytes successfully copied
 */
size_t copy_between_gphys(pte_t *dtbl, phys_addr_t dest,
                          pte_t *stbl, phys_addr_t src, size_t len)
{
	size_t schunk = 0, dchunk = 0, chunk, ret = 0;
	
	/* Initializiations not needed, but GCC is stupid. */
	void *vdest = NULL, *vsrc = NULL;

	while (len > 0) {
		if (!schunk) {
			vsrc = map_gphys(TEMPTLB1, stbl, src, temp_mapping[0],
			                 &schunk, TLB_TSIZE_16M, 0);
			if (!vsrc)
				break;
		}

		if (!dchunk) {
			vdest = map_gphys(TEMPTLB2, dtbl, dest, temp_mapping[1],
			                  &dchunk, TLB_TSIZE_16M, 1);
			if (!vdest)
				break;
		}

		chunk = min(schunk, dchunk);
		if (chunk > len)
			chunk = len;

		memcpy(vdest, vsrc, chunk);

		src += chunk;
		dest += chunk;
		ret += chunk;
		len -= chunk;
		dchunk -= chunk;
		schunk -= chunk;
	}

	return ret;
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
phys_addr_t find_lowest_guest_phys(void)
{
	int off = -1, ret;
	phys_addr_t low = (phys_addr_t)-1;
	const uint32_t *prop;
	const void *gtree;
	uint32_t naddr, nsize;

	ret = get_addr_format(fdt, 0, &naddr, &nsize);
	if (ret < 0)
		return ret;

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

		ret = get_addr_format(gtree, 0, &gnaddr, &gnsize);
		if (ret < 0)
			continue;

		prop = fdt_getprop(fdt, off, "fsl,hv-physaddr-map", &ret);
		if (!prop)
			continue;
		
		entry_len = gnaddr + naddr + gnsize;
		while (i + entry_len <= ret / 4) {
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
