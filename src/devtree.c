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

#include <devtree.h>
#include <paging.h>
#include <errors.h>
#include <byte_chan.h>

#include <libos/ns16550.h>
#include <libos/queue.h>
#include <libos/chardev.h>
#include <libos/interrupts.h>
#include <libos/mpic.h>

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
		printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		         "Bad addr cells %d\n", *naddrp);
		return ERR_BADTREE;
	}

	const uint32_t *nsizep = fdt_getprop(tree, node, "#size-cells", &len);
	if (!nsizep) {
		if (len != -FDT_ERR_NOTFOUND)
			return len;
	} else if (len == 4 && *nsizep <= MAX_SIZE_CELLS) {
		*nsize = *nsizep;
	} else {
		printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		         "Bad size cells %d\n", *nsizep);
		return ERR_BADTREE;
	}

	return 0;
}

int get_addr_format_nozero(const void *tree, int node,
                           uint32_t *naddr, uint32_t *nsize)
{
	int ret = get_addr_format(tree, node, naddr, nsize);
	if (!ret && (*naddr == 0 || *nsize == 0)) {
		printlog(LOGTYPE_MISC, LOGLEVEL_NORMAL,
		         "Bad addr/size cells %d/%d\n", *naddr, *nsize);

		ret = ERR_BADTREE;
	}

	return ret;
}

void copy_val(uint32_t *dest, const uint32_t *src, int naddr)
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
              phys_addr_t *rangesize)
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

	/* Reject ranges that wrap around the address space.  Primarily
	 * intended to enable blacklist entries in fsl,hvranges.
	 */
	copy_val(tmpaddr, ranges + prev_naddr, naddr);
	copy_val(tmpaddr2, ranges + prev_naddr + naddr, nsize);
	
	if (!add_reg(tmpaddr, tmpaddr2, naddr))
		return ERR_NOTRANS;

	return 0;
}

int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                  uint32_t *addrbuf, phys_addr_t *size,
                  uint32_t naddr, uint32_t nsize)
{
	uint32_t prev_naddr, prev_nsize;
	const uint32_t *ranges;
	int len, ret;

	int parent = fdt_parent_offset(tree, node);
	if (parent < 0)
		return parent;

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

	return 0;
}

int xlate_reg(const void *tree, int node, const uint32_t *reg,
              phys_addr_t *addr, phys_addr_t *size)
{
	uint32_t addrbuf[MAX_ADDR_CELLS];
	uint32_t naddr, nsize;

	int parent = fdt_parent_offset(tree, node);
	if (parent < 0)
		return parent;

	int ret = get_addr_format(tree, parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	ret = xlate_reg_raw(tree, node, reg, addrbuf, size, naddr, nsize);
	if (ret < 0)
		return ret;

	if (addrbuf[0] || addrbuf[1])
		return ERR_BADTREE;

	*addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];
	return 0;
}

int dt_get_reg(const void *tree, int node, int res,
               phys_addr_t *addr, phys_addr_t *size)
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

#ifdef CONFIG_LIBOS_NS16550
void create_ns16550(void)
{
	int off = -1, ret;
	phys_addr_t addr;
	const uint32_t *prop;
	int ncells;

	while (1) {
		interrupt_t *irq = NULL;

		off = fdt_node_offset_by_compatible(fdt, off, "ns16550");
		if (off < 0)
			break;

		ret = dt_get_reg(fdt, off, 0, &addr, NULL);
		if (ret < 0) {
			printf("ns16550 failed to get reg: %d\n", ret);
			continue;
		}

		// FIXME: clock-frequency
		ret = get_interrupt(fdt, off, 0, &prop, &ncells);
		if (ret >= 0)
			irq = get_mpic_irq(prop, ncells);
		if (irq) {
			ret = irq->ops->config_by_intspec(irq, prop, ncells);
			if (ret < 0) {
				printf("ns16550 irq config failed: %d\n", ret);
				irq = NULL;
			} else {
				irq->ops->set_delivery_type(irq, TYPE_CRIT);
				irq->ops->set_priority(irq, 15);
			}
		}

		chardev_t *cd = ns16550_init((uint8_t *)(unsigned long)
		                             (CCSRBAR_VA + (addr - CCSRBAR_PA)),
		                             irq, 0, 16);

		ret = fdt_setprop(fdt, off, "fsl,hv-internal-chardev-ptr",
		                  &cd, sizeof(cd));
		if (ret < 0)
			break;
	}
}
#endif

chardev_t *cd_console;
byte_chan_handle_t *bc_console;
queue_t *stdout, *stdin;

int open_stdout_chardev(int node)
{
	chardev_t *cd;
	
	cd = ptr_from_node(fdt, node, "chardev");
	if (!cd)
		return ERR_INVALID;

	console_init(cd);

#ifdef CONFIG_LIBOS_QUEUE
	if (cd->ops->set_tx_queue) {
		int ret;
		queue_t *q = alloc_type(queue_t);
		if (!q)
			return ERR_NOMEM;

		ret = queue_init(q, 2048);
		if (ret < 0)
			return ret;

		ret = cd->ops->set_tx_queue(cd, q);
		if (ret < 0)
			return ret;
		
		stdout = q;
	}
#endif

	cd_console = cd;
	return 0;
}

#ifdef CONFIG_BYTE_CHAN
int open_stdout_bytechan(int node)
{
	byte_chan_t *bc;
	
	bc = ptr_from_node(fdt, node, "bc");
	if (!bc)
		return ERR_INVALID;

	bc_console = byte_chan_claim(bc);
	if (!bc_console)
		return ERR_BUSY;

	qconsole_init(bc_console->tx);
	stdout = bc_console->tx;

	return 0;
}
#endif

void open_stdout(void)
{
	int ret = lookup_alias(fdt, "stdout");
	if (ret < 0)
		return;

	open_stdout_chardev(ret);
#ifdef CONFIG_BYTE_CHAN
	open_stdout_bytechan(ret);
#endif
}

#ifdef CONFIG_SHELL
int open_stdin_chardev(chardev_t *cd)
{
	queue_t *q;
	int ret;

	if (!cd->ops->set_rx_queue)
		return ERR_INVALID;

	q = alloc_type(queue_t);
	if (!q)
		return ERR_NOMEM;

	ret = queue_init(q, 2048);
	if (ret < 0)
		goto err_queue;

	ret = cd->ops->set_rx_queue(cd, q);
	if (ret < 0)
		goto err_queue_buf;
		
	stdin = q;
	return 0;

err_queue_buf:
	queue_destroy(q);
err_queue:
	free(q);
	
	return ret;
}

#ifdef CONFIG_BYTE_CHAN
int open_stdin_bytechan(byte_chan_handle_t *bc)
{
	stdin = bc->rx;
	return 0;
}
#endif

void open_stdin(void)
{
	if (cd_console)
		open_stdin_chardev(cd_console);
#ifdef CONFIG_BYTE_CHAN
	if (bc_console)
		open_stdin_bytechan(bc_console);
#endif
}
#endif

static void add_memory(phys_addr_t start, phys_addr_t size)
{
	if (start + size < start) {
		printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
		         "memory node contains invalid region 0x%llx->0x%llx\n",
		         start, start + size - 1);

		return;
	}

	start += PHYSBASE;

	if (start > 0x100000000ULL)
		return;

	if (start + size + PHYSBASE > 0x100000000ULL)
		size = 0x100000000ULL - start;

	malloc_add_segment((void *)(unsigned long)start,
	                   (void *)(unsigned long)(start + size - 1));
}

phys_addr_t find_memory(void)
{
	phys_addr_t mem_end = 0;
	int memnode = -1;
	int len;
	
	while (1) {
		uint32_t naddr, nsize;
		int parent;

		memnode = fdt_node_offset_by_prop_value(fdt, memnode, "device_type",
		                                        "memory", strlen("memory") + 1);
		if (memnode < 0)
			break;

		const uint32_t *memreg = fdt_getprop(fdt, memnode, "reg", &len);
		if (!memreg) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "error %d (%s) reading memory reg\n", len,
			         fdt_strerror(len));

			continue;
		}

		parent = fdt_parent_offset(fdt, memnode);
		if (parent < 0) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "error %d (%s) finding memory parent\n", len,
			         fdt_strerror(len));

			continue;
		}

		int ret = get_addr_format(fdt, parent, &naddr, &nsize);
		if (ret < 0) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "error %d (%s) getting address format for memory node\n",
			         ret, fdt_strerror(ret));

			continue;
		}

		if (naddr < 1 || naddr > 2 || nsize < 1 || nsize > 2) {
			printlog(LOGTYPE_MALLOC, LOGLEVEL_ERROR,
			         "bad address format %u/%u for memory node\n",
			         naddr, nsize);

			continue;
		}

		const uint32_t *reg = memreg;
		while (reg + naddr + nsize <= memreg + len / 4) {
			phys_addr_t addr = *reg++;
			if (naddr == 2) {
				addr <<= 32;
				addr |= *reg++;
			}

			phys_addr_t size = *reg++;
			if (nsize == 2) {
				size <<= 32;
				size |= *reg++;
			}

			add_memory(addr, size);

			addr += size - 1;
			if (addr > mem_end)
				mem_end = addr;
		}
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

int get_interrupt_domain(const void *tree, int node)
{
	const uint32_t *prop;
	int len;

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

		prop = fdt_getprop(tree, node, "interrupt-controller", &len);
		if (prop)
			return node;

		prop = fdt_getprop(tree, node, "interrupt-map", &len);
		if (prop)
			return node;
	}
}

static int get_int_cells(const void *tree, int domain)
{
	int len;
	const uint32_t *prop;
	
	prop = fdt_getprop(tree, domain, "#interrupt-cells", &len);
	if (!prop) {
		if (len == -FDT_ERR_NOTFOUND) {
			printf("get_interrupt: Interrupt domain has no #interrupt-cells\n");
			len = ERR_BADTREE;
		}

		return len;
	}
	
	return *prop;
}

/** Get the interrupt controller and intspec for a node.
 *
 * @param[in] tree the device tree to search
 * @param[in] the node for which we want the interrupt
 * @param[in] intnum index into interrupts node
 * @param[out] intspec the returned interrupt spec
 * @return the node offset of the interrupt controller, or an error.
 */
int get_num_interrupts(const void *tree, int node)
{
	const uint32_t *intspec;
	int domain, len;

	intspec = fdt_getprop(tree, node, "interrupts", &len);
	if (!intspec) {
		if (len == -FDT_ERR_NOTFOUND)
			len = 0;
	
		return len;
	}
	
	domain = get_interrupt_domain(tree, node);
	if (domain < 0)
		return domain;
	
	return len / get_int_cells(tree, domain);
}

/** Get the interrupt controller and intspec for a node.
 *
 * @param[in] tree the device tree to search
 * @param[in] the node for which we want the interrupt
 * @param[in] intnum index into interrupts node
 * @param[out] intspec the returned interrupt spec
 * @param[out] ncells the number of cells in the intspec (optional)
 * @return the node offset of the interrupt controller, or an error.
 */
int get_interrupt(const void *tree, int node, int intnum,
                  const uint32_t **intspec, int *ncellsp)
{
	const uint32_t *prop;
	int len, domain, speclen, ncells;

	while (1) {
		*intspec = fdt_getprop(tree, node, "interrupts", &speclen);
		if (!*intspec)
			return speclen;
		
		domain = get_interrupt_domain(tree, node);
		if (domain < 0) {
			if (domain == -FDT_ERR_NOTFOUND) {
				printf("get_interrupt: Interrupt domain has no #interrupt-cells\n");
				domain = ERR_BADTREE;
			}

			return domain;
		}

		ncells = get_int_cells(tree, domain);
		if (ncells < 0)
			return ncells;

		if (ncellsp)
			*ncellsp = ncells;

		if ((intnum + 1) * ncells * 4 > speclen)
			return ERR_BADTREE;

		*intspec += ncells * intnum;

		prop = fdt_getprop(tree, domain, "interrupt-controller", &len);
		if (prop)
			return domain;

		/* FIXME: Translate interrupt here */
		printf("get_interrupt: interrupt-map not yet supported\n");
		return ERR_BADTREE;
	}
}


/*
 * Searches for a descendant of the supplied offset by compatible
 *
 */
int fdt_next_descendant_by_compatible(const void *fdt, int offset, int *depth, const char *compatible)
{
	while (1) {
		offset = fdt_next_node(fdt, offset, depth);
		if (offset < 0)
			return offset;
		if (*depth < 1)
			return -FDT_ERR_NOTFOUND;
		if (fdt_node_check_compatible(fdt, offset, compatible) == 0)
			return offset;
	}
}

int get_cpu_node(const void *fdt, int cpunum)
{
	int off = 0;

	while (1) {
		const uint32_t *reg;
		int len;

		off = fdt_node_offset_by_prop_value(fdt, off, "device_type", "cpu", 4);
		if (off < 0)
			return off;

		reg = fdt_getprop(fdt, off, "reg", &len);
		if (!reg) {
			printlog(LOGTYPE_MISC, LOGLEVEL_ERROR,
			         "Missing reg property in cpu node, fdt error %d\n", len);
			return len;
		}

		if (*reg == cpunum)
			return off;
	}
}
