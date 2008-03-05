/** @file
 * Device tree processing
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#include <devtree.h>
#include <paging.h>
#include <errors.h>

#include <libos/ns16550.h>

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
		return ERR_BADTREE;
	}

	const uint32_t *nsizep = fdt_getprop(tree, node, "#size-cells", &len);
	if (!nsizep) {
		if (len != -FDT_ERR_NOTFOUND)
			return len;
	} else if (len == 4 && *nsizep <= MAX_SIZE_CELLS) {
		*nsize = *nsizep;
	} else {
		return ERR_BADTREE;
	}

	return 0;
}

static void copy_val(uint32_t *dest, const uint32_t *src, int naddr)
{
	int pad = MAX_ADDR_CELLS - naddr;

	memset(dest, 0, pad * 4);
	memcpy(dest + pad, src, naddr * 4);
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
		return ERR_BADTREE;

	for (i = 0; i < buflen; i += nrange) {
		uint32_t range_addr[MAX_ADDR_CELLS];
		uint32_t range_size[MAX_ADDR_CELLS];

		if (i + nrange > buflen) {
			return ERR_BADTREE;
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
int xlate_one(uint32_t *addr, const uint32_t *ranges,
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
		return ERR_BADTREE;

	if (rangesize) {
		copy_val(tmpaddr, ranges + prev_naddr + naddr, prev_nsize);
	
		if (!sub_reg(tmpaddr, addr))
			return ERR_BADTREE;

		*rangesize = ((uint64_t)tmpaddr[2]) << 32;
		*rangesize |= tmpaddr[3];
	}

	copy_val(tmpaddr, ranges + prev_naddr, naddr);

	if (!add_reg(addr, tmpaddr, naddr))
		return ERR_BADTREE;

	// Reject ranges that wrap around the address space.  Primarily
	// intended to enable blacklist entries in fsl,hvranges.

	copy_val(tmpaddr, ranges + prev_naddr, naddr);
	copy_val(tmpaddr2, ranges + prev_naddr + naddr, nsize);
	
	if (!add_reg(tmpaddr, tmpaddr2, naddr))
		return ERR_NOTRANS;

	return 0;
}

int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                  uint32_t *addrbuf, uint32_t *rootnaddr,
                  physaddr_t *size)
{
	int parent;
	uint32_t naddr, nsize, prev_naddr, prev_nsize;
	const uint32_t *ranges;
	int len, ret;

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
				return ERR_NOTRANS;
		
			return len;
		}

		if (len == 0)
			continue;
		if (len % 4)
			return ERR_BADTREE;

		ret = xlate_one(addrbuf, ranges, len, naddr, nsize,
		                prev_naddr, prev_nsize, NULL);
		if (ret < 0)
			return ret;
	}

	*rootnaddr = naddr;
	return 0;
}

int xlate_reg(const void *tree, int node, const uint32_t *reg,
              physaddr_t *addr, physaddr_t *size)
{
	uint32_t addrbuf[MAX_ADDR_CELLS];
	uint32_t rootnaddr;

	int ret = xlate_reg_raw(tree, node, reg, addrbuf, &rootnaddr, size);
	if (ret < 0)
		return ret;

	if (rootnaddr < 0 || rootnaddr > 2)
		return ERR_BADTREE;

	if (addrbuf[0] || addrbuf[1])
		return ERR_BADTREE;

	*addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];
	return 0;
}

int dt_get_reg(const void *tree, int node, int res,
               physaddr_t *addr, physaddr_t *size)
{
	int ret, len;
	uint32_t naddr, nsize;
	const uint32_t *reg = fdt_getprop(tree, node, "reg", &len);
	if (!reg)
		return len;

	int parent = fdt_parent_offset(tree, node);
	if (parent < 0)
		return parent;

	ret = get_addr_format(tree, parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	if (naddr == 0 || nsize == 0)
		return ERR_NOTRANS;

	if (len < (naddr + nsize) * 4 * (res + 1))
		return ERR_BADTREE;

	return xlate_reg(tree, node, &reg[(naddr + nsize) * res], addr, size);
}

void *ptr_from_node(const void *tree, int offset, const char *type)
{
	char buf[64];
	void *const *ptr;
	int len;

	snprintf(buf, sizeof(buf), "fsl,hv-internal-%s-ptr", type);

	ptr = fdt_getprop(tree, offset, buf, &len);
	if (!ptr) {
		if (len != -FDT_ERR_NOTFOUND)
			printf("ptr_from_node(%s): libfdt error %d (%s)\n",
			       type, len, fdt_strerror(len));
	
		return NULL;
	}

	return *ptr;
}

#ifdef CONFIG_NS16550
void create_ns16550(void)
{
	int off = -1, ret;
	physaddr_t addr;

	while (1) {
		off = fdt_node_offset_by_compatible(fdt, off, "ns16550");
		if (off < 0)
			break;

		ret = dt_get_reg(fdt, off, 0, &addr, NULL);
		if (ret < 0)
			continue;

		if (addr < 0)
			continue;

		// FIXME: IRQ, clock-frequency
		chardev_t *cd = ns16550_init((uint8_t *)(unsigned long)
		                             (CCSRBAR_VA + (addr - CCSRBAR_PA)),
		                             0x24, 0, 16);

		ret = fdt_setprop(fdt, off, "fsl,hv-internal-chardev-ptr",
		                  &cd, sizeof(cd));
		if (ret < 0)
			break;
	}
}
#endif

void open_stdout(void)
{
	int off = lookup_alias(fdt, "stdout");
	if (off < 0)
		return;

	chardev_t *cd = ptr_from_node(fdt, off, "chardev");
	if (cd)
		console_init(cd);
}

// FIXME: memory holes
physaddr_t find_end_of_mem(void)
{
	physaddr_t mem_end = 0;
	int memnode = fdt_subnode_offset(fdt, 0, "memory");
	if (memnode < 0) {
		printf("error %d (%s) opening /memory\n", memnode,
		       fdt_strerror(memnode));

		return 0;
	}

	int len;
	const uint32_t *memreg = fdt_getprop(fdt, memnode, "reg", &len);
	if (!memreg) {
		printf("error %d (%s) reading /memory/reg\n", memnode,
		       fdt_strerror(memnode));

		return 0;
	}

	uint32_t naddr, nsize;
	int ret = get_addr_format(fdt, memnode, &naddr, &nsize);
	if (ret < 0) {
		printf("error %d (%s) getting address format for /memory\n",
		       ret, fdt_strerror(ret));

		return 0;
	}

	if (naddr < 1 || naddr > 2 || nsize < 1 || nsize > 2) {
		printf("bad address format %u/%u for /memory\n", naddr, nsize);
		return 0;
	}

	const uint32_t *reg = memreg;
	while (reg + naddr + nsize <= memreg + len / 4) {
		physaddr_t addr = *reg++;
		if (naddr == 2) {
			addr <<= 32;
			addr |= *reg++;
		}

		physaddr_t size = *reg++;
		if (nsize == 2) {
			size <<= 32;
			size |= *reg++;
		}

		addr += size - 1;
		if (addr > mem_end)
			mem_end = addr;
	}
	
	return mem_end;
}

/** Look up a string which may be a path or an alias
 *
 * @param[in] tree the device tree to search
 * @param[in] path the path or alias to look up
 * @return the node offset within the tree, or a libfdt error.
 */
int lookup_alias(const void *tree, const char *path)
{
	int ret;
	
	if (path[0] == '/') {
		ret = fdt_path_offset(tree, path);
		if (ret != -FDT_ERR_NOTFOUND)
			return ret;
	}

	ret = fdt_subnode_offset(tree, 0, "aliases");
	if (ret < 0)
		return ret;

	path = fdt_getprop(tree, ret, path, &ret);
	if (!path)
		return ret;

	return fdt_path_offset(tree, path);
}

/** Get the interrupt controller for a node
 *
 * @param[in] tree the device tree to search
 * @param[in] the node for which we want the interrupt-parent
 * @return the node offset within the tree, or a libfdt error.
 *
 */

int get_interrupt_controller(const void *tree, int node)
{
	const uint32_t *prop;
	int len;

	// FIXME: update to deal with interrupt maps

	while (1) {
		prop = fdt_getprop(tree, node,
                              "interrupt-parent", &len);
		if (prop) {
			node = fdt_node_offset_by_phandle(tree, *prop);
		} else {
			node = fdt_parent_offset(tree, node);
    		}
		if (node < 0)
			return node;

		prop = fdt_getprop(tree, node,
                              "interrupt-controller", &len);
		if (prop)
			return node;
	}
}

