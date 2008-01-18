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

#include <uv.h>
#include <paging.h>
#include <timers.h>

#include <libfdt.h>

guest_t guest;
static unsigned long last_lpid;

static const unsigned long guest_io_pages[] = {
	0xfe11d, TLB_TSIZE_4K, // DUART
	0xfe040, TLB_TSIZE_256K, // MPIC
	0xfe118, TLB_TSIZE_4K, // I2C
	0xfe31c, TLB_TSIZE_16K, // ethernet
};

static int cpu_in_cpulist(const uint32_t *cpulist, int len, int cpu)
{
	int i;
	for (i = 0; i < len / 4; i += 2) {
		if (cpu >= cpulist[i] && cpu < cpulist[i] + cpulist[i + 1])
			return 1;
	}

	return 0;
}

// Error values that will not conflict with FDT errors
#define BADTREE -256
#define NOMEM -257
#define NOTRANS -258

#define MAX_ADDR_CELLS 4
#define MAX_SIZE_CELLS 2

int get_addr_format(const void *tree, int node,
                    uint32_t *naddr, uint32_t *nsize)
{
	*naddr = 2;
	*nsize = 1;

	int len;
	const uint32_t *naddrp = fdt_getprop(tree, node, "#address-cells", &len);
	if (!naddrp) {
		if (len != -FDT_ERR_NOTFOUND)
			return len;
	} else if (len == 4 && *naddrp <= MAX_ADDR_CELLS) {
		*naddr = *naddrp;
	} else {
		return BADTREE;
	}

	const uint32_t *nsizep = fdt_getprop(tree, node, "#size-cells", &len);
	if (!nsizep) {
		if (len != -FDT_ERR_NOTFOUND)
			return len;
	} else if (len == 4 && *nsizep <= MAX_SIZE_CELLS) {
		*nsize = *nsizep;
	} else {
		return BADTREE;
	}

	return 0;
}

static void copy_val(uint32_t *dest, const uint32_t *src, int naddr)
{
	int pad = MAX_ADDR_CELLS - naddr;

	memset(dest, 0, pad * 4);
	memcpy(dest + pad, src, naddr * 4);
}

static void val_from_int(uint32_t *dest, physaddr_t src)
{
	dest[0] = 0;
	dest[1] = 0;
	dest[2] = src >> 32;
	dest[3] = (uint32_t)src;
}

static int sub_reg(uint32_t *reg, uint32_t *sub)
{
	int i, borrow = 0;

	for (i = MAX_ADDR_CELLS - 1; i >= 0; i--) {
		int prev_borrow = borrow;
		borrow = reg[i] < sub[i] + prev_borrow;
		reg[i] -= sub[i] + prev_borrow;
	}

	return !borrow;
}

static int add_reg(uint32_t *reg, uint32_t *add, int naddr)
{
	int i, carry = 0;

	for (i = MAX_ADDR_CELLS - 1; i >= MAX_ADDR_CELLS - naddr; i--) {
		uint64_t tmp = (uint64_t)reg[i] + add[i] + carry;
		carry = tmp >> 32;
		reg[i] = (uint32_t)tmp;
	}

	return !carry;
}

/* FIXME: It is assumed that if the first byte of reg fits in a
 * range, then the whole reg block fits.
 */
static int compare_reg(const uint32_t *reg, const uint32_t *range,
                       const uint32_t *rangesize)
{
	int i;
	uint32_t end;

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		if (reg[i] < range[i])
			return 0;
		if (reg[i] > range[i])
			break;
	}

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		end = range[i] + rangesize[i];

		if (reg[i] < end)
			return 1;
		if (reg[i] > end)
			return 0;
	}

	return 0;
}

/* reg must be MAX_ADDR_CELLS */
static int find_range(const uint32_t *reg, const uint32_t *ranges,
                      int nregaddr, int naddr, int nsize, int buflen)
{
	int nrange = nregaddr + naddr + nsize;
	int i;

	if (nrange <= 0)
		return BADTREE;

	for (i = 0; i < buflen; i += nrange) {
		uint32_t range_addr[MAX_ADDR_CELLS];
		uint32_t range_size[MAX_ADDR_CELLS];

		if (i + nrange > buflen) {
			return BADTREE;
		}

		copy_val(range_addr, ranges + i, naddr);
		copy_val(range_size, ranges + i + nregaddr + naddr, nsize);

		if (compare_reg(reg, range_addr, range_size))
			return i;
	}

	return -FDT_ERR_NOTFOUND;
}

/* Currently only generic buses without special encodings are supported.
 * In particular, PCI is not supported.  Also, only the beginning of the
 * reg block is tracked; size is ignored except in ranges.
 */
static int xlate_one(uint32_t *addr, const uint32_t *ranges,
                     int rangelen, uint32_t naddr, uint32_t nsize,
                     uint32_t prev_naddr, uint32_t prev_nsize,
                     physaddr_t *rangesize)
{
	uint32_t tmpaddr[MAX_ADDR_CELLS], tmpaddr2[MAX_ADDR_CELLS];
	int offset = find_range(addr, ranges, prev_naddr,
	                        naddr, prev_nsize, rangelen / 4);

	if (offset < 0)
		return offset;

	ranges += offset;

	copy_val(tmpaddr, ranges, prev_naddr);

	if (!sub_reg(addr, tmpaddr))
		return BADTREE;

	if (rangesize) {
		copy_val(tmpaddr, ranges + prev_naddr + naddr, prev_nsize);
	
		if (!sub_reg(tmpaddr, addr))
			return BADTREE;

		*rangesize = ((uint64_t)tmpaddr[2]) << 32;
		*rangesize |= tmpaddr[3];
	}

	copy_val(tmpaddr, ranges + prev_naddr, naddr);

	if (!add_reg(addr, tmpaddr, naddr))
		return BADTREE;

	// Reject ranges that wrap around the address space.  Primarily
	// intended to enable blacklist entries in fsl,hvranges.

	copy_val(tmpaddr, ranges + prev_naddr, naddr);
	copy_val(tmpaddr2, ranges + prev_naddr + naddr, nsize);
	
	if (!add_reg(tmpaddr, tmpaddr2, naddr))
		return NOTRANS;

	return 0;
}

static int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                         uint32_t *addrbuf, uint32_t *rootnaddr,
                         physaddr_t *size)
{
	int parent;
	uint64_t ret_addr;
	uint32_t naddr, nsize, prev_naddr, prev_nsize;
	const uint32_t *ranges;
	int len, offset;
	int ret;

	parent = fdt_parent_offset(tree, node);
	if (parent < 0)
		return parent;

	ret = get_addr_format(tree, parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	copy_val(addrbuf, reg, naddr);

	if (size) {
		*size = reg[naddr];
		if (nsize == 2) {
			*size <<= 32;
			*size |= reg[naddr + 1];
		}
	}

	for (;;) {
		prev_naddr = naddr;
		prev_nsize = nsize;
		node = parent;

		parent = fdt_parent_offset(tree, node);
		if (parent == -FDT_ERR_NOTFOUND)
			break;
		if (parent < 0)
			return parent;

		ret = get_addr_format(tree, parent, &naddr, &nsize);
		if (ret < 0)
			return ret;

		ranges = fdt_getprop(tree, node, "ranges", &len);
		if (!ranges) {
			if (len == -FDT_ERR_NOTFOUND)
				return NOTRANS;
		
			return len;
		}

		if (len == 0)
			continue;
		if (len % 4)
			return BADTREE;

		ret = xlate_one(addrbuf, ranges, len, naddr, nsize,
		                prev_naddr, prev_nsize, NULL);
		if (ret < 0)
			return ret;
	}

	*rootnaddr = naddr;
	return 0;
}

static int xlate_reg(const void *tree, int node, const uint32_t *reg,
                     physaddr_t *addr, physaddr_t *size)
{
	uint32_t addrbuf[MAX_ADDR_CELLS];
	uint32_t rootnaddr;

	int ret = xlate_reg_raw(tree, node, reg, addrbuf, &rootnaddr, size);

	if (rootnaddr < 0 || rootnaddr > 2)
		return BADTREE;

	if (addrbuf[0] || addrbuf[1])
		return BADTREE;

	*addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];
	return 0;
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

	vptbl_map(guest->gphys, gaddr >> PAGE_SHIFT, addr >> PAGE_SHIFT,
	          pages, PTE_ALL, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, addr >> PAGE_SHIFT, gaddr >> PAGE_SHIFT,
	          size >> PAGE_SHIFT, PTE_ALL, PTE_PHYS_LEVELS);
}

static int map_guest_reg_one(guest_t *guest, int node, const uint32_t *reg)
{
	physaddr_t gaddr, size;
	uint32_t addrbuf[MAX_ADDR_CELLS], rootnaddr = 0;
	physaddr_t rangesize, addr, offset = 0;
	int hvrlen;

	const uint32_t *hvranges = fdt_getprop(fdt, guest->partition_node,
	                                       "fsl,hv-ranges", &hvrlen);
	if (!hvranges && hvrlen != -FDT_ERR_NOTFOUND)
		return hvrlen;
	if (!hvranges || hvrlen % 4)
		return BADTREE;

	int ret = xlate_reg_raw(guest->devtree, node, reg, addrbuf,
	                        &rootnaddr, &size);
	if (ret < 0)
		return ret;	

	if (rootnaddr < 0 || rootnaddr > 2)
		return BADTREE;

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
			return BADTREE;

		addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

		if (rangesize > size - offset)
			rangesize = size - offset;
		
		map_guest_range(guest, gaddr + offset, addr, rangesize);
		offset += rangesize;
	}

	return 0;
}

static int map_guest_reg(guest_t *guest, int node)
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
		return BADTREE;

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
			return BADTREE;

		ret = map_guest_reg_one(guest, node, reg + i);
		if (ret < 0 && ret != NOTRANS)
			return ret;
	}

	return 0;
}

static int map_guest_reg_all(guest_t *guest, int partition)
{
	int node = -1;
	int depth = -1;

	while ((node = fdt_get_next_node(guest->devtree, node, &depth, 1)) >= 0) {
		int ret = map_guest_reg(guest, node);
		if (ret < 0)
			return ret;
	}
	
	if (node != -FDT_ERR_NOTFOUND)
		return node;

	return 0;
}

#define MAX_PATH 256

static int process_guest_devtree(guest_t *guest,
                                 const uint32_t *cpulist,
                                 int cpulist_len)
{
	int ret, len;
	int gfdt_size = fdt_totalsize(fdt);
	const void *guest_origtree;

	guest_origtree = fdt_getprop(fdt, guest->partition_node, "fsl,dtb", &len);
	if (!guest_origtree) {
		printf("guest %s: no fsl,dtb property\n", guest->name);
		ret = -FDT_ERR_NOTFOUND;
		goto fail;
	}
	
	gfdt_size += len;
	
	guest->devtree = alloc(gfdt_size, 16);
	if (!guest->devtree) {
		ret = NOMEM;
		goto fail;
	}

	ret = fdt_open_into(guest_origtree, guest->devtree, gfdt_size);
	if (ret < 0)
		goto fail;

	guest->gphys = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	guest->gphys_rev = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	if (!guest->gphys || !guest->gphys_rev) {
		ret = NOMEM;
		goto fail;
	}

	ret = map_guest_reg_all(guest, guest->partition_node);
	if (ret == 0)
		return 0;

fail:
	printf("error %d (%s) building guest device tree for %s\n",
	       ret, fdt_strerror(ret), guest->name);

	return ret;
}

void start_guest(void)
{
	int off = -1, found = 0, ret;
	int pir = mfspr(SPR_PIR);
	char buf[MAX_PATH];
	
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

		if (pir == cpus[0]) {
			guest_t *guest = alloc(sizeof(guest_t), 16);
			if (!guest)
				goto nomem;
			
			guest->name = alloc(MAX_PATH, 1);
			if (!guest->name)
				goto nomem;

			guest->partition_node = off;
			guest->lpid = atomic_add(&last_lpid, 1);
			mtspr(SPR_LPID, guest->lpid);
			
			ret = fdt_get_path(fdt, off, guest->name, sizeof(buf));
			if (ret < 0) {
				printf("start_guest: error %d (%s) getting path at offset %d\n",
				       ret, fdt_strerror(ret), off);
				continue;
			}
			
			ret = process_guest_devtree(guest, cpus, len);
			if (ret < 0)
				continue;

			unsigned long attr;
			unsigned long rpn = vptbl_xlate(guest->gphys,
			                                0x00f00, &attr, PTE_PHYS_LEVELS);

			if (!(attr & PTE_VALID)) {
				printf("No valid guest phys 0x00f00000\n");
				continue;
			}
			
			memcpy((void *)(PHYSBASE + (rpn << PAGE_SHIFT)), guest->devtree,
			       fdt_totalsize(fdt));
			
			get_gcpu()->guest = guest;
			guest_set_tlb1(0, (TLB_TSIZE_256M << MAS1_TSIZE_SHIFT) | MAS1_IPROT,
			               0, 0, TLB_MAS2_MEM, TLB_MAS3_KERN);

			printf("branching to guest\n");

			asm volatile("mfmsr %%r3; oris %%r3, %%r3, 0x1000;"
			             "li %%r4, 0; li %%r5, 0; li %%r6, 0; li %%r7, 0;"
			             "mtsrr0 %0; mtsrr1 %%r3; lis %%r3, 0x00f0; rfi" : :
			             "r" (0 << PAGE_SHIFT) :
	   		          "r3", "r4", "r5", "r6", "r7", "r8");

		} else {
			// enter as secondary cpu
		}
	}

	return;

nomem:
	printf("out of memory in start_guest\n");
} 
