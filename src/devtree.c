/** @file
 * Device tree semantic processing
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

int get_addr_format(dt_node_t *node, uint32_t *naddr, uint32_t *nsize)
{
	dt_prop_t *prop;

	*naddr = 2;
	*nsize = 1;

	prop = dt_get_prop(node, "#address-cells", 0);
	if (prop) {
		if (prop->len != 4)
			goto bad;

		*naddr = *(const uint32_t *)prop->data;

		if (*naddr > MAX_ADDR_CELLS)
			goto bad;
	}

	prop = dt_get_prop(node, "#size-cells", 0);
	if (prop) {
		if (prop->len != 4)
			goto bad;

		*nsize = *(const uint32_t *)prop->data;

		if (*nsize > MAX_SIZE_CELLS)
			goto bad;
	}

	return 0;

bad:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_NORMAL,
	         "%s: Bad #addresss-cells or #size-cells\n", node->name);
	return ERR_BADTREE;
}

int fdt_get_addr_format(const void *fdt, int offset,
                        uint32_t *naddr, uint32_t *nsize)
{
	const uint32_t *prop;
	int len;

	*naddr = 2;
	*nsize = 1;

	prop = fdt_getprop(fdt, offset, "#address-cells", &len);
	if (prop) {
		if (len != 4)
			goto bad;

		*naddr = *prop;

		if (*naddr > MAX_ADDR_CELLS)
			goto bad;
	}

	prop = fdt_getprop(fdt, offset, "#size-cells", &len);
	if (prop) {
		if (len != 4)
			goto bad;

		*nsize = *prop;

		if (*nsize > MAX_SIZE_CELLS)
			goto bad;
	}

	return 0;

bad:
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_NORMAL,
	         "Bad #addresss-cells or #size-cells\n");
	return ERR_BADTREE;
}

int get_addr_format_nozero(dt_node_t *node, uint32_t *naddr, uint32_t *nsize)
{
	int ret = get_addr_format(node, naddr, nsize);
	if (!ret && (*naddr == 0 || *nsize == 0)) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_NORMAL,
		         "%s: Bad addr/size cells %d/%d\n",
		         node->name, *naddr, *nsize);

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

	return ERR_NOTFOUND;
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

int xlate_reg_raw(dt_node_t *node, const uint32_t *reg,
                  uint32_t *addrbuf, phys_addr_t *size,
                  uint32_t naddr, uint32_t nsize)
{
	uint32_t prev_naddr, prev_nsize;
	dt_node_t *parent;
	dt_prop_t *prop;
	int ret;

	parent = node->parent;
	if (!parent)
		return ERR_BADTREE;

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

		parent = node->parent;
		if (!parent)
			break;

		ret = get_addr_format(parent, &naddr, &nsize);
		if (ret < 0)
			return ret;

		prop = dt_get_prop(node, "ranges", 0);
		if (!prop)
			return ERR_NOTRANS;

		if (prop->len == 0)
			continue;
		if (prop->len % 4)
			return ERR_BADTREE;

		ret = xlate_one(addrbuf, prop->data, prop->len, naddr, nsize,
		                prev_naddr, prev_nsize, NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int xlate_reg(dt_node_t *node, const uint32_t *reg,
              phys_addr_t *addr, phys_addr_t *size)
{
	uint32_t addrbuf[MAX_ADDR_CELLS];
	uint32_t naddr, nsize;

	dt_node_t *parent = node->parent;
	if (!parent)
		return ERR_BADTREE;

	int ret = get_addr_format(parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	ret = xlate_reg_raw(node, reg, addrbuf, size, naddr, nsize);
	if (ret < 0)
		return ret;

	if (addrbuf[0] || addrbuf[1])
		return ERR_BADTREE;

	*addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];
	return 0;
}

int dt_get_reg(dt_node_t *node, int res,
               phys_addr_t *addr, phys_addr_t *size)
{
	int ret;
	uint32_t naddr, nsize;
	dt_prop_t *prop;
	const uint32_t *reg;
	
	prop = dt_get_prop(node, "reg", 0);
	if (!prop)
		return ERR_NOTFOUND;

	dt_node_t *parent = node->parent;
	if (!parent)
		return ERR_BADTREE;

	ret = get_addr_format_nozero(parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	if (prop->len < (naddr + nsize) * 4 * (res + 1))
		return ERR_RANGE;

	reg = prop->data;
	return xlate_reg(node, &reg[(naddr + nsize) * res], addr, size);
}

void *ptr_from_node(dt_node_t *node, const char *type)
{
	char buf[64];
	dt_prop_t *prop;

	snprintf(buf, sizeof(buf), "fsl,hv-internal-%s-ptr", type);
	buf[sizeof(buf) - 1] = 0;

	prop = dt_get_prop(node, buf, 0);
	if (!prop)
		return NULL;

	assert(prop->len == 4);
	return *(void *const *)prop->data;
}

#ifdef CONFIG_LIBOS_NS16550
static int create_one_ns16550(dt_node_t *node, void *arg)
{
	int ret;
	phys_addr_t addr;
	const uint32_t *intspec;
	dt_node_t *irqnode;
	interrupt_t *irq = NULL;
	int ncells;

	ret = dt_get_reg(node, 0, &addr, NULL);
	if (ret < 0) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "ns16550 failed to get reg: %d\n", ret);
		return 0;
	}

	// FIXME: clock-frequency
	irqnode = get_interrupt(hw_devtree, node, 0, &intspec, &ncells);
	if (irqnode)
		irq = get_mpic_irq(intspec, ncells);
	if (irq) {
		ret = irq->ops->config_by_intspec(irq, intspec, ncells);
		if (ret < 0) {
			printf("ns16550 irq config failed: %d\n", ret);
			irq = NULL;
		} else {
			irq->ops->set_priority(irq, 15);
		}
	}

	chardev_t *cd = ns16550_init((uint8_t *)(unsigned long)
	                             (CCSRBAR_VA + (addr - CCSRBAR_PA)),
	                             irq, 0, 16);

	dt_set_prop(node, "fsl,hv-internal-chardev-ptr", &cd, sizeof(cd));
	return 0;
}

void create_ns16550(void)
{
	dt_for_each_compatible(hw_devtree, "ns16550", create_one_ns16550, NULL);
}
#endif

chardev_t *cd_console;
byte_chan_handle_t *bc_console;
queue_t *stdout, *stdin;

static int open_stdout_chardev(dt_node_t *node)
{
	chardev_t *cd;
	
	cd = ptr_from_node(node, "chardev");
	if (!cd)
		return ERR_INVALID;

	console_init(cd);

#ifdef CONFIG_LIBOS_QUEUE
	if (cd->ops->set_tx_queue) {
		int ret;
		queue_t *q = alloc_type(queue_t);
		if (!q)
			return ERR_NOMEM;

		ret = queue_init(q, 16384);
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
static int open_stdout_bytechan(dt_node_t *node)
{
	byte_chan_t *bc;
	
	bc = ptr_from_node(node, "bc");
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
	// Temporarily fall back on serial0 if no stdout; eventually, get
	// stdout from config tree.
	dt_node_t *node = dt_lookup_alias(hw_devtree, "stdout");
	if (!node)
		node = dt_lookup_alias(hw_devtree, "serial0");
	if (!node)
		return;

	open_stdout_chardev(node);
#ifdef CONFIG_BYTE_CHAN
	open_stdout_bytechan(node);
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

phys_addr_t find_memory(void *fdt)
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

		int ret = fdt_get_addr_format(fdt, parent, &naddr, &nsize);
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

dt_node_t *get_interrupt_domain(dt_node_t *tree, dt_node_t *node)
{
	dt_prop_t *prop;

	while (1) {
		prop = dt_get_prop(node, "interrupt-parent", 0);
		if (prop) {
			if (prop->len != 4) {
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
				         "%s: bad interrupt-parent len %d in %s\n",
				         __func__, prop->len, node->name);

				return NULL;
			}

			assert(tree);
			assert(prop->data);

			node = dt_lookup_phandle(tree, *(const uint32_t *)prop->data);
		} else {
			node = node->parent;
		}

		if (!node)
			return NULL;

		if (dt_get_prop(node, "interrupt-controller", 0))
			return node;
		if (dt_get_prop(node, "interrupt-map", 0))
			return node;
	}
}

int dt_get_int_format(dt_node_t *domain, uint32_t *nint, uint32_t *naddr)
{
	dt_prop_t *prop;

	if (nint) {	
		prop = dt_get_prop(domain, "#interrupt-cells", 0);
		if (!prop) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "get_int_cells: %s has no #interrupt-cells\n",
			         domain->name);
			return ERR_BADTREE;
		}

		*nint = *(const uint32_t *)prop->data;

		if (*nint == 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "dt_get_int_format: %s has #interrupt-cells == 0\n",
			         domain->name);
			return ERR_BADTREE;
		}
	}

	if (naddr) {
		prop = dt_get_prop(domain, "#address-cells", 0);
		if (!prop) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_NORMAL,
			         "dt_get_int_format: %s has no #address-cells\n",
			         domain->name);
			*naddr = 0;
		} else {
			*naddr = *(const uint32_t *)prop->data;
		}
	}

	return 0;
}

/** Get the interrupt controller and intspec for a node.
 *
 * @param[in] tree the device tree to search
 * @param[in] the node for which we want the interrupt
 * @param[in] intnum index into interrupts node
 * @param[out] intspec the returned interrupt spec
 * @return the node offset of the interrupt controller, or an error.
 */
int get_num_interrupts(dt_node_t *tree, dt_node_t *node)
{
	dt_prop_t *prop;
	dt_node_t *domain;
	uint32_t icell;
	int ret;

	prop = dt_get_prop(node, "interrupts", 0);
	if (!prop)
		return 0;

	domain = get_interrupt_domain(tree, node);
	if (!domain)
		return ERR_BADTREE;

	ret = dt_get_int_format(domain, &icell, NULL);
	if (ret < 0)
		return ret;
	
	return prop->len / icell;
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
dt_node_t *get_interrupt(dt_node_t *tree, dt_node_t *node, int intnum,
                         const uint32_t **intspec, int *ncellsp)
{
	dt_prop_t *prop;
	dt_node_t *domain;
	uint32_t ncells;
	int ret;

	prop = dt_get_prop(node, "interrupts", 0);
	if (!prop)
		return NULL;
	*intspec = prop->data;
		
	domain = get_interrupt_domain(tree, node);
	if (!domain) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: Node %s has no interrupt controller\n",
		         __func__, node->name);
		return NULL;
	}

	ret = dt_get_int_format(domain, &ncells, NULL);
	if (ret < 0)
		return NULL;

	if (ncellsp)
		*ncellsp = ncells;

	if ((intnum + 1) * ncells * 4 > prop->len)
		return NULL;

	*intspec += ncells * intnum;

	if (dt_get_prop(domain, "interrupt-controller", 0))
		return domain;

	/* FIXME: Translate interrupt here */
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "get_interrupt: interrupt-map not yet supported\n");
	return NULL;
}

typedef struct get_cpu_ctx {
	dt_node_t *ret;
	int cpunum;
} get_cpu_ctx_t;

static int get_cpu_node_callback(dt_node_t *node, void *arg)
{
	get_cpu_ctx_t *ctx = arg;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "reg", 0);
	if (!prop) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "get_cpu_node: Missing reg property in cpu node\n");
		return 0;
	}

	if (*(const uint32_t *)prop->data == ctx->cpunum) {
		ctx->ret = node;
		return 1;
	}
	
	return 0;
}

/** Search for a descendant of the supplied offset by compatible
 *
 * @param[in] fdt flat device tree to search
 * @param[in] offset node whose children to search
 * @param[in,out] depth should be a pointer to zero initially
 * @param[in] compatible compatible string to search for
 * @return offset to next compatible node
 */
int fdt_next_descendant_by_compatible(const void *fdt, int offset,
                                      int *depth, const char *compatible)
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

dt_node_t *get_cpu_node(dt_node_t *tree, int cpunum)
{
	get_cpu_ctx_t ctx = { .cpunum = cpunum };

	dt_for_each_prop_value(tree, "device_type", "cpu", 4,
	                       get_cpu_node_callback, &ctx);
	return ctx.ret;
}

typedef struct assign_ctx {
	dt_node_t *tree;
	guest_t *guest;
} assign_ctx_t;

static uint32_t owner_lock;
DECLARE_LIST(hv_devs);

int assign_callback(dt_node_t *node, void *arg)
{
	assign_ctx_t *ctx = arg;
	const char *alias;
	dt_node_t *hwnode;
	
	/* Only process immediate children */
	if (node->parent != ctx->tree)
		return 0;

	alias = dt_get_prop_string(node, "device");
	if (!alias) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: malformed device property in %s\n",
		         __func__, node->name);
		return 0;
	}

	hwnode = dt_lookup_alias(hw_devtree, alias);
	if (!hwnode) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: device %s in %s not found\n",
		         __func__, alias, node->name);
		return 0;
	}

	if (hwnode->parent && hwnode->parent->parent &&
	    !dt_node_is_compatible(hwnode->parent, "simple-bus")) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: don't know how to assign device %s on bus %s\n",
		         __func__, alias, hwnode->parent->name);
		return 0;
	}

	dev_owner_t *owner = malloc(sizeof(dev_owner_t));
	if (!owner) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return ERR_NOMEM;
	}

	spin_lock(&owner_lock);

	/* Hypervisor ownership of a device is exclusive */
	if (ctx->guest) {
		if (!list_empty(&hwnode->owners)) {
			dev_owner_t *other = to_container(hwnode->owners.next,
			                                  dev_owner_t, dev_node);

			if (!other->guest) {
				spin_unlock(&owner_lock);
				printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			   	      "%s: device %s in %s already assigned to the hypervisor\n",
		         		__func__, alias, node->name);
				free(owner);
				return 0;
			}
		}
		
		list_add(&ctx->guest->dev_list, &owner->dev_node);
	} else {
		if (!list_empty(&hwnode->owners)) {
			dev_owner_t *other = to_container(hwnode->owners.next,
			                                  dev_owner_t, dev_node);

			/* If it's already owned when the hv tries to claim it,
			 * it can only be the hv itself that already owns it.
			 */
			assert(!other->guest);
			assert(hwnode->owners.next->next == &hwnode->owners);
			spin_unlock(&owner_lock);
			free(owner);
			return 0;
		}

		list_add(&hv_devs, &owner->dev_node);
	}

	list_add(&hwnode->owners, &owner->dev_node);
	spin_unlock(&owner_lock);

	return 0;
}

/** Assign children of a subtree to the specified partition.
 *
 * @param[in] tree a partition or hv-config node
 * @param[in] guest the guest corresponding to the partition, or NULL
 *   if the node is for the hypervisor.
 */
void dt_assign_devices(dt_node_t *tree, guest_t *guest)
{
	assign_ctx_t ctx = {	
		.tree = tree,
		.guest = guest
	};

	dt_for_each_prop_value(tree, "device", NULL, 0, assign_callback, &ctx);
}
