/*
 * Guest device tree processing
 *
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

#include <hv.h>
#include <paging.h>
#include <timers.h>
#include <byte_chan.h>
#include <devtree.h>
#include <errors.h>

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

	const uint32_t *hvranges = fdt_getprop(fdt, partition,
	                                       "fsl,hv-ranges", &hvrlen);
	if (!hvranges && hvrlen != -FDT_ERR_NOTFOUND)
		return hvrlen;
	if (!hvranges || hvrlen % 4)
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

		ret = xlate_one(addrbuf, hvranges, hvrlen, 2, 1, 2, 1, &rangesize);
		if (ret == -FDT_ERR_NOTFOUND) {
			// FIXME: It is assumed that if the beginning of the reg is not
			// in hvranges, then none of it is.

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

static int create_guest_spin_table(guest_t *guest)
{
	unsigned long rpn;
	unsigned int num_cpus = guest->cpucnt;
	struct boot_spin_table *spintbl;
	uint32_t spin_addr;
	int ret, i;
	
	spintbl = alloc(PAGE_SIZE, PAGE_SIZE);
	if (!spintbl)
		return ERR_NOMEM;

	for (i = 0; i < num_cpus; i++) {
		spintbl[i].addr = 1;
		spintbl[i].pir = i;
		spintbl[i].r3 = 0;
		spintbl[i].r4 = 0;
		spintbl[i].r7 = 0;
	}

	/* FIXME: hardcoded cache line size */
	for (i = 0; i < num_cpus * sizeof(struct boot_spin_table); i += 32)
		asm volatile("dcbf 0, %0" : : "r" ((unsigned long)spintbl + i ) :
		             "memory");

	rpn = ((physaddr_t)(unsigned long)spintbl - PHYSBASE) >> PAGE_SHIFT;

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

	printf("Setting spintbl on guest %p\n", guest);
	smp_mbar();
	guest->spintbl = spintbl;
	return 0;

fail:
	printf("create_guest_spin_table(%s): libfdt error %d (%s)\n",
	       guest->name, ret, fdt_strerror(ret));

	return ret;
}

#define MAX_PATH 256

static int process_guest_devtree(guest_t *guest, int partition,
                                 const uint32_t *cpulist,
                                 int cpulist_len)
{
	int ret, len;
	int gfdt_size = fdt_totalsize(fdt);
	const void *guest_origtree;

	guest_origtree = fdt_getprop(fdt, partition, "fsl,dtb", &len);
	if (!guest_origtree) {
		printf("guest %s: no fsl,dtb property\n", guest->name);
		ret = -FDT_ERR_NOTFOUND;
		goto fail;
	}
	
	gfdt_size += len;
	
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
	if (ret == 0)
		return 0;

fail:
	printf("error %d (%s) building guest device tree for %s\n",
	       ret, fdt_strerror(ret), guest->name);

	return ret;
}

uint32_t start_guest_lock;

static guest_t *node_to_partition(int partition)
{
	int i;
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
	
	guest->gcpus[gpir] = get_gcpu();
	get_gcpu()->gcpu_num = gpir;
	mtspr(SPR_GPIR, gpir);
	return gpir;
}

void start_guest(void)
{
	int off = -1, found = 0, ret;
	int pir = mfspr(SPR_PIR);
	char buf[MAX_PATH];
	guest_t *guest;
	unsigned long page;
	int gpir;
	
	while (1) {
		off = fdt_node_offset_by_compatible(fdt, off, "fsl,hv-partition");
		if (off < 0)
			break;

		int len;
		const uint32_t *cpus = fdt_getprop(fdt, off, "fsl,cpus", &len);
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
		mtspr(SPR_LPID, guest->lpid);
	
		get_gcpu()->guest = guest;

		if (pir == cpus[0]) {
			int name_len;
		
			// Boot CPU
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
				continue;

			byte_chan_partition_init(guest);

			unsigned long attr;
			unsigned long rpn = vptbl_xlate(guest->gphys,
			                                0x00f00, &attr, PTE_PHYS_LEVELS);

			if (!(attr & PTE_VALID)) {
				printf("No valid guest phys 0x00f00000\n");
				continue;
			}
			
			memcpy((void *)(PHYSBASE + (rpn << PAGE_SHIFT)), guest->devtree,
			       fdt_totalsize(fdt));

			guest_set_tlb1(0, (TLB_TSIZE_256M << MAS1_TSIZE_SHIFT) | MAS1_IPROT,
			               0, 0, TLB_MAS2_MEM, TLB_MAS3_KERN);

			printf("branching to guest\n");

			asm volatile("mfmsr %%r3; oris %%r3, %%r3, 0x1000;"
			             "li %%r4, 0; li %%r5, 0; li %%r6, 0; li %%r7, 0;"
			             "mtsrr0 %0; mtsrr1 %%r3; lis %%r3, 0x00f0; rfi" : :
			             "r" (0 << PAGE_SHIFT) :
	   		          "r3", "r4", "r5", "r6", "r7", "r8");
		} else {
			register register_t r3 asm("r3");
			register register_t r4 asm("r4");
			register register_t r6 asm("r6");
			register register_t r7 asm("r7");

			printf("cpu %d waiting for spintbl on guest %p...\n", pir, guest);
		
			// Wait for the boot cpu...
			while (!guest->spintbl)
				smp_mbar();

			gpir = register_gcpu_with_guest(guest, cpus, len);
			printf("cpu %d/%d spinning on table...\n", pir, gpir);

			while (guest->spintbl[gpir].addr & 1) {
				asm volatile("dcbi 0, %0" : : "r" (&guest->spintbl[gpir]) : "memory");
				asm volatile("dcbi 0, %0" : : "r" (&guest->spintbl[gpir + 1]) : "memory");
				smp_mbar();
			}

			printf("secondary %d/%d spun up, addr %lx\n", pir, gpir, guest->spintbl[gpir].addr);

			if (guest->spintbl[gpir].pir != gpir)
				printf("WARNING: cpu %d (guest cpu %d) changed spin-table "
				       "PIR to %ld, ignoring\n",
				       pir, gpir, guest->spintbl[gpir].pir);

			/* Mask for 256M mapping */
			page = (guest->spintbl[gpir].addr & ~0xfffffff) >> PAGE_SHIFT;

			guest_set_tlb1(0, (TLB_TSIZE_256M << MAS1_TSIZE_SHIFT) | MAS1_IPROT,
			               page, page, TLB_MAS2_MEM, TLB_MAS3_KERN);

			r3 = guest->spintbl[gpir].r3;
			r4 = guest->spintbl[gpir].r4;
			r6 = 0;
			r7 = guest->spintbl[gpir].r7;

			asm volatile("mfmsr %%r5; oris %%r5, %%r5, 0x1000;"
			             "mtsrr0 %0; mtsrr1 %%r5; li %%r5, 0; rfi" : :
			             "r" (guest->spintbl[gpir].addr),
			             "r" (r3), "r" (r4), "r" (r6), "r" (r7) : "r5", "memory");
		}
	}

	return;

nomem:
	printf("out of memory in start_guest\n");
} 
