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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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
#include <libos/spr.h>
#include <libos/io.h>
#include <libos/bitops.h>
#include <libos/mpic.h>

#include <hv.h>
#include <paging.h>
#include <timers.h>
#include <byte_chan.h>
#include <vmpic.h>
#include <ipi_doorbell.h>
#include <devtree.h>
#include <errors.h>
#include <elf.h>
#include <events.h>
#include <doorbell.h>

#define MAX_PARTITIONS 8

guest_t guests[MAX_PARTITIONS];
static unsigned long last_lpid;

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

static void map_guest_range(guest_t *guest, physaddr_t gaddr,
                            physaddr_t addr, physaddr_t size)
{
	unsigned long grpn = gaddr >> PAGE_SHIFT;
	unsigned long rpn = addr >> PAGE_SHIFT;
	unsigned long pages = (gaddr + size -
	                       (grpn << PAGE_SHIFT) +
	                       (PAGE_SIZE - 1)) >> PAGE_SHIFT;

//	printf("cpu%ld mapping guest %lx to real %lx, %lx pages\n",
//	       mfspr(SPR_PIR), grpn, rpn, pages);

	vptbl_map(guest->gphys, grpn, rpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, grpn, pages, PTE_ALL, PTE_PHYS_LEVELS);
}

static int map_guest_reg_one(guest_t *guest, int node, int partition,
                             const uint32_t *reg)
{
	physaddr_t gaddr, size;
	uint32_t addrbuf[MAX_ADDR_CELLS], rootnaddr = 0;
	physaddr_t rangesize, addr, offset = 0;
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
//	printf("found reg in %s\n", path);

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
	uint32_t spin_addr;
	int ret, i;
	
	guest->spintbl = alloc(PAGE_SIZE, PAGE_SIZE);
	if (!guest->spintbl)
		return ERR_NOMEM;

	/* FIXME: hardcoded cache line size */
	for (i = 0; i < num_cpus * sizeof(struct boot_spin_table); i += 32)
		asm volatile("dcbf 0, %0" : : "r" ((unsigned long)guest->spintbl + i ) :
		             "memory");

	rpn = ((physaddr_t)(unsigned long)guest->spintbl - PHYSBASE) >> PAGE_SHIFT;

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
		                  "cpu-release-addr", &spin_addr, 4);
		if (ret < 0)
			goto fail;

		printf("cpu-release-addr of CPU%u: %x\n", cpu, spin_addr);
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
#define FLASH_ADDR      0xef000000
#define FLASH_SIZE      (16 * 1024 * 1024)

/*
 * Map flash into memory and return a hypervisor virtual address for the
 * given physical address.
 *
 * @phys: physical address inside flash space
 *
 * The TLB entry created by this function is temporary.
 */
static void *map_flash(physaddr_t phys)
{
	/* Make sure 'phys' points to flash */
	if ((phys < FLASH_ADDR) || (phys > (FLASH_ADDR + FLASH_SIZE - 1))) {
		printf("%s: phys addr %llx is out of range\n", __FUNCTION__, phys);
		return NULL;
	}

	/*
	 * There is no permanent TLB entry for flash, so we create a temporary
	 * one here. TEMPTLB2 is a slot reserved for temporary mappings.  We
	 * can't use TEMPTLB1, because map_gphys() is using that one.
	 */

	tlb1_set_entry(TEMPTLB2, (unsigned long) temp_mapping[1],
		       FLASH_ADDR, TLB_TSIZE_16M, TLB_MAS2_MEM,
		       TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	return temp_mapping[1] + (phys - FLASH_ADDR);
}

/**
 * Load a binary or ELF image into guest memory
 *
 * @guest: guest data structure
 * @image_phys: real physical address of the image
 * @guest_phys: guest physical address to load the image to
 * @length: size of the image, optional if image is an ELF
 */
static int load_image_from_flash(guest_t *guest, physaddr_t image_phys, physaddr_t guest_phys, size_t length)
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
		printf("guest %s: missing or invalid fsl,hv-image-size property\n",
			guest->name);
		return ERR_BADTREE;
	}

	printf("guest %s: loading binary image from flash\n", guest->name);

	if (copy_to_gphys(guest->gphys, guest_phys, image, length) != length)
		return ERR_BADADDR;

	guest->entry = guest_phys;

	return 0;
}

/* Returns 1 if the image is loaded successfully, 0 if no image,
 * negative on error.
 */
static int load_image(guest_t *guest)
{
	/*
	 * If an ELF image exists, load it.  This must be called after
	 * map_guest_reg_all(), because that function creates some of the TLB
	 * mappings we need.
	 */
	int node = guest->partition;
	int ret;

	if (!fdt_get_property(fdt, node, "fsl,hv-loaded-images", &ret)) {
		if (ret == -FDT_ERR_NOTFOUND)
			return 0;

		return ret;
	}
	
	const uint64_t *image_addr;
	const uint64_t *guest_addr;
	size_t length = (size_t) -1;
	const uint32_t *iprop;

	image_addr = fdt_getprop(fdt, node, "fsl,hv-image-src-addr", &ret);
	if (!image_addr) {
		printf("guest %s: no fsl,image-src-addr property\n", guest->name);
		return ret;
	}

	guest_addr = fdt_getprop(fdt, node, "fsl,hv-image-gphys-addr", &ret);
	if (!guest_addr) {
		printf("guest %s: no fsl,image-dest-offset property\n", guest->name);
		return ret;
	}

	/* The size of the image is only required for binary images,
	   not ELF images. */
	iprop = fdt_getprop(fdt, node, "fsl,hv-image-size", NULL);
	if (iprop)
		length = *iprop;

	ret = load_image_from_flash(guest, *image_addr, *guest_addr, length);
	if (ret < 0) {
		printf("guest %s: could not load image\n", guest->name);
		return ret;
	}
	
	return 1;
}
		
static int process_guest_devtree(guest_t *guest, int partition,
                                 const uint32_t *cpulist,
                                 int cpulist_len)
{
	int ret;
	int gfdt_size = fdt_totalsize(fdt);
	void *guest_origtree;

	guest_origtree = fdt_getprop_w(fdt, partition, "fsl,dtb", &ret);
	if (!guest_origtree) {
		printf("guest %s: no fsl,dtb property\n", guest->name);
		ret = -FDT_ERR_NOTFOUND;
		goto fail;
	}
	
	gfdt_size += ret;
	
	guest->devtree = alloc(gfdt_size, 16);
	if (!guest->devtree) {
		ret = ERR_NOMEM;
		goto fail;
	}

	ret = fdt_open_into(guest_origtree, guest->devtree, gfdt_size);
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

	/* Look for partition-handle nodes.  These are nodes that are
	   "fsl,hv-partition"-compatible and are children of a
	   "fsl,hv-handles"-compatible node. */

	// Get a pointer to the /handles node
	int off = fdt_node_offset_by_compatible(guest_origtree, -1, "fsl,hv-handles");

	while (off > 0) {
		// get a pointer to the first/next partition-handle node
		off = fdt_node_offset_by_compatible(guest_origtree, off, "fsl,hv-partition");
		if (off < 0)
			break;

		// Find the partition node in the hypervisor device tree

		const char *s = fdt_getprop(guest_origtree, off, "fsl,endpoint", &ret);
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
		ret = fdt_setprop(guest_origtree, off, "reg", &ghandle, sizeof(ghandle));
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

static int start_guest_primary_nowait(void)
{
	guest_t *guest = get_gcpu()->guest; 
	int i;
	
	assert(!(mfmsr() & MSR_CE));

	assert(guest->state == guest_starting);
	reset_spintbl(guest);

	/* FIXME: append device tree to image, or use address provided by
	 * the device tree, or something sensible like that.
	 */
	int ret = copy_to_gphys(guest->gphys, 0x00f00000,
	                        guest->devtree, fdt_totalsize(guest->devtree));
	if (ret != fdt_totalsize(guest->devtree)) {
		printf("Couldn't copy device tree to guest %s, %d\n",
		       guest->name, ret);
		return ERR_BADADDR;
	}

	guest_set_tlb1(0, MAS1_VALID | (TLB_TSIZE_256M << MAS1_TSIZE_SHIFT) |
	                  MAS1_IPROT, 0, 0, TLB_MAS2_MEM, TLB_MAS3_KERN);

	printf("branching to guest %s, %d cpus\n", guest->name, guest->cpucnt);
	guest->active_cpus = guest->cpucnt;
	guest->state = guest_running;
	smp_mbar();

	for (i = 1; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], GEV_START);

	// FIXME: This isn't exactly ePAPR compliant.  For starters, we only
	// map 256MB, so we don't support loading/booting an OS above that
	// address.  Also, we pass the guest physical address even though it
	// should be a guest virtual address, but since we program the TLBs such
	// that guest virtual == guest physical at boot time, this works.
	// Also, the IMA needs to be put into r7.

	asm volatile("mfmsr %%r3; oris %%r3, %%r3, 0x1000;"
	             "li %%r4, 0; li %%r5, 0; li %%r6, 0; li %%r7, 0;"
	             "mtsrr0 %0; mtsrr1 %%r3; lis %%r3, 0x00f0; rfi" : :
	             "r" (guest->entry) :
	             "r3", "r4", "r5", "r6", "r7", "r8");

	BUG();
}

static void start_guest_primary(void)
{
	guest_t *guest = get_gcpu()->guest; 
	int ret;

	assert(!(mfmsr() & MSR_CE));
	if (cpu->ret_user_hook)
		return;

	spin_lock(&guest->lock);
	assert(guest->state == guest_starting);

	ret = load_image(guest);
	if (ret < 0) {
		guest->state = guest_stopped;

		// TODO: send signal to manager
		spin_unlock(&guest->lock);
		return;
	}

	spin_unlock(&guest->lock);

	if (ret > 0)
		start_guest_primary_nowait();
	
	if (ret == 0) {
		/* No hypervisor-loadable image; wait for a manager to start us. */
		printf("Guest %s waiting for manager start\n", guest->name);
		return;
	}
	
	guest->state = guest_starting;
	start_guest_primary_nowait();
}

static void start_guest_secondary(void)
{
	register register_t r3 asm("r3");
	register register_t r4 asm("r4");
	register register_t r6 asm("r6");
	register register_t r7 asm("r7");
	register_t msr;

	unsigned long page;
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	int pir = mfspr(SPR_PIR);
	int gpir = gcpu->gcpu_num;

	enable_critint();
		
	mtspr(SPR_GPIR, gpir);

	printf("cpu %d/%d spinning on table...\n", pir, gpir);

	msr = mfmsr() | MSR_GS;

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

	printf("secondary %d/%d spun up, addr %lx\n", pir, gpir, guest->spintbl[gpir].addr_lo);

	if (guest->spintbl[gpir].pir != gpir)
		printf("WARNING: cpu %d (guest cpu %d) changed spin-table "
		       "PIR to %ld, ignoring\n",
		       pir, gpir, guest->spintbl[gpir].pir);

	/* Mask for 256M mapping */
	page = ((((uint64_t)guest->spintbl[gpir].addr_hi << 32) |
		guest->spintbl[gpir].addr_lo) & ~0xfffffff) >> PAGE_SHIFT;

	guest_set_tlb1(0, MAS1_VALID | (TLB_TSIZE_256M << MAS1_TSIZE_SHIFT) |
	                  MAS1_IPROT, page, page, TLB_MAS2_MEM, TLB_MAS3_KERN);

	r3 = guest->spintbl[gpir].r3_lo;   // FIXME 64-bit
	r6 = 0;
	// FIXME: set r7 to IMA size

	asm volatile("mtsrr0 %0; mtsrr1 %1; li %%r5, 0; rfi" : :
	             "r" (guest->spintbl[gpir].addr_lo), "r" (msr),
	             "r" (r3), "r" (r4), "r" (r6), "r" (r7) : "r5", "memory");

	BUG();
}

void start_core(trapframe_t *regs)
{
	printf("start core %lu\n", mfspr(SPR_PIR));

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
	printf("start_wait core %lu\n", mfspr(SPR_PIR));

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
	printf("%s core %lu\n", restart ? "restart" : "stop", mfspr(SPR_PIR));

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
	register_t saved = spin_lock_critsave(&guest->lock);

	if (guest->state != guest_running)
		ret = -ERR_INVALID;
	else
		guest->state = guest_stopping;

	spin_unlock_critsave(&guest->lock, saved);
	
	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], GEV_STOP);

	return ret;
}

int start_guest(guest_t *guest)
{
	int ret = 0;
	register_t saved = spin_lock_critsave(&guest->lock);

	if (guest->state != guest_stopped)
		ret = -ERR_INVALID;
	else
		guest->state = guest_starting;

	spin_unlock_critsave(&guest->lock, saved);
	
	if (ret)
		return ret;

	setgevent(guest->gcpus[0], GEV_START);
	return ret;
}

/* Process configuration options in the hypervisor's
 * chosen node.
 */
static void partition_config(guest_t *guest)
{
	int chosen = fdt_subnode_offset(fdt, 0, "chosen");
	if (chosen < 0) {
		/* set defaults */
		guest->coreint = 1;
		return;
	}

	int len;
	const uint32_t *coreint = fdt_getprop(fdt, chosen, "fsl,hv-pic-coreint", &len);
	if (coreint) {
		guest->coreint = *coreint;
	} else {
		guest->coreint = 1;
	}
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

		cpus = fdt_getprop(fdt, off, "fsl,cpus", &len);
		if (!cpus) {
			ret = fdt_get_path(fdt, off, buf, sizeof(buf));
			printf("No fsl,cpus in guest %s\n", buf);
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
		guest->gcpus = alloc(sizeof(long) * guest->cpucnt, __alignof__(long));
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

		disable_critint();
		guest->state = guest_starting;
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

void *map_gphys(int tlbentry, pte_t *tbl, physaddr_t addr,
                void *vpage, size_t *len, int maxtsize, int write)
{
	size_t offset, bytesize;
	unsigned long attr;
	unsigned long rpn;
	physaddr_t physaddr;
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
	physaddr = (physaddr_t)rpn << PAGE_SHIFT;

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
size_t copy_to_gphys(pte_t *tbl, physaddr_t dest, void *src, size_t len)
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
size_t zero_to_gphys(pte_t *tbl, physaddr_t dest, size_t len)
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
size_t copy_from_gphys(pte_t *tbl, void *dest, physaddr_t src, size_t len)
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
size_t copy_between_gphys(pte_t *dtbl, physaddr_t dest,
                          pte_t *stbl, physaddr_t src, size_t len)
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
