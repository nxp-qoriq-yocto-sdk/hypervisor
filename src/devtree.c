/** @file
 * Device tree semantic processing
 */

/* Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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

#include <libos/queue.h>
#include <libos/chardev.h>
#include <libos/interrupts.h>
#include <libos/mpic.h>
#include <libos/alloc.h>
#include <libos/console.h>

#include <devtree.h>
#include <paging.h>
#include <errors.h>
#include <byte_chan.h>
#include <limits.h>

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

static int sub_reg(uint32_t *reg, const uint32_t *sub)
{
	int i, borrow = 0;

	for (i = MAX_ADDR_CELLS - 1; i >= 0; i--) {
		int prev_borrow = borrow;
		borrow = reg[i] < sub[i] + prev_borrow;
		reg[i] -= sub[i] + prev_borrow;
	}

	return !borrow;
}

static int add_reg(uint32_t *reg, const uint32_t *add, int naddr)
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
	uint32_t end[MAX_ADDR_CELLS];
	int i;

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		if (reg[i] < range[i])
			return 0;
		if (reg[i] > range[i])
			break;
	}

	memcpy(end, range, sizeof(end));

	/* If the size forces a carry off the final cell, then
	 * reg can't possibly be beyond the end.
	 */
	if (!add_reg(end, rangesize, MAX_ADDR_CELLS))
		return 1;

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		if (reg[i] < end[i])
			return 1;
		if (reg[i] > end[i])
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

		copy_val(range_addr, ranges + i, nregaddr);
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

static chardev_t *cd_console;
static byte_chan_handle_t *bc_console;
queue_t *stdout, *stdin;

int open_stdout_chardev(dt_node_t *node)
{
	chardev_t *cd;

	if (!dt_owned_by(node, NULL)) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: stdout (%s) is not owned by the hypervisor\n",
		         __func__, node->name);

		return ERR_INVALID;
	}

	dt_lookup_regs(node);
	dt_lookup_irqs(node);
	dt_bind_driver(node);

	cd = node->dev.chardev;
	if (!cd) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: stdout (%s) does not expose a character device\n",
		         __func__, node->name);

		return ERR_INVALID;
	}

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
static dt_node_t *stdout_node;

int open_stdout_bytechan(dt_node_t *node)
{
	byte_chan_t *bc;

	if (!dt_node_is_compatible(node, "byte-channel"))
		return ERR_UNHANDLED;

	// We support only two kinds of byte channels: defined either in the
	// guest partition, or defined in the hypervisor config node directly.

	if (!dt_node_is_compatible(node->parent, "partition")) {
		// This byte channel is defined in the hypervisor configuration,
		// not in a partition.

		// An "implicit" connection to a byte channel is a pointer to
		// the byte channel object, and an "explicit" connection is a
		// pointer from the byte channel object (e.g. the 'endpoint'
		// field).  Byte channel objects only support one explicit
		// connection.  For the hypervisor byte channel object, that
		// connection is used for the serial device. We use bc_console
		// to act as an implicit connection because the hypervisor does
		// not manage a list of handles.
		init_byte_channel(node);

		// init_byte_channel() already claims the handle for us, so we
		// don't need to call byte_chan_claim().  We just use the handle
		// we already have.

		// We use bc_console as our implicit connection to the byte
		// channel object.
		bc_console = node->bch;
	} else {
		// This byte channel is defined in a partition.

		stdout_node = create_dev_tree();
		if (!stdout_node)
			return ERR_NOMEM;

		stdout_node->name = strdup("stdout");

		bc = other_attach_byte_chan(node, stdout_node);
		if (!bc)
			return ERR_INVALID;

		bc_console = byte_chan_claim(bc);
		if (!bc_console)
			return ERR_BUSY;
	}

	qconsole_init(bc_console->tx);
	stdout = bc_console->tx;

	// stdin is used only if there's a shell, so that variable will
	// be initialized in shell_init().

	return 0;
}
#endif

#ifdef CONFIG_SHELL
static int open_stdin_chardev(chardev_t *cd)
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
static int open_stdin_bytechan(byte_chan_handle_t *bc)
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
				         "%s: bad interrupt-parent len %zu in %s\n",
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

		if (*nint == 0 || *nint > MAX_INT_CELLS) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "dt_get_int_format: %s has invalid #interrupt-cells\n",
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

		if (*naddr > MAX_ADDR_CELLS) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "dt_get_int_format: %s has invalid #address-cells\n",
			         domain->name);
			return ERR_BADTREE;
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
	uint32_t cpunum;
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

uint32_t dt_owner_lock;
DECLARE_LIST(hv_devs);

static dev_owner_t *dt_owned_by_nolock(dt_node_t *node, struct guest *guest)
{
	list_for_each(&node->owners, i) {
		dev_owner_t *owner = to_container(i, dev_owner_t, dev_node);

		if (owner->guest == guest)
			return owner;
	}

	return NULL;
}

dev_owner_t *dt_owned_by(dt_node_t *node, struct guest *guest)
{
	dev_owner_t *ret;
	register_t saved;

	saved = spin_lock_intsave(&dt_owner_lock);
	ret = dt_owned_by_nolock(node, guest);
	spin_unlock_intsave(&dt_owner_lock, saved);

	return ret;
}

typedef struct assign_ctx {
	dt_node_t *tree;
	guest_t *guest;
} assign_ctx_t;

typedef struct assignportal_ctx {
	dt_node_t *hwnode;
	uint32_t pcpu_num;
} assignportal_ctx_t;

#ifdef CONFIG_CLAIMABLE_DEVICES
static void set_claimable(dev_owner_t *owner)
{
	const char *str = dt_get_prop_string(owner->cfgnode, "claimable");

	if (!owner->guest && str) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: %s: hv-owned devices cannot be claimable\n",
		         __func__, owner->cfgnode->name);
		return;
	}
	
	if (!str)
		owner->claimable = not_claimable;
	else if (!strcmp(str, "active"))
		owner->claimable = claimable_active;
	else if (!strcmp(str, "standby"))
		owner->claimable = claimable_standby;
	else {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s in %s: unrecognized claimable state: %s\n",
		         owner->cfgnode->name, owner->guest->name, str);

		owner->claimable =  not_claimable;
	}
}

void check_compatible_owners(dev_owner_t *new, dev_owner_t *old)
{
	/* If claimable and non-claimable owners are mixed, we warn the
	 * user, and it is undefined which partition ends up with
	 * interrupts and DMA, either initially or after a claim hcall.
	 *
	 * If multiple claimable owners try to be initially active,
	 * we warn the user and arbitrarily pick one to be active.
	 */
	if (!new->claimable && old->claimable) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: not claimable in %s, but %s owns claimably\n",
		         new->hwnode->name, new->guest->name, old->guest->name);
	} else if (!old->claimable && new->claimable) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: not claimable in %s, but %s owns claimably\n",
		         new->hwnode->name, old->guest->name, new->guest->name);
	} else if (new->claimable == claimable_active &&
	           old->claimable == claimable_active) { 
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: %s and %s are both active owners\n",
		         new->hwnode->name, old->guest->name, new->guest->name);
		new->claimable = claimable_standby;
	}
}
#endif

static int assign_callback(dt_node_t *node, void *arg)
{
	assign_ctx_t *ctx = arg;
	const char *alias;
	dt_node_t *hwnode;
	register_t saved;

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

	if (!hwnode->parent) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: cannot assign the root node %s from %s\n",
		         __func__, alias, node->name);
		return 0;
	}

	dev_owner_t *owner = alloc_type(dev_owner_t);
	if (!owner) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return ERR_NOMEM;
	}

	owner->cfgnode = node;
	owner->hwnode = hwnode;
	owner->guest = ctx->guest;
	owner->direct = owner;
#ifdef CONFIG_CLAIMABLE_DEVICES
	set_claimable(owner);
#endif

	saved = spin_lock_intsave(&dt_owner_lock);

	list_for_each(&hwnode->owners, i) {
		dev_owner_t *other = to_container(i, dev_owner_t, dev_node);

		/* Hypervisor ownership of a device is exclusive */
		if (!other->guest) {
			spin_unlock_intsave(&dt_owner_lock, saved);
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: device %s in %s already assigned to the hypervisor\n",
			         __func__, alias, node->name);
			free(owner);
			return 0;
		}

		if (other->guest == ctx->guest) {
			spin_unlock_intsave(&dt_owner_lock, saved);
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: device %s in %s already assigned to %s\n",
			         __func__, alias, node->name, ctx->guest->name);
			free(owner);
			return 0;
		}

		check_compatible_owners(owner, other);
	}

	if (ctx->guest)
		list_add(&ctx->guest->dev_list, &owner->guest_node);
	else
		list_add(&hv_devs, &owner->guest_node);

	node->endpoint = hwnode;

	list_add(&hwnode->owners, &owner->dev_node);
	spin_unlock_intsave(&dt_owner_lock, saved);

	return 0;
}

/** Read aliases and attach them to the nodes they point to. */
void dt_read_aliases(void)
{
	dt_node_t *aliases;

	aliases = dt_get_subnode(hw_devtree, "aliases", 0);
	if (!aliases)
		return;

	list_for_each(&aliases->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);
		dt_node_t *node;
		alias_t *alias;
		const char *str = prop->data;

		if (prop->len == 0 || str[prop->len - 1] != 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: Bad alias value in %s\n", __func__, prop->name);
			continue;
		}

		node = dt_lookup_path(hw_devtree, str, 0);
		if (!node) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: Alias %s points to non-existent %s\n",
			         __func__, prop->name, str);
			continue;
		}

		alias = malloc(sizeof(alias_t));
		if (!alias) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: out of memory\n", __func__);

			return;
		}

		alias->name = prop->name;
		list_add(&node->aliases, &alias->list_node);
	}
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

/** Read the memory resources of the given node */
void dt_lookup_regs(dt_node_t *node)
{
	dt_prop_t *prop;
	uint32_t naddr, nsize;
	const uint32_t *reg;
	register_t saved;
	int ret;

	saved = spin_lock_intsave(&dt_owner_lock);

	if (node->dev.regs)
		goto out;

	prop = dt_get_prop(node, "reg", 0);
	if (!prop)
		goto out;

	ret = get_addr_format_nozero(node->parent, &naddr, &nsize);
	if (ret < 0)
		goto out;

	reg = prop->data;
	node->dev.num_regs = prop->len / ((naddr + nsize) * 4);

	if (prop->len % ((naddr + nsize) * 4) != 0)
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: Ignoring junk at end of reg in %s\n",
		         __func__, node->name);

	node->dev.regs = alloc_type_num(mem_resource_t, node->dev.num_regs);
	if (!node->dev.regs) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		goto out;
	}

	for (int i = 0; i < node->dev.num_regs; i++) {
		phys_addr_t addr, size;

		ret = xlate_reg(node, &reg[(naddr + nsize) * i], &addr, &size);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d on reg resource %d in %s\n",
			         __func__, ret, i, node->name);
			continue;
		}

		node->dev.regs[i].start = addr;
		node->dev.regs[i].size = size;

		/* Create virtual mappings for HV-owned devices */
		if (dt_owned_by_nolock(node, NULL))
			node->dev.regs[i].virt = map(addr, size,
			                             TLB_MAS2_IO, TLB_MAS3_KERN);
	}

out:
	spin_unlock_intsave(&dt_owner_lock, saved);
}

int dt_bind_driver(dt_node_t *node)
{
	dt_prop_t *compat;

	/* Don't bind twice. */
	if (node->dev.driver)
		return 0;

	/* Don't bind if we don't own it. */
	if (!dt_owned_by_nolock(node, NULL))
		return ERR_INVALID;

	compat = dt_get_prop(node, "compatible", 0);
	if (!compat)
		return ERR_UNHANDLED;

	return libos_bind_driver(&node->dev, compat->data, compat->len);
}

/* Maximum nesting of IRQ controllers, to avoid stack overruns
 * and detect interrupt loops.
 */
#define MAX_IRQ_DEPTH 5

static void dt_lookup_irqs_nolock(dt_node_t *node, int depth);

/* Read raw intmap data into C data structures.  addr/int cells
 * are looked up, but otherwise domain phandles are not followed
 * so as to avoid problems with circular maps (e.g. two maps that
 * refer to each other, without any single interrupt following a
 * circular path).  interrupt_t lookups happen later.
 */
static void read_intmap(dt_node_t *node)
{
	dt_prop_t *imap_prop, *mask_prop;
	uint32_t naddr, nint, nimap;
	uint32_t *imap, *imap_end;
	int ret;

	ret = dt_get_int_format(node, &nint, &naddr);
	if (ret < 0)
		return;

	nimap = naddr + nint;

	imap_prop = dt_get_prop(node, "interrupt-map", 0);
	if (!imap_prop)
		return;

	mask_prop = dt_get_prop(node, "interrupt-map-mask", 0);
	if (mask_prop) {
		if (mask_prop->len != (nint + naddr) * 4) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: %s: Invalid interrupt-map-mask length\n",
			         __func__, node->name);
			return;
		}

		memcpy(node->intmap_mask, mask_prop->data, mask_prop->len);
	} else {
		memset(node->intmap_mask, 0xff, sizeof(node->intmap_mask));
	}

	/* Estimate an upper bound for number of interrupt-map entries. */
	node->intmap_len = imap_prop->len / (nint + naddr + 1);

	node->intmap = alloc_type_num(intmap_entry_t, node->intmap_len);
	if (!node->intmap) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return;
	}

	imap = imap_prop->data;
	imap_end = imap_prop->data + imap_prop->len;

	for (int i = 0; i < node->intmap_len; i++) {
		intmap_entry_t *ent = &node->intmap[i];
		dt_node_t *domain;

		if (imap + naddr + nint + 1 > imap_end)
			break;

		memcpy(ent->intspec, imap, (naddr + nint) * 4);
		ent->parent = imap[naddr + nint];

		domain = dt_lookup_phandle(hw_devtree, ent->parent);
		if (!domain) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: %s: Invalid phandle %x in interrupt-map entry %d\n",
			         __func__, node->name, ent->parent, i);
			break;
		}

		imap += naddr + nint + 1;

		ret = dt_get_int_format(domain, &ent->parent_nint,
		                        &ent->parent_naddr);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: %s: Bad addr/int cells in %s, entry %d\n",
			         __func__, node->name, domain->name, i);
			break;
		}

		if (imap + ent->parent_naddr + ent->parent_nint > imap_end) {
			memset(ent, 0, sizeof(ent));
			break;
		}

		memcpy(ent->parent_intspec, imap,
		       (ent->parent_naddr + ent->parent_nint) * 4);

		imap += ent->parent_naddr + ent->parent_nint;
		ent->valid = 1;
	}
}

static interrupt_t *lookup_irq(dt_node_t *domain, const uint32_t *intspec,
                               uint32_t nint)
{
	interrupt_t *irq;
	int_ops_t *ctrl;

	ctrl = domain->dev.irqctrl;
	if (!ctrl) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: %s does not expose an interrupt controller\n",
		         __func__, domain->name);

		return NULL;
	}

	irq = ctrl->get_irq(&domain->dev, intspec, nint);
	if (!irq) {
		/* For simplicity, we assume the first cell of the intspec is
		 * the most meaningful.
		 */
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: cannot register interrupt %u on %s\n",
		         __func__, *intspec, domain->name);
	}

	return irq;
}

static intmap_entry_t *lookup_intmap(dt_node_t *domain,
                                     const uint32_t *intspec,
                                     uint32_t nispec)
{
	if (!domain->intmap)
		return NULL;

	for (int i = 0; i < domain->intmap_len; i++) {
		intmap_entry_t *ent = &domain->intmap[i];
		unsigned int j;

		if (!ent->valid)
			break;

		for (j = 0; j < nispec; j++)
			if ((ent->intspec[j] & domain->intmap_mask[j]) !=
			    (intspec[j] & domain->intmap_mask[j]))
				break;

		if (j == nispec)
			return ent;
	}

	return NULL;
}

static interrupt_t *lookup_intmap_entry(dt_node_t *node,
                                        intmap_entry_t *ent, int depth)
{
	dt_node_t *domain;

	if (depth > MAX_IRQ_DEPTH) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: IRQ nesting too deep in %s\n",
		         __func__, node->name);
		ent->valid = 0;
		return NULL;
	}

	if (ent->irq)
		return ent->irq;

	domain = dt_lookup_phandle(hw_devtree, ent->parent);
	if (!domain) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: %s: Invalid phandle %x in interrupt-map entry %zd\n",
		         __func__, node->name, ent->parent, ent - node->intmap);
		ent->valid = 0;
		return NULL;
	}

	if (!dt_owned_by_nolock(domain, NULL))
		return NULL;

	dt_lookup_irqs_nolock(domain, depth + 1);
	dt_bind_driver(domain);

	if (domain->dev.irqctrl) {
		ent->irq = lookup_irq(domain,
		                      ent->parent_intspec + ent->parent_naddr,
		                      ent->parent_nint);
		return ent->irq;
	}

	if (domain->intmap) {
		intmap_entry_t *next = lookup_intmap(domain, ent->parent_intspec,
		                                     ent->parent_naddr +
		                                     ent->parent_nint);
		if (!next) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: No interrupt-map match in %s for %s, intmap %zd\n",
			         __func__, domain->name, node->name, ent - node->intmap);
			ent->valid = 0;
			return NULL;
		}

		ent->irq = lookup_intmap_entry(domain, next, depth + 1);
		return ent->irq;
	}

	/* HV-owned, no supported IRQ controller, and no interrupt map */
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: %s intmap %zd points to invalid domain %s\n",
	         __func__, node->name, ent - node->intmap, domain->name);

	ent->valid = 0;
	return NULL;
}

static void lookup_irqs_in_intmap(dt_node_t *node, dt_node_t *domain,
                                  const uint32_t *ints,
                                  uint32_t naddr, uint32_t nint, int depth)
{
	uint32_t intspec[MAX_ADDR_CELLS + MAX_INT_CELLS];
	uint32_t *intspec_afteraddr = intspec;
	dt_prop_t *reg;
	uint32_t dtparent_naddr, dtparent_nsize;

	if (!domain->intmap)
		read_intmap(domain);

	if (naddr > 0) {
		int ret = get_addr_format(node->parent,
		                          &dtparent_naddr,
		                          &dtparent_nsize);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: get_addr_format failed %d for %s\n",
			         __func__, ret, node->name);
			return;
		}

		if (naddr != dtparent_naddr) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: address cell mismatch (%u/%u) for %s\n",
			         __func__, naddr, dtparent_naddr, node->name);
			return;
		}

		reg = dt_get_prop(node, "reg", 0);
		if (!reg || reg->len < naddr * 4) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: missing/invaid reg in %s\n",
			         __func__, node->name);
			return;
		}

		memcpy(intspec, reg->data, naddr * 4);
		intspec_afteraddr += naddr;
	}

	for (int i = 0; i < node->dev.num_irqs; i++) {
		intmap_entry_t *ent;

		memcpy(intspec_afteraddr, &ints[i * nint], nint * 4);

		ent = lookup_intmap(domain, intspec, naddr + nint);
		if (!ent) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: No interrupt-map match in %s for %s, int %d\n",
			         __func__, domain->name, node->name, i);
			continue;
		}

		node->dev.irqs[i] = lookup_intmap_entry(domain, ent, depth);
	}
}

static void dt_lookup_irqs_nolock(dt_node_t *node, int depth)
{
	dt_prop_t *prop;
	uint32_t nint, naddr;
	const uint32_t *ints;
	dt_node_t *domain;
	int ret;

	if (node->irqs_looked_up)
		return;

	if (depth > MAX_IRQ_DEPTH) {
		printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
		         "%s: IRQ nesting too deep in %s\n",
		         __func__, node->name);
		return;
	}

	node->irqs_looked_up = 1;

	if (!node->intmap && dt_get_prop(node, "interrupt-map", 0))
		read_intmap(node);

	prop = dt_get_prop(node, "interrupts", 0);
	if (!prop)
		return;

	domain = get_interrupt_domain(hw_devtree, node);
	if (!domain) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: %s has interrupts but no interrupt-parent\n",
		         __func__, node->name);
		return;
	}

	ret = dt_get_int_format(domain, &nint, &naddr);
	if (ret < 0)
		return;

	/* Only process hv-owned IRQ controllers and nexuses */
	if (!dt_owned_by_nolock(domain, NULL))
		return;

	ints = prop->data;
	node->dev.num_irqs = prop->len / (nint * 4);

	if (prop->len % (nint * 4) != 0)
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: Ignoring junk at end of interrupts in %s\n",
		         __func__, node->name);

	node->dev.irqs = alloc_type_num(interrupt_t *, node->dev.num_irqs);
	if (!node->dev.irqs) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: out of memory\n", __func__);
		return;
	}

	dt_lookup_irqs_nolock(domain, depth + 1);
	dt_bind_driver(domain);

	if (dt_get_prop(domain, "interrupt-controller", 0)) {
		for (int i = 0; i < node->dev.num_irqs; i++)
			node->dev.irqs[i] = lookup_irq(domain, &ints[i * nint], nint);
	} else {
		lookup_irqs_in_intmap(node, domain, ints, naddr, nint, depth);
	}
}

void dt_lookup_irqs(dt_node_t *node)
{
	register_t saved = spin_lock_intsave(&dt_owner_lock);
	dt_lookup_irqs_nolock(node, 0);
	spin_unlock_intsave(&dt_owner_lock, saved);
}

void dt_lookup_intmap(dt_node_t *node)
{
	register_t saved = spin_lock_intsave(&dt_owner_lock);

	if (!node->intmap)
		read_intmap(node);

	if (node->intmap)
		for (int i = 0; i < node->intmap_len; i++)
			if (node->intmap[i].valid)
				lookup_intmap_entry(node, &node->intmap[i], 0);

	spin_unlock_intsave(&dt_owner_lock, saved);
}

/* We assume the timebase is the same on all cores. */
uint64_t dt_get_timebase_freq(void)
{
	uint64_t ret;
	dt_prop_t *prop;
	dt_node_t *node;

	node = get_cpu_node(hw_devtree, 0);
	if (!node) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: Could not get device tree node for CPU0\n", __func__);
		return 0;
	}

	prop = dt_get_prop(node, "timebase-frequency", 0);
	if (!prop || (prop->len != 4 && prop->len != 8)) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: No timebase-frequency for CPU0\n", __func__);
		return 0;
	}

	if (prop->len == 4)
		return *(const uint32_t *)prop->data;

	return *(const uint64_t *)prop->data;
}
