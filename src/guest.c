/** @file
 * Guest management
 */

/*
 * Copyright (C) 2007-2009 Freescale Semiconductor, Inc.
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
#include <libos/trap_booke.h>
#include <libos/thread.h>
#include <libos/alloc.h>

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
#include <ccm.h>
#include <debug-stub.h>

guest_t guests[MAX_PARTITIONS];
unsigned long last_lpid;

int vcpu_to_cpu(const uint32_t *cpulist, unsigned int len, unsigned int vcpu)
{
	unsigned int i, vcpu_base = 0;

	for (i = 0; i < len / 4; i += 2) {
		if (vcpu >= vcpu_base && vcpu < vcpu_base + cpulist[i + 1])
			return cpulist[i] + vcpu - vcpu_base;

		vcpu_base += cpulist[i + 1];
	}

	return ERR_RANGE;
}

static int cpu_in_cpulist(const uint32_t *cpulist, unsigned int len,
                          unsigned int cpunum)
{
	unsigned int i;
	for (i = 0; i < len / 4; i += 2) {
		if (cpunum >= cpulist[i] && cpunum < cpulist[i] + cpulist[i + 1])
			return 1;
	}

	return 0;
}

static int get_gcpu_num(const uint32_t *cpulist, unsigned int len,
                        unsigned int cpunum)
{
	unsigned int i;
	unsigned int total = 0;

	for (i = 0; i < len / 4; i += 2) {
		unsigned int base = cpulist[i];
		unsigned int num = cpulist[i + 1];

		if (cpunum >= base && cpunum < base + num)
			return total + cpunum - base;

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

static void map_guest_addr_range(guest_t *guest, phys_addr_t gaddr,
				phys_addr_t addr, phys_addr_t size,
				uint32_t exc_flags)
{
	unsigned long grpn = gaddr >> PAGE_SHIFT;
	unsigned long rpn = addr >> PAGE_SHIFT;
	unsigned long pages = (gaddr + size -
	                       ((phys_addr_t)grpn << PAGE_SHIFT) +
	                       (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	uint32_t flags = PTE_ALL & ~exc_flags;

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_DEBUG,
	         "mapping guest %lx to real %lx, %lx pages\n",
	         grpn, rpn, pages);

	vptbl_map(guest->gphys, grpn, rpn, pages, flags, PTE_PHYS_LEVELS);
	vptbl_map(guest->gphys_rev, rpn, grpn, pages, flags, PTE_PHYS_LEVELS);
}

void map_dev_range(guest_t *guest, phys_addr_t addr, phys_addr_t size)
{
	uint32_t tmpaddr[MAX_ADDR_CELLS];
	phys_addr_t rangesize;
	int ret;

	if (!guest->devranges) {
		map_guest_addr_range(guest, addr, addr, size, 0);
		return;
	}
	
	while (size > 0) {
		val_from_int(tmpaddr, addr);

		ret = xlate_one(tmpaddr, guest->devranges->data,
		                guest->devranges->len, 2, 2, 2, 2, &rangesize);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: 0x%llx does not translate through device-ranges\n",
			         __func__, addr);
			return;
		}

		phys_addr_t cursize = min(size, rangesize);
		phys_addr_t gaddr = ((phys_addr_t)tmpaddr[MAX_ADDR_CELLS - 2] << 32) |
		                    tmpaddr[MAX_ADDR_CELLS - 1];
		map_guest_addr_range(guest, gaddr, addr, cursize, 0);

		size -= cursize;
		addr += cursize;
	}
}

static uint32_t *write_reg(uint32_t *reg, phys_addr_t start, phys_addr_t size)
{
	if (rootnaddr == 2)
		*reg++ = start >> 32;

	*reg++ = start & 0xffffffff;

	if (rootnsize == 2)
		*reg++ = size >> 32;

	*reg++ = size & 0xffffffff;

	return reg;
}

static int map_gpma_callback(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	dt_prop_t *prop;
	dt_node_t *gnode, *pma_node;
	pma_t *pma;
	phys_addr_t gaddr;
	uint32_t reg[4];
	char buf[32];
	uint32_t exc_flags = 0;

	pma_node = get_pma_node(node);
	if (!pma_node || !pma_node->pma)
		return 0;

	if (dt_owned_by(pma_node, NULL)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: phys mem area %s owned by hypervisor and %s\n",
		         __func__, pma_node->name, guest->name);
		return 0;
	}

	pma = pma_node->pma;

	if (dt_get_prop(node, "dma-only", 0))
		exc_flags |= PTE_VALID;

	if (dt_get_prop(node, "read-only", 0))
		exc_flags |= PTE_SW | PTE_UW;

	prop = dt_get_prop(node, "guest-addr", 0);
	if (prop) {
		if (prop->len != 8) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: bad guest-addr in %s\n", __func__, node->name);
			return 0;
		}

		gaddr = *(uint64_t *)prop->data;

		if (gaddr & (pma->size - 1)) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: guest phys %s is not naturally aligned\n",
			         __func__, node->name);

			return 0;
		}
	} else {
		gaddr = pma->start;
	}

	map_guest_addr_range(guest, gaddr, pma->start, pma->size, exc_flags);

	if (!(exc_flags & PTE_VALID)) {
		add_cpus_to_csd(guest, pma_node);

		snprintf(buf, sizeof(buf), "memory@%llx", gaddr);

		gnode = dt_get_subnode(guest->devtree, buf, 1);
		if (!gnode)
			goto nomem;

		dt_record_guest_phandle(gnode, node);

		if (dt_set_prop_string(gnode, "device_type", "memory"))
			goto nomem;

		write_reg(reg, gaddr, pma->size);
		if (dt_set_prop(gnode, "reg", reg, (rootnaddr + rootnsize) * 4))
			goto nomem;

		if (dt_process_node_update(guest, gnode, node))
			goto nomem;
	}

	return 0;

nomem:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}

static void map_guest_mem(guest_t *guest)
{
	dt_for_each_compatible(guest->partition, "guest-phys-mem-area",
	                       map_gpma_callback, guest);
}

static int map_guest_ranges(dev_owner_t *owner)
{
	size_t len;
	uint32_t naddr, nsize, caddr, csize;
	dt_prop_t *prop;
	const uint32_t *ranges;
	uint32_t *newranges, *newranges_base;
	dt_node_t *hwnode = owner->hwnode;
	int map_ranges = 0;
	int ret;

	prop = dt_get_prop(owner->gnode, "ranges", 0);
	if (!prop)
		return 0;

	if (dt_get_prop(owner->cfgnode, "map-ranges", 0) &&
	    owner->direct == owner)
		map_ranges = 1;

	/* Don't parse the ranges at all if it's a non-reparented child
	 * without map-ranges.
	 */
	if (!map_ranges && owner->gnode->parent != owner->guest->devices)
		return 0;

	dt_node_t *parent = hwnode->parent;
	if (!parent) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: Cannot have ranges in root node\n", __func__);
		return ERR_BADTREE;
	}

	ret = get_addr_format_nozero(parent, &naddr, &nsize);
	if (ret < 0)
		return ret;

	ret = get_addr_format_nozero(hwnode, &caddr, &csize);
	if (ret < 0)
		return ret;

	ranges = prop->data;
	len = prop->len / ((caddr + naddr + csize) * 4);

	if (prop->len % ((caddr + naddr + csize) * 4) != 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: Ignoring junk at end of ranges in %s\n",
		         __func__, hwnode->name);
	}

	newranges = alloc(len * (caddr + rootnaddr + csize) * 4, 8);
	if (!newranges)
		return ERR_NOMEM;

	newranges_base = newranges;
	for (unsigned int i = 0; i < len; i++, ranges += caddr + naddr + csize) {
		uint32_t addrbuf[MAX_ADDR_CELLS];
		phys_addr_t addr, size;

		ret = xlate_reg_raw(hwnode, ranges + caddr, addrbuf, &size,
		                    naddr, csize);
		if (ret < 0) {
			printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
			         "%s: error %d on range %d in %s\n",
			         __func__, ret, i, hwnode->name);
			continue;
		}

		assert(!addrbuf[0] && !addrbuf[1]);
		addr = ((uint64_t)addrbuf[2] << 32) | addrbuf[3];

		map_dev_range(owner->guest, addr, size);

		memcpy(newranges, ranges, caddr * 4);
		newranges += caddr;

		memcpy(newranges, addrbuf + 4 - rootnaddr, rootnaddr * 4);
		newranges += rootnaddr;

		memcpy(newranges, ranges + caddr + naddr, csize * 4);
		newranges += csize;
	}

	/* Only change ranges if this is a top-level guest node */
 	if (owner->gnode->parent != owner->guest->devices)
 		return 0;

	return dt_set_prop(owner->gnode, "ranges", newranges_base,
	                   (newranges - newranges_base) * sizeof(newranges[0]));
}

#ifdef CONFIG_PAMU
static dt_node_t *find_cfgnode(dt_node_t *hwnode, dev_owner_t *owner)
{
	dt_prop_t *prop;

	if (!dt_node_is_compatible(hwnode->parent, "fsl,qman-portal")) {
		return owner->cfgnode;  /* normal case */
	}

	/* if parent of hwnode is fsl,qman-portal it's a special case */

	/* iterate over all children of the config node */
	list_for_each(&owner->cfgnode->children, i) {
		dt_node_t *config_child = to_container(i, dt_node_t, child_node);

		/* look for nodes with a "device" property */
		const char *s = dt_get_prop_string(config_child, "device");
		if (!s)
			continue;

		dt_node_t *devnode = dt_lookup_alias(hw_devtree, s);
		if (!devnode) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: missing device (%s) reference at %s\n",
			         __func__, s, config_child->name);
			continue;
		}
		uint32_t hw_phandle_cfg = dt_get_phandle(devnode, 0);

		prop = dt_get_prop(hwnode, "dev-handle", 0);
		if (prop) {
			if (prop->len != 4) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: missing/bad dev-handle on %s\n",
				         __func__, owner->hwnode->name);
				continue;
			}
			uint32_t hw_phandle = *(const uint32_t *)prop->data;

			if (hw_phandle == hw_phandle_cfg)
				return config_child; /* found match */
		}
	}

	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: no config node found for %s\n",
	         __func__, owner->hwnode->name);

	return NULL;
}

typedef struct search_liodn_ctx {
	dt_node_t *cfgnode;
	uint32_t liodn_index;
} search_liodn_ctx_t;

static int search_liodn_index(dt_node_t *cfgnode, void *arg)
{
	search_liodn_ctx_t *ctx = arg;
	dt_prop_t *prop;

	prop = dt_get_prop(cfgnode, "liodn-index", 0);
	if (prop) {
		if (prop->len != 4) {
			ctx->cfgnode = NULL;
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: bad liodn-index on %s\n",
			         __func__, cfgnode->name);
			return 0;
		}
		if (ctx->liodn_index == *(const uint32_t *)prop->data) {
			ctx->cfgnode = cfgnode;
			return 1; /* found match */
		}
	}

	return 0;
}

static int configure_dma(dt_node_t *hwnode, dev_owner_t *owner)
{
	dt_node_t *cfgnode;
	dt_prop_t *liodn_prop = NULL;
	const uint32_t *liodn;
	int liodn_cnt, i;
	int ret;
	uint32_t *dma_handles = NULL;
	pamu_handle_t *pamu_handle;

	cfgnode = find_cfgnode(hwnode, owner);
	if (!cfgnode)
		return 0;

	/* get the liodn property on the hw node */
	liodn_prop = dt_get_prop(hwnode, "fsl,liodn", 0);
	if (!liodn_prop)
		return 0;  /* continue */

	if (liodn_prop->len & 3 || liodn_prop->len == 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: bad liodn on %s\n",
		         __func__, hwnode->name);
		return 0;
	}
	liodn = (const uint32_t *)liodn_prop->data;

	/* get a count of liodns */
	liodn_cnt = liodn_prop->len / 4;

	dma_handles = malloc(liodn_cnt * sizeof(uint32_t));
	if (!dma_handles)
		goto nomem;

	for (i = 0; i < liodn_cnt; i++) {
		search_liodn_ctx_t ctx;

		/* search for an liodn-index that matches this liodn */
		ctx.liodn_index = i;
		ctx.cfgnode = cfgnode;  /* default */
		dt_for_each_node(cfgnode, &ctx, search_liodn_index, NULL);

		if (!ctx.cfgnode) { /* error */
			free(dma_handles);
			return 0;
		}

		ret = pamu_config_liodn(owner->guest, liodn[i], hwnode, ctx.cfgnode);
		if (ret < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: config of liodn failed (rc=%d)\n",
			         __func__, ret);
			free(dma_handles);
			return 0;
		}

		pamu_handle = alloc_type(pamu_handle_t);
		if (!pamu_handle)
			goto nomem;

		pamu_handle->assigned_liodn = liodn[i];
		pamu_handle->user.pamu = pamu_handle;
		dma_handles[i] = alloc_guest_handle(owner->guest,
							&pamu_handle->user);
	}

	if (dt_set_prop(owner->gnode, "fsl,hv-dma-handle", dma_handles,
				i * sizeof(uint32_t)) < 0)
		goto nomem;

	free(dma_handles);
	return 0;

nomem:
	free(dma_handles);
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
        	 "%s: out of memory\n", __func__);
	return ERR_NOMEM;
}
#endif

static int map_guest_reg(dev_owner_t *owner)
{
	mem_resource_t *regs;
	uint32_t *reg, *regp;
	dt_node_t *hwnode = owner->hwnode;
	int regsize = (rootnaddr + rootnsize) * 4;
	int num, ret = 0;

	if (!dt_get_prop(owner->gnode, "reg", 0))
		return 0;

	if (hwnode->parent->parent &&
	    !dt_node_is_compatible(hwnode->parent, "simple-bus"))
		return 0;

	dt_lookup_regs(hwnode);

	regs = hwnode->dev.regs;
	num = hwnode->dev.num_regs;

	if (num == 0)
		return 0;

	for (int i = 0; i < num; i++)
		map_dev_range(owner->guest, regs[i].start, regs[i].size);

	/* Only change reg if this is a top-level guest node */
 	if (owner->gnode->parent == owner->guest->devices) {
		reg = regp = malloc(num * regsize);
		if (!reg)
			return ERR_NOMEM;

		for (int i = 0; i < num; i++)
			regp = write_reg(regp, regs[i].start, regs[i].size);

		ret = dt_set_prop(owner->gnode, "reg", reg, num * regsize);
		free(reg);
		
		if (ret < 0)
			return ret;
	}

	return 0;
}

/** Convert aliases in the config or hardware tree to aliases in the guest tree.
 *
 * @param[in] node config or hardware node which may have aliases
 * @param[in] gnode guest node that corresponds to node, to which
 *    the new aliases should point
 * @param[in] tree tree in which to create the aliases
 */
void create_aliases(dt_node_t *node, dt_node_t *gnode, dt_node_t *tree)
{
	dt_node_t *aliases;
	char path_buf[MAX_DT_PATH];
	int len;

	len = dt_get_path(NULL, gnode, path_buf, sizeof(path_buf));
	if (len > (int)sizeof(path_buf)) {
		printlog(LOGTYPE_PARTITION, LOGTYPE_MISC,
 		         "%s: %s path too long for alias\n",
 		         __func__, node->name);
		return;
	}

	aliases = dt_get_subnode(tree, "aliases", 1);
	if (!aliases)
		goto nomem;

	list_for_each(&node->aliases, i) {
		alias_t *alias = to_container(i, alias_t, list_node);

		if (dt_set_prop(aliases, alias->name, path_buf, len) < 0)
			goto nomem;
	}

	return;

nomem:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

static int map_guest_irqs(dev_owner_t *owner)
{
	dt_node_t *hwnode = owner->hwnode;
	int ret;

	if (!dt_get_prop(owner->gnode, "interrupts", 0))
		return 0;

	if (hwnode->dev.num_irqs <= 0)
		return 0;

	uint32_t *intspec = malloc(hwnode->dev.num_irqs * 8);
	if (!intspec)
		return ERR_NOMEM;

	for (int i = 0; i < hwnode->dev.num_irqs; i++) {
		interrupt_t *irq = hwnode->dev.irqs[i];
		int handle;

		if (!irq)
			goto bad;

		/* FIXME: handle more than just mpic */
		handle = vmpic_alloc_mpic_handle(owner->guest, irq);
		if (handle < 0) {
bad:
			printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
			         "%s: couldn't grant interrupt %d for %s\n",
			         __func__, i, hwnode->name);

 			intspec[i * 2 + 0] = 0xffffffff;
 			intspec[i * 2 + 1] = 0xffffffff;

			continue;
		}

		intspec[i * 2 + 0] = handle;
		intspec[i * 2 + 1] = hwnode->dev.irqs[i]->config;
		if (owner->guest->mpic_direct_eoi) {
			intspec[i * 2 + 1] |= IRQ_TYPE_MPIC_DIRECT;
			((vmpic_interrupt_t *)irq->priv)->config |=
						IRQ_TYPE_MPIC_DIRECT;
		}
	}

	ret = dt_set_prop(owner->gnode, "interrupts", intspec,
	                  hwnode->dev.num_irqs * 8);
	free(intspec);
	if (ret < 0)
		return ERR_NOMEM;

	ret = dt_set_prop(owner->gnode, "interrupt-parent",
	                  &owner->guest->vmpic_phandle, 4);
	if (ret < 0)
		return ERR_NOMEM;

	return 0;
}

static int calc_new_imaplen(dt_node_t *node, uint32_t naddr, uint32_t nint)
{
	int newmaplen = 0;

	for (int i = 0; i < node->intmap_len; i++) {
		intmap_entry_t *ent = &node->intmap[i];

		if (!ent->valid)
			continue;

		newmaplen += nint + naddr + 1;

		if (ent->irq)
			newmaplen += 2; /* VMPIC */
		else
			newmaplen += ent->parent_naddr + ent->parent_nint;
	}

	return newmaplen;
}

static int patch_guest_intmaps(dev_owner_t *owner)
{
	dt_node_t *hwnode = owner->hwnode;
	int ret, newmaplen;
	uint32_t *newmap, *mapptr;
	uint32_t naddr, nint;
	dt_prop_t *gimap;

	gimap = dt_get_prop(owner->gnode, "interrupt-map", 0);
	if (!gimap)
		return 0;

	if (!hwnode->intmap)
		return 0;

	dt_lookup_intmap(hwnode);

	ret = dt_get_int_format(hwnode, &nint, &naddr);
	if (ret < 0)
		return ret;

	newmaplen = calc_new_imaplen(hwnode, naddr, nint);
	if (newmaplen == 0) {
		dt_delete_prop(gimap);
		return 0;
	}

	mapptr = newmap = malloc(newmaplen * 4);
	if (!newmap)
		return ERR_NOMEM;

	for (int i = 0; i < hwnode->intmap_len; i++) {
		intmap_entry_t *ent = &hwnode->intmap[i];
		int handle = -1;

		if (!ent->valid)
			continue;

		if (ent->irq) {
			/* FIXME: handle more than just mpic */
			handle = vmpic_alloc_mpic_handle(owner->guest, ent->irq);
			if (handle < 0) {
				printlog(LOGTYPE_IRQ, LOGLEVEL_ERROR,
				         "%s: couldn't grant imap entry %d for %s\n",
				         __func__, i, hwnode->name);
				continue;
			}
		}

		memcpy(mapptr, ent->intspec, (naddr + nint) * 4);
		mapptr += naddr + nint;

		if (ent->irq) {
			*mapptr++ = owner->guest->vmpic_phandle;
			*mapptr++ = handle;
			*mapptr++ = ent->irq->config;
		} else {
			int parentlen = ent->parent_naddr + ent->parent_nint;

			*mapptr++ = ent->parent;

			memcpy(mapptr, ent->parent_intspec, parentlen * 4);
			mapptr += parentlen;
		}

		assert(mapptr <= newmap + newmaplen);
	}

	ret = dt_set_prop(owner->gnode, "interrupt-map", newmap, newmaplen * 4);
	free(newmap);
	return ret;
}

typedef struct {
	unsigned int cpu;
	dt_node_t *node;
} cpu_to_node_t;

static int do_cpu_to_node(dt_node_t *node, void *arg)
{
	cpu_to_node_t *ctx = arg;
	dt_prop_t *prop;

	prop = dt_get_prop_len(node, "reg", sizeof(uint32_t));
	if (prop)
		if ((* ((uint32_t *) (prop->data))) == ctx->cpu) {
			ctx->node = node;
			return 1;
		}

	return 0;
}

/**
 * cpu_to_node - return the HW node that corresponds to the given cpu number
 * @param[in] pcpu the physical CPU number
 */
static dt_node_t *cpu_to_node(unsigned int pcpu)
{
	cpu_to_node_t ctx = { .cpu = pcpu };
	dt_node_t *node = dt_get_subnode(hw_devtree, "cpus", 0);

	dt_for_each_prop_value(node, "device_type", "cpu", sizeof("cpu"),
			       do_cpu_to_node, &ctx);

	return ctx.node;
}

/**
 * configure_portal - configure the vcpu property of a Qman or Bman portal node
 * @param[in] guest the guest
 * @param[in] cfgnode the configuration node
 * @param[in] hwnode the corresponding hardware node
 * @param[in] gnode the corresponding guest node
 *
 * Check if the configuration node has a 'vcpu' property and the hardware
 * node it represents is a Qman/Bman portal node.  If so, create the
 * corresponding cpu-handle property in the target guest node.
 *
 * Returns 0 if success or not a portal node, or negative error code
 */
static int configure_qman_portal(guest_t *guest, dt_node_t *cfgnode,
				 dt_node_t *hwnode, dt_node_t *gnode)
{
	if (!dt_node_is_compatible(hwnode, "fsl,qman-portal") &&
	    !dt_node_is_compatible(hwnode, "fsl,bman-portal"))
		return 0;

	dt_prop_t *prop = dt_get_prop_len(cfgnode, "vcpu", sizeof(uint32_t));
	if (!prop)
		return 0;

	// Get the VCPU number
	uint32_t vcpu = * ((uint32_t *) (prop->data));

	// Get the physical CPU number
	int pcpu = vcpu_to_cpu(guest->cpulist, guest->cpulist_len, vcpu);
	if (pcpu < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: 'vcpu' property in cfg node %s has invalid value %u\n",
			 __func__, cfgnode->name, vcpu);
		return ERR_BADTREE;
	}

	// Get the corresponding CPU node in the HW tree
	dt_node_t *cpunode = cpu_to_node(pcpu);
	if (!cpunode) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: cpu %u does not exist\n", __func__, pcpu);
		return ERR_BADTREE;
	}

	// Get the phandle from the CPU node
	uint32_t phandle = dt_get_phandle(cpunode, 0);
	if (!phandle) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: cpu node %s does not have a phandle\n",
			 __func__, cpunode->name);
		return ERR_BADTREE;
	}

	// Put the phandle in the guest node
	int ret = dt_set_prop(gnode, "cpu-handle", &phandle, sizeof(phandle));
	if (ret)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: could not set cpu-handle property in node %s\n",
			 __func__, gnode->name);

	return ret;
}


static int map_device_to_guest(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	dt_node_t *hwnode;
	dev_owner_t *owner;
	const char *failstr = NULL;
	int ret;

	hwnode = node->upstream;
	if (!hwnode)
		return 0;

	owner = dt_owned_by(hwnode, guest);
	if (!owner)
		return 0;

	assert(hwnode == owner->hwnode);

	create_aliases(hwnode, owner->gnode, guest->devtree);
	create_aliases(owner->cfgnode, owner->gnode, guest->devtree);

	ret = map_guest_reg(owner);
	if (ret < 0) {
		failstr = "map_guest_reg";
		goto fail;
	}

	ret = map_guest_ranges(owner);
	if (ret < 0) {
		failstr = "map_guest_ranges";
		goto fail;
	}

	dt_lookup_irqs(hwnode);

	ret = map_guest_irqs(owner);
	if (ret < 0) {
		failstr = "map_guest_irqs";
		goto fail;
	}

	ret = patch_guest_intmaps(owner);
	if (ret < 0) {
		failstr = "patch_guest_intmaps";
		goto fail;
	}

	configure_qman_portal(guest, owner->cfgnode, hwnode, node);
#ifdef CONFIG_PAMU
	configure_dma(node, owner);
#endif

	return 0;

fail:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: error %d in %s\n", failstr, ret, hwnode->name);

	if (dt_set_prop_string(owner->gnode, "status", "fail") < 0)
		return ERR_NOMEM;

	return 0;
}

static void reset_spintbl(guest_t *guest)
{
	struct boot_spin_table *spintbl = guest->spintbl;
	unsigned int i;

	for (i = 0; i < guest->cpucnt; i++) {
		spintbl[i].addr_hi = 0;
		spintbl[i].addr_lo = 1;
		spintbl[i].pir = i;
		spintbl[i].r3_hi = 0;
		spintbl[i].r3_lo = 0;
	}
}

static int copy_cpu_node(guest_t *guest, uint32_t vcpu,
                         const uint32_t *cpulist, int cpulist_len,
                         dt_node_t **nodep)
{
	dt_node_t *node, *gnode;
	int pcpu, ret;
	uint32_t cells;
	char buf[32];

	pcpu = vcpu_to_cpu(cpulist, cpulist_len, vcpu);
	if (pcpu < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: partition has no cpu %d\n", __func__, vcpu);
		return pcpu;
	}

	node = get_cpu_node(hw_devtree, pcpu);
	if (!node) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: vcpu %d maps to non-existent CPU %d\n",
		         __func__, vcpu, pcpu);
		return ERR_RANGE;
	}

	// The "cpu" nodes go under the /cpus node, which need the
	// #address-cells and #size-cells properties.
	//
	gnode = dt_get_subnode(guest->devtree, "cpus", 1);
	if (!gnode)
		return ERR_NOMEM;

	cells = 1;
	ret = dt_set_prop(gnode, "#address-cells", &cells, sizeof(cells));
	if (ret < 0)
		return ret;

	cells = 0;
	ret = dt_set_prop(gnode, "#size-cells", &cells, sizeof(cells));
	if (ret < 0)
		return ret;

	// Create the "cpu" node for this CPU.

	snprintf(buf, sizeof(buf), "cpu@%x", vcpu);
	gnode = dt_get_subnode(gnode, buf, 1);
	if (!gnode)
		return ERR_NOMEM;

	ret = dt_merge_tree(gnode, node, 0);
	if (ret < 0)
		return ret;

	ret = dt_set_prop(gnode, "reg", &vcpu, 4);
	if (ret < 0)
		return ret;

	*nodep = gnode;
	return 0;
}

static int create_guest_spin_table_cpu(guest_t *guest, int vcpu)
{
	uint64_t spin_addr;
	dt_node_t *node;
	int ret;

	ret = copy_cpu_node(guest, vcpu, guest->cpulist, guest->cpulist_len, &node);
	if (ret < 0)
		return ret;

	if (vcpu == 0)
		return dt_set_prop_string(node, "status", "okay");

	ret = dt_set_prop_string(node, "status", "disabled");
	if (ret < 0)
		return ret;

	ret = dt_set_prop_string(node, "enable-method", "spin-table");
	if (ret < 0)
		return ret;

	spin_addr = 0xfffff000 + vcpu * sizeof(struct boot_spin_table);
	return dt_set_prop(node, "cpu-release-addr", &spin_addr, 8);
}

static int create_guest_spin_table(guest_t *guest)
{
	unsigned long rpn;
	int ret;
	unsigned int i;

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

	for (i = 0; i < guest->cpucnt; i++) {
		ret = create_guest_spin_table_cpu(guest, i);

		if (ret < 0) {
			printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
			         "%s: error %d\n", __func__, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * create_recv_dbell_handle - create the receive handle for a special doorbell
 * @kind - the doorbell name
 * @guest - guest device tree
 * @offset - pointer to the partition node in the guest device tree
 * @dbell - pointer to special doorbell to use
 *
 * This function creates a doorbell receive handle node (under the specified
 * node) for a particular special doorbell.
 *
 * 'kind' is a string that is used to both name the special doorbell and to
 * create the compatible property for the receive handle node.
 *
 * This function must be called after recv_dbell_partition_init() is called,
 * because it creates receive doorbell handles that do not have an endpoint
 * property.  recv_dbell_partition_init() considers receive doorbell handle
 * configuration nodes without an endpoint to be an error.  An endpoint is
 * required in doorbell handle nodes only when the doorbell is defined in
 * the configuration DTS.  Special doorbells are created by the hypervisor
 * in create_guest_special_doorbells(), so they don't exist in any DTS.
 */
static int create_recv_dbell_handle(const char *kind, guest_t *guest,
				    dt_node_t *node, struct ipi_doorbell *dbell)
{
	char s[96];	// Should be big enough
	int ret, length;

	// Create the special doorbell receive handle node.
	node = dt_get_subnode(node, kind, 1);
	if (!node)
		return ERR_NOMEM;

	// 'node' is now a pointer to the new doorbell receive handle node

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
 * create_send_dbell_handle - create the send handles for a special doorbell
 * @kind - the doorbell name
 * @guest - guest device tree
 * @offset - pointer to the partition node in the guest device tree
 * @dbell - pointer to special doorbell to use
 *
 * This function creates a doorbell send handle node (under the 'handles'
 * node in each partition) for a particular special doorbell.  The node is
 * created in all partitions, even if there is no manager for this
 * partition.
 *
 * 'kind' is a string that is used to both name the special doorbell and to
 * create the compatible property for the send handle node.
 *
 * This function must be called after send_dbell_partition_init() is called,
 * because it creates send doorbell handles that do not have an endpoint
 * property.  send_dbell_partition_init() considers send doorbell handle
 * configuration nodes without and endpoint to be an error.  An endpoint is
 * required in doorbell handle nodes only when the doorbell is defined in
 * the configuration DTS.  Special doorbells are created by the hypervisor
 * in create_guest_special_doorbells(), so they don't exist in any DTS.
 */
static int create_send_dbell_handle(const char *kind, guest_t *guest,
				    dt_node_t *node, struct ipi_doorbell *dbell)
{
	char s[96];	// Should be big enough
	int ret, length;
	int32_t handle;

	// Create the special doorbell send handle node.
	node = dt_get_subnode(node, kind, 1);
	if (!node)
		return ERR_NOMEM;

	// 'node' is now a pointer to the new doorbell send handle node

	// Write the 'compatible' property to the doorbell send handle node
	// We can't embed a \0 in the format string, because that will confuse
	// snprintf (it will stop scanning when it sees the \0), so we use %c.
	length = snprintf(s, sizeof(s),
		"fsl,hv-%s-doorbell%cfsl,hv-doorbell-send-handle", kind, 0);

	ret = dt_set_prop(node, "compatible", s, length + 1);
	if (ret < 0)
		return ret;

	handle = doorbell_attach_guest(dbell, guest);
	if (handle < 0)
		return handle;

	return dt_set_prop(node, "reg", &handle, sizeof(handle));
}

/**
 * create_sdbell_receive_handles - create the receive handles for this guest
 *
 * This function creates the receive handles for the special doorbells.
 */
static int create_sdbell_receive_handles(guest_t *guest)
{
	dt_node_t *node;

	// Find the 'handles' node in the target partition
	node = get_handles_node(guest);
	if (!node) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "guest %s: cannot create 'handles' node\n", guest->name);
		return 0;
	}

	return create_recv_dbell_handle("shutdown", guest, node,
					guest->dbell_shutdown);
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

	guest->dbell_shutdown = alloc_type(ipi_doorbell_t);
	if (!guest->dbell_shutdown) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s: out of memory\n", __func__);
		goto error;
	}

	return 0;

error:
	free(guest->dbell_shutdown);
	free(guest->dbell_restart_request);
	free(guest->dbell_watchdog_expiration);
	free(guest->dbell_restart_request);

	guest->dbell_shutdown = NULL;
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
static int load_image(guest_t *guest, phys_addr_t image,
                      phys_addr_t guest_phys, size_t *length,
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

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "loading binary image from %#llx to %#llx\n",
	         image, guest_phys);

	if (copy_phys_to_gphys(guest->gphys, guest_phys, image, *length, 1) != *length)
		return ERR_BADADDR;

	if (entry)
		*entry = guest_phys;

	return 0;
}

static int load_image_table(guest_t *guest, const char *table,
                            int entry, int rootfs)
{
	int ret;
	dt_prop_t *prop;
	const uint32_t *data, *end;
	uint64_t image_addr;
	uint64_t guest_addr;
	size_t length;
	unsigned int row = 1;

	prop = dt_get_prop(guest->partition, table, 0);
	if (!prop)
		return 0;

	data = prop->data;
	end = (const uint32_t *)((uintptr_t)prop->data + prop->len);

	while (data + rootnaddr + rootnaddr + rootnsize <= end) {
		image_addr = int_from_tree(&data, rootnaddr);
		guest_addr = int_from_tree(&data, rootnaddr);
		length = int_from_tree(&data, rootnsize);

		if (length != (size_t)length) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "load_image: guest %s: invalid length %#zx\n",
			         guest->name, length);
			continue;
		}

		// We want everyone to put a length field in the device tree
		if (!length)
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
				 "load_image: guest %s: "
				 "image #%u should specify a non-zero length\n",
				 guest->name, row);

		ret = load_image(guest, image_addr, guest_addr,
		                 &length, entry ? &guest->entry : NULL);
		if (ret < 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "guest %s: could not load image\n", guest->name);
			return ret;
		}

		if (rootfs) {
			dt_node_t *chosen = dt_get_subnode(guest->devtree, "chosen", 1);
			if (!chosen)
				goto nomem;

			ret = dt_set_prop(chosen, "linux,initrd-start",
			                  &guest_addr, sizeof(guest_addr));
			if (ret < 0)
				goto nomem;

			guest_addr += length;

			ret = dt_set_prop(chosen, "linux,initrd-end",
			                  &guest_addr, sizeof(guest_addr));
			if (ret < 0)
				goto nomem;
		}

		if (entry || rootfs) {
			if (data != end)
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: ignoring junk at end of %s in %s\n",
				         __func__, table, guest->partition->name);

			return 1;
		}
		row++;
	}

	return 1;

nomem:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);

	return ERR_NOMEM;
}

static int load_images(guest_t *guest)
{
	load_image_table(guest, "load-image-table", 0, 0);
	load_image_table(guest, "linux-rootfs", 0, 1);
	return load_image_table(guest, "guest-image", 1, 0);
}

static const uint32_t hv_version[4] = {
	CONFIG_HV_MAJOR_VERSION,
	CONFIG_HV_MINOR_VERSION,
	FH_API_VERSION,
	FH_API_COMPAT_VERSION
};

/**
 * manager_partition_init_one - init the managed guests for one manager
 * @node: pointer to manager's "managed-partition" node
 * @arg: guest_t
 *
 * This function finds and initializes all of the managed guests for the
 * given manager partition.
 */
static int process_managed_partition_node(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	dt_prop_t *prop;
	uint32_t phandle;
	int ret;

	// The 'partition' property has a phandle to the managed node
	prop = dt_get_prop(node, "partition", 0);
	if (!prop || (prop->len != 4)) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s has missing/invalid 'partition' property\n",
			 __func__, guest->name, node->name);
		return 0;
	}
	phandle = *(const uint32_t *)prop->data;

	dt_node_t *endpoint = dt_lookup_phandle(config_tree, phandle);
	if (!endpoint) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s has invalid phandle in 'partition' property\n",
			 __func__, guest->name, node->name);
		return 0;
	}

	// Get the guest_t for the partition, or create one if necessary
	guest_t *target_guest = node_to_partition(endpoint);
	if (!target_guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s is not a partition\n",
			 __func__, guest->name, endpoint->name);
		return 0;
	}

	// Store the pointer to the target guest in our list of handles
	target_guest->handle.guest = target_guest;

	// Allocate a handle
	int32_t ghandle = alloc_guest_handle(guest, &target_guest->handle);
	if (ghandle < 0) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: too many handles\n",
			 __func__, guest->name);
		return ghandle;
	}

	// Get a pointer to the manager's 'handles' node
	node = get_handles_node(guest);
	if (!node)
		return ERR_NOMEM;

	// Find or create the pointer to the managed partition's subnode
	node = dt_get_subnode(node, target_guest->name, 1);
	if (!node)
		return ERR_NOMEM;

	// Insert the 'compatible' property.
	ret = dt_set_prop_string(node, "compatible", "fsl,hv-partition-handle");
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' property\n",
			 __func__, guest->name, node->name, "compatible");
		return ret;
	}

	// Insert a 'reg' property into the partition-handle node of the
	// guest device tree
	ret = dt_set_prop(node, "reg", &ghandle, sizeof(ghandle));
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' property\n",
			 __func__, guest->name, node->name, "reg");
		return ret;
	}

	ret = dt_set_prop_string(node, "label", target_guest->name);
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' property\n",
			 __func__, guest->name, node->name, "label");
		return ret;
	}

	// Create the receive handles for the special doorbells that this
	// manager receives *from* the managed partitions.

	ret = create_recv_dbell_handle("state-change", guest, node,
				   target_guest->dbell_state_change);
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' doorbell handle\n",
			 __func__, guest->name, node->name, "state-change");
		return ret;
	}

	ret = create_recv_dbell_handle("watchdog-expiration", guest, node,
				   target_guest->dbell_watchdog_expiration);
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' doorbell handle\n",
			 __func__, guest->name, node->name, "watchdog-expiration");
		return ret;
	}

	ret = create_recv_dbell_handle("reset-request", guest, node,
				   target_guest->dbell_restart_request);
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' doorbell handle\n",
			 __func__, guest->name, node->name, "reset-request");
		return ret;
	}

	// Create the send handles for the special doorbells that this manager
	// sends *to* to the managed partitions.

	ret = create_send_dbell_handle("shutdown", guest, node,
				       target_guest->dbell_shutdown);
	if (ret) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: guest %s: node %s: cannot create '%s' doorbell handle\n",
			 __func__, guest->name, node->name, "shutdown");
		return ret;
	}

	// Ideally, here we would create the receive handles in the target
	// (managed) guest for the special doorbells that that manager sends.
	// This would ensure that only managed guests get those receive handles.
	// Unfortunately, target_guest->devtree is NULL, so we can't do that.
	// Instead, we have to create those handles later in all guests during
	// a call to create_sdbell_receive_handles().

	return 0;
}

static int register_gcpu_with_guest(guest_t *guest)
{
	int gpir = get_gcpu_num(guest->cpulist, guest->cpulist_len,
	                        mfspr(SPR_PIR));
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
			setgevent(guest->gcpus[i], gev_restart);

	return ret;
}

/* Process configuration options in the partition node
 */
static int partition_config(guest_t *guest)
{
	dt_node_t *hv_node;
	int ret;

	/* guest cache lock mode */
	if (guest->guest_cache_lock) {
		hv_node = dt_get_subnode(guest->devtree, "hypervisor", 0);
		if (!hv_node)
			return -1;
		ret = dt_set_prop(hv_node, "fsl,hv-guest-cache-lock", NULL, 0);
		if (ret < 0)
			return -1;
	}

	/* guest debug mode */
	if (guest->guest_debug_mode) {
		hv_node = dt_get_subnode(guest->devtree, "hypervisor", 0);
		if (!hv_node)
			return -1;

		ret = dt_set_prop(hv_node, "fsl,hv-guest-debug", NULL, 0);
		if (ret < 0)
			return -1;
	}

	return 0;
}

static void get_cpulist(guest_t *guest)
{
	dt_node_t *node = guest->partition;
	dt_prop_t *prop;

	prop = dt_get_prop(node, "cpus", 0);
	if (prop) {
		if (prop->len == 0 || (prop->len % 8) != 0) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: bad cpus property in %s\n",
			         __func__, node->name);

			return;
		}

		guest->cpulist = malloc(prop->len);
		if (!guest->cpulist)
			goto nomem;

		memcpy(guest->cpulist, prop->data, prop->len);
		guest->cpulist_len = prop->len;
	}

	if (!guest->cpulist)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: No cpus property in guest %s\n",
		         __func__, node->name);

	return;

nomem:
	printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

static uint32_t start_guest_lock;

guest_t *node_to_partition(dt_node_t *partition)
{
	unsigned int i;
	int ret;
	char *name;

	// Verify that 'partition' points to a compatible node
	if (!dt_node_is_compatible(partition, "partition")) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: invalid partition node\n", __FUNCTION__);
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
			name = malloc(MAX_DT_PATH);
			if (!name) {
				spin_unlock(&start_guest_lock);
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "node_to_partition: out of memory\n");
				return NULL;
			}

			ret = dt_get_path(NULL, partition, name, MAX_DT_PATH);
			if (ret > MAX_DT_PATH)
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "node_to_partition: path name too long\n");
		}

		/* guest cache lock mode */
		if (dt_get_prop(partition, "guest-cache-lock", 0))
			guests[i].guest_cache_lock = 1;

		/* guest debug mode */
		if (dt_get_prop(partition, "guest-debug", 0))
			guests[i].guest_debug_mode = 1;

		if (dt_get_prop(partition, "mpic-direct-eoi", 0))
			guests[i].mpic_direct_eoi = 1;

		guests[i].name = name;
		guests[i].state = guest_starting;
		guests[i].partition = partition;
		guests[i].lpid = ++last_lpid;

		get_cpulist(&guests[i]);

#ifdef CONFIG_DEBUG_STUB
		init_stubops(&guests[i]);
#endif

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

/* FIXME: better ePAPR compliance for default IMA -- need to
 * look up the GPMA to find the size that is actually backed
 * by RAM.
 */
static int setup_ima(trapframe_t *regs, phys_addr_t entry, int secondary)
{
	unsigned long vpage = 0, ppage = 0, pages;
	size_t size;
	int tsize;
	int need_two = 0; // Two TLB entries needed for non-power-of-4 IMA
	guest_t *guest = get_gcpu()->guest;

	if (secondary) {
		ppage = entry >> PAGE_SHIFT;
		tsize = pages_to_tsize(1);
		size = PAGE_SIZE;
		pages = 1;
	} else {
		dt_prop_t *prop;

		prop = dt_get_prop(guest->partition, "init-map-size", 0);
		if (prop) {
			if (prop->len != 8 ||
			    (sizeof(void *) != 8 &&
			     ((const uint32_t *)prop->data)[0] != 0)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: Bad init-map-size\n", __func__);
				return ERR_BADTREE;
			}

			size = *(const uint64_t *)prop->data;

			if (size & (size - 1)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: init-map-size must be a power of two\n", __func__);
				return ERR_BADTREE;
			}

			if (size < PAGE_SIZE) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: init-map-size must at least %d bytes\n",
				         __func__, PAGE_SIZE);
				return ERR_BADTREE;
			}

			pages = size >> PAGE_SHIFT;

			tsize = pages_to_tsize(pages);
			if (count_lsb_zeroes(pages) & 1)
				need_two = 1;
		} else {
			tsize = TLB_TSIZE_1G;
			size = 1 << 30;
			pages = size >> PAGE_SHIFT;
		}

		prop = dt_get_prop(guest->partition, "init-map-paddr", 0);
		if (prop) {
			phys_addr_t paddr;

			if (prop->len != 8) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: Bad init-map-paddr\n", __func__);
				return ERR_BADTREE;
			}

			paddr = *(const uint64_t *)prop->data;
			ppage = paddr >> PAGE_SHIFT;

			if (paddr & (size - 1)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: init-map-paddr must be size-aligned\n", __func__);
				return ERR_BADTREE;
			}
		}

		prop = dt_get_prop(guest->partition, "init-map-vaddr", 0);
		if (prop) {
			uintptr_t vaddr;
		
			if (prop->len != 8 ||
			    (sizeof(void *) != 8 &&
			     ((const uint32_t *)prop->data)[0] != 0)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: Bad init-map-vaddr\n", __func__);
				return ERR_BADTREE;
			}

			vaddr = *(const uint64_t *)prop->data;
			vpage = vaddr >> PAGE_SHIFT;
			
			if (vaddr & (size - 1)) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
				         "%s: init-map-vaddr must be size-aligned\n", __func__);
				return ERR_BADTREE;
			}
		}
	}

	if ((entry >> PAGE_SHIFT) < ppage ||
	    (entry >> PAGE_SHIFT) >= ppage + pages) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: entry point 0x%llx outside IMA 0x%llx-0x%llx\n",
		         __func__, entry, (uint64_t)ppage << PAGE_SHIFT,
		         ((uint64_t)(ppage + pages) << PAGE_SHIFT) - 1);
		return ERR_BADTREE;
	}

	regs->gpregs[7] = size;
	regs->srr0 = (vpage << PAGE_SHIFT) | (entry & (size - 1));

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL + 2,
	         "%s: virtual IMA 0x%llx-0x%llx, entry 0x%lx\n",
	         __func__, (uint64_t)vpage << PAGE_SHIFT,
		      ((uint64_t)(vpage + pages) << PAGE_SHIFT) - 1, regs->srr0);

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL + 2,
	         "%s: guest physical IMA 0x%llx-0x%llx, entry 0x%llx\n",
	         __func__, (uint64_t)ppage << PAGE_SHIFT,
		      ((uint64_t)(ppage + pages) << PAGE_SHIFT) - 1, entry);

	for (int i = 0; i <= need_two; i++) {
		guest_set_tlb1(i, MAS1_VALID | (tsize << MAS1_TSIZE_SHIFT) |
		               MAS1_IPROT, vpage, ppage, TLB_MAS2_MEM,
		               TLB_MAS3_KERN);

		vpage += pages / 2;
		ppage += pages / 2;
	}

	return 0;
}

static void start_guest_primary_nowait(trapframe_t *regs, void *arg)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	int ret;
	unsigned int i;

	assert(guest->state == guest_starting);

	guest_core_init(guest);
	reset_spintbl(guest);

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

	size_t fdtsize = fdt_totalsize(fdt);
	assert(fdtsize <= guest->dtb_window_len);

	ret = copy_to_gphys(guest->gphys, guest->dtb_gphys,
	                    fdt, fdtsize, 0);
	free(fdt);
	if (ret != (ssize_t)fdtsize) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "%s: cannot copy device tree to guest\n", __func__);
		return;
	}

	if (setup_ima(regs, guest->entry, 0) < 0)
		return;

	printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
	         "branching to guest %s, %d cpus\n", guest->name, guest->cpucnt);

	assert(guest->active_cpus == 0);

	for (i = 1; i < guest->cpucnt; i++) {
		if (!guest->gcpus[i]) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_NORMAL,
			         "guest %s waiting for cpu %d...\n", guest->name, i);

			while (!guest->gcpus[i])
				barrier();
		}

		setgevent(guest->gcpus[i], gev_start);
	}

	atomic_add(&guest->active_cpus, 1);

	while (guest->active_cpus != guest->cpucnt)
		barrier();

	guest->state = guest_running;
	smp_mbar();
	send_doorbells(guest->dbell_state_change);

	assert(cpu->traplevel == 0);
	assert(cpu->thread == &gcpu->thread.libos_thread);

	// We only map 1GiB, so we don't support loading/booting an OS above
	// that address.  We pass the guest physical address even though
	// it should be a guest virtual address, but since we program the TLBs
	// such that guest virtual == guest physical at boot time, this works.

	regs->gpregs[1] = 0xdeadbeef;
	regs->gpregs[3] = guest->dtb_gphys;
	regs->gpregs[6] = 0x45504150; // ePAPR Magic for Book-E
	regs->srr1 = MSR_GS;

#ifdef CONFIG_DEBUG_STUB
	if (get_gcpu()->dbgstub_cfg && guest->stub_ops && guest->stub_ops->vcpu_start)
		guest->stub_ops->vcpu_start(regs);
#endif
}

static void start_guest_primary(trapframe_t *regs, void *arg)
{
	guest_t *guest = get_gcpu()->guest;
	int ret = 1;

	assert(guest->state == guest_starting);

	if (!guest->no_auto_load)
		ret = load_images(guest);

	if (ret <= 0 || dt_get_prop(guest->partition, "no-auto-start", 0)) {
		guest->state = guest_stopped;

		/* No hypervisor-loadable image; wait for a manager to start us. */
		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "Guest %s waiting for manager start\n", guest->name);

		// Notify the manager(s) that it needs to load images and
		// start this guest.
		send_doorbells(guest->dbell_restart_request);
		send_doorbells(guest->dbell_state_change);
		prepare_to_block();
		block();
		BUG();
	}

	start_guest_primary_nowait(regs, arg);
}

static void start_guest_secondary(trapframe_t *regs, void *arg)
{
	unsigned long page;
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned int pir = mfspr(SPR_PIR);
	unsigned int gpir = gcpu->gcpu_num;
	phys_addr_t entry;

	mtspr(SPR_GPIR, gpir);

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "cpu %d/%d spinning on table...\n", pir, gpir);

	atomic_add(&guest->active_cpus, 1);

	while (guest->spintbl[gpir].addr_lo & 1) {
		asm volatile("dcbf 0, %0" : : "r" (&guest->spintbl[gpir]) : "memory");
		asm volatile("dcbf 0, %0" : : "r" (&guest->spintbl[gpir + 1]) : "memory");
		smp_mbar();

		if (gcpu->gevent_pending & (1 << gev_stop)) {
			atomic_and(&gcpu->gevent_pending, ~(1 << gev_stop));
			stop_core(regs);
		}

		if (gcpu->gevent_pending & (1 << gev_restart)) {
			atomic_and(&gcpu->gevent_pending, ~(1 << gev_restart));
			restart_core(regs);
		}

		if (gcpu->gevent_pending & (1 << gev_pause)) {
			atomic_and(&gcpu->gevent_pending, ~(1 << gev_pause));
			pause_core(regs);
		}
	}

	guest_core_init(guest);

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "secondary %d/%d spun up, addr %lx\n",
	         pir, gpir, guest->spintbl[gpir].addr_lo);

	if (guest->spintbl[gpir].pir != gpir)
		printlog(LOGTYPE_MP, LOGLEVEL_ERROR,
		         "WARNING: cpu %d (guest cpu %d) changed spin-table "
		         "PIR to %ld, ignoring\n",
		         pir, gpir, guest->spintbl[gpir].pir);

	entry = ((uint64_t)guest->spintbl[gpir].addr_hi << 32) |
	        guest->spintbl[gpir].addr_lo;

	assert(cpu->traplevel == 0);
	assert(cpu->thread == &gcpu->thread.libos_thread);

	regs->gpregs[1] = 0xdeadbeef;
	regs->gpregs[3] = ((uint64_t)guest->spintbl[gpir].r3_hi << 32) |
	                  guest->spintbl[gpir].r3_lo;

	setup_ima(regs, entry, 1);

	regs->srr1 = MSR_GS;

#ifdef CONFIG_DEBUG_STUB
	if (get_gcpu()->dbgstub_cfg && guest->stub_ops && guest->stub_ops->vcpu_start)
		guest->stub_ops->vcpu_start(regs);
#endif
}

void start_core(trapframe_t *regs)
{
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG, "start core %lu\n", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	void (*fn)(trapframe_t *regs, void *arg);

	assert(guest->state == guest_starting);

	if (gcpu == guest->gcpus[0])
		fn = start_guest_primary_nowait;
	else
		fn = start_guest_secondary;

	new_thread_inplace(&gcpu->thread, gcpu->hvstack, fn, NULL, 0);
	unblock(&gcpu->thread);
}

void start_wait_core(trapframe_t *regs)
{
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "start wait core %lu\n", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	assert(gcpu == guest->gcpus[0]);
	assert(guest->state == guest_starting);

	new_thread_inplace(&gcpu->thread, gcpu->hvstack,
	                   start_guest_primary, NULL, 0);
	unblock(&gcpu->thread);
}

void do_stop_core(trapframe_t *regs, int restart)
{
	int i;
	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG,
	         "%s core %lu\n", restart ? "restart" : "stop", mfspr(SPR_PIR));

	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	assert(guest->state == guest_stopping);

#ifdef CONFIG_DEBUG_STUB
	if (get_gcpu()->dbgstub_cfg && guest->stub_ops && guest->stub_ops->vcpu_stop) {
		guest->stub_ops->vcpu_stop();
	}
#endif

	disable_int();
	guest_reset_tlb();
	enable_int();

	if (atomic_add(&guest->active_cpus, -1) == 0) {
		for (i = 0; i < MAX_HANDLES; i++) {
			handle_t *h = guest->handles[i];

			if (h && h->ops && h->ops->reset)
				h->ops->reset(h);
		}

		if (restart) {
			guest->state = guest_starting;
			setgevent(guest->gcpus[0], gev_start_wait);
		} else {
			guest->state = guest_stopped;
			send_doorbells(guest->dbell_state_change);
		}
	}

	mpic_reset_core();

	memset(&gcpu->gdbell_pending, 0,
	       sizeof(gcpu_t) - offsetof(gcpu_t, gdbell_pending));

	if (!is_idle()) {
		prepare_to_block();
		block();
		BUG();
	}
}

void stop_core(trapframe_t *regs)
{
	do_stop_core(regs, 0);
}

void restart_core(trapframe_t *regs)
{
	do_stop_core(regs, 1);
}

void pause_core(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG, "pause core\n");
	assert(guest->state == guest_pausing);

	if (atomic_add(&guest->active_cpus, -1) == 0) {
		guest->active_cpus = guest->cpucnt;
		smp_lwsync();
		guest->state = guest_paused;
		send_doorbells(guest->dbell_state_change);
	}

	prepare_to_block();
	block();
}

void resume_core(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;

	printlog(LOGTYPE_MP, LOGLEVEL_DEBUG, "resume core\n");
	assert(guest->state == guest_resuming);

	if (atomic_add(&guest->active_cpus, 1) == guest->cpucnt) {
		guest->state = guest_running;
		send_doorbells(guest->dbell_state_change);
	}

	unblock(&get_gcpu()->thread);
}

int stop_guest(guest_t *guest)
{
	unsigned int i, ret = 0;
	spin_lock(&guest->state_lock);

	if (guest->state != guest_running && guest->state != guest_paused)
		ret = ERR_INVALID;
	else
		guest->state = guest_stopping;

	spin_unlock(&guest->state_lock);

	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], gev_stop);

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

	setgevent(guest->gcpus[0], gev_start);
	return ret;
}

int pause_guest(guest_t *guest)
{
	unsigned int i, ret = 0;
	spin_lock(&guest->state_lock);

	if (guest->state != guest_running)
		ret = ERR_INVALID;
	else
		guest->state = guest_pausing;

	spin_unlock(&guest->state_lock);

	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], gev_pause);

	return ret;
}

int resume_guest(guest_t *guest)
{
	int ret = 0;
	unsigned int i;
	spin_lock(&guest->state_lock);

	if (guest->state != guest_paused) {
		ret = ERR_INVALID;
	} else {
		guest->active_cpus = 0;
		smp_lwsync();
		guest->state = guest_resuming;
	}

	spin_unlock(&guest->state_lock);

	if (ret)
		return ret;

	for (i = 0; i < guest->cpucnt; i++)
		setgevent(guest->gcpus[i], gev_resume);

	return ret;
}

static void read_phandle_aliases(guest_t *guest)
{
	dt_node_t *aliases;

	aliases = dt_get_subnode(guest->partition, "aliases", 0);
	if (!aliases)
		return;

	list_for_each(&aliases->props, i) {
		dt_prop_t *prop = to_container(i, dt_prop_t, prop_node);
		dt_node_t *node;
		alias_t *alias;

		/* It could be a string alias rather than a phandle alias.
		 * Note that there is a possibility of ambiguity, if a
		 * 3-letter string alias gets interpreted as a valid phandle;
		 * however, the odds of this actually happening are very low.
		 */
		if (prop->len != 4)
			continue;

		node = dt_lookup_phandle(config_tree, *(const uint32_t *)prop->data);
		if (!node)
			continue;

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

dt_node_t *get_handles_node(guest_t *guest)
{
	dt_node_t *hv, *handles;
	uint32_t propdata;

	hv = dt_get_subnode(guest->devtree, "hypervisor", 1);
	if (!hv)
		return NULL;

	handles = dt_get_subnode(hv, "handles", 0);
	if (handles)
		return handles;

	handles = dt_get_subnode(hv, "handles", 1);
	if (!handles)
		return NULL;

	propdata = 1;
	if (dt_set_prop(handles, "#address-cells", &propdata, 4) < 0)
		return NULL;

	propdata = 0;
	if (dt_set_prop(handles, "#size-cells", &propdata, 4) < 0)
		return NULL;

	propdata = guest->vmpic_phandle;
	if (propdata &&
	    dt_set_prop(handles, "interrupt-parent", &propdata, 4) < 0)
		return NULL;

	return handles;
}

static dev_owner_t *get_direct_owner(dt_node_t *node, guest_t *guest)
{
	dev_owner_t *owner;

	while (node) {
		owner = dt_owned_by(node, guest);
		if (owner && owner->direct == owner)
			return owner;

		node = node->parent;
	}

	return NULL;
}

static int assign_child(dt_node_t *node, void *arg)
{
	guest_t *guest = arg;
	dev_owner_t *direct = get_direct_owner(node->upstream, guest);
	dev_owner_t *owner;

	/* No direct owner means it was created with node-update.  Skip it. */
	if (!direct)
		return 0;

	owner = dt_owned_by(node->upstream, guest);
	if (!owner) {
		owner = malloc(sizeof(dev_owner_t));
		if (!owner)
			return ERR_NOMEM;

		spin_lock(&dt_owner_lock);

		list_add(&node->upstream->owners, &owner->dev_node);
		list_add(&guest->dev_list, &owner->guest_node);

		owner->guest = guest;
		owner->hwnode = node->upstream;

		spin_unlock(&dt_owner_lock);
	}

	owner->cfgnode = direct->cfgnode;
	owner->direct = direct;
	owner->gnode = node;

	return 0;
}

static int merge_guest_dev(dt_node_t *hwnode, void *arg)
{
	guest_t *guest = arg;
	dev_owner_t *owner;
	dt_node_t *parent = guest->devices;
	const char *name;
	int ret;

	owner = dt_owned_by(hwnode, guest);
	if (!owner || owner->direct != owner)
		return 0;

	assert(owner->hwnode == hwnode);

	/* If a parent of this node was owned, then replace the gnode. */
	if (owner->gnode) {
		parent = owner->gnode->parent;
		dt_delete_node(owner->gnode);
		name = hwnode->name;
	} else {
		name = owner->cfgnode->name;

		if (dt_node_is_compatible(hwnode->parent, "fsl,dpaa")) {
			// DPAA nodes belong under the fsl,dpaa container node
			parent = dt_get_subnode(guest->devtree, hwnode->parent->name, 1);
			if (!parent) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
					 "%s: could not create %s container node\n",
					 __func__, hwnode->parent->name);
				return ERR_NOMEM;
			}
			ret = dt_copy_properties(hwnode->parent, parent);
			if (ret) {
				printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
					 "%s: could not copy properties of %s node\n",
					 __func__, hwnode->parent->name);
				return ret;
			}
		} else if (hwnode->parent->parent &&
			   !dt_node_is_compatible(hwnode->parent, "simple-bus")) {
			printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			         "%s: don't know how to assign device %s on bus %s\n",
			         __func__, hwnode->name, hwnode->parent->name);
			return 0;
		}
	}

	owner->gnode = dt_get_subnode(parent, name, 1);
	if (!owner->gnode)
		goto out;

	ret = dt_merge_tree(owner->gnode, hwnode, 0);
	if (ret < 0)
		goto out_gnode;

	dt_record_guest_phandle(owner->gnode, owner->cfgnode);

	if (dt_process_node_update(guest, owner->gnode, owner->cfgnode))
		goto out_gnode;

	ret = dt_for_each_node(owner->gnode, guest, assign_child, NULL);
	if (ret < 0)
		return ret;

	return 0;

out_gnode:
	dt_delete_node(owner->gnode);
	owner->gnode = NULL;

out:
	return ERR_NOMEM;
}

static void init_dev_ranges(guest_t *guest)
{
	/* Create a toplevel container node under which all devices will go. */
	dt_node_t *container = dt_get_subnode(guest->devtree, "devices", 1);
	if (!container)
		goto nomem;

	if (dt_set_prop_string(container, "compatible", "simple-bus") < 0)
		goto nomem;

	/* Create a "ranges" property equivalent to device-ranges */
	if (dt_set_prop(container, "ranges", 0, 0) < 0)
		goto nomem;

	uint32_t val = 2;
	if (dt_set_prop(container, "#address-cells", &val, 4) < 0)
		goto nomem;

	if (dt_set_prop(container, "#size-cells", &val, 4) < 0)
		goto nomem;

	guest->devices = container;

	guest->devranges = dt_get_prop(guest->partition, "device-ranges", 0);
	if (!guest->devranges)
		return;

	if (guest->devranges->len == 0) {
		printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
		         "%s: %s: device-ranges cannot be empty "
		         "(omit the property instead)\n",
		         __func__, guest->partition->name);

		guest->devranges = NULL;
		return;
	}

	/* Create a "ranges" property equivalent to device-ranges */
	if (dt_set_prop(container, "ranges", guest->devranges->data,
	                guest->devranges->len) < 0)
		goto nomem;


	return;

nomem:
	guest->devranges = NULL;
	printlog(LOGTYPE_DEVTREE, LOGLEVEL_ERROR,
	         "%s: out of memory\n", __func__);
}

static int init_guest_devs(guest_t *guest)
{
	int ret;

	init_dev_ranges(guest);

	vmpic_partition_init(guest);

	dt_assign_devices(guest->partition, guest);

	/* First, merge each assigned device and its sub-tree, and
	 * apply node-update.  It is done with dt_for_each_node rather
	 * than traversing the device list, so that parents are processed
	 * before children.  Each sub-device is added to the device list,
	 * with its cfgnode pointing to its nearest directly assigned
	 * ancestor.
	 */

	ret = dt_for_each_node(hw_devtree, guest, merge_guest_dev, NULL);
	if (ret < 0)
		return ret;

	/* Second, traverse the assigned nodes (including children)
	 * and assign resources and fix up device tree nodes.
	 */

	return dt_for_each_node(guest->devtree, guest,
	                        map_device_to_guest, NULL);
}

/* Don't inline this inside init_guest, to isolate its stack usage. */
static int __attribute__((noinline)) init_guest_primary(guest_t *guest)
{
	int ret;
	dt_prop_t *prop;
	dt_node_t *node;
	const uint32_t *propdata;
	int gpir;
	char buf[64];

	/* count number of cpus for this partition and alloc data struct */
	guest->cpucnt = count_cpus(guest->cpulist, guest->cpulist_len);
	guest->gcpus = alloc(sizeof(long) * guest->cpucnt, sizeof(long));
	if (!guest->gcpus)
		goto nomem;

	gpir = register_gcpu_with_guest(guest);
	assert(gpir == 0);

	prop = dt_get_prop(guest->partition, "dtb-window", 0);
	if (!prop) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: Guest missing property dtb-window\n");
		return ERR_BADTREE;
	}

	if (prop->len != (rootnaddr + rootnsize) * 4) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest_primary: Invalid property len for dtb-window\n");
		return ERR_BADTREE;
	}

	guest->devtree = create_dev_tree();
	ret = dt_set_prop_string(guest->devtree, "label", guest->name);
	if (ret < 0)
		goto fail;

	ret = dt_set_prop(guest->devtree, "#address-cells", &rootnaddr, 4);
	if (ret < 0)
		goto fail;

	ret = dt_set_prop(guest->devtree, "#size-cells", &rootnsize, 4);
	if (ret < 0)
		goto fail;

	/* FIXME: hardcoded p4080 */
	ret = snprintf(buf, sizeof(buf), "fsl,hv-platform-p4080%csimple-bus", 0);
	assert(ret < (int)sizeof(buf));

	ret = dt_set_prop(guest->devtree, "compatible", buf, ret + 1);
	if (ret < 0)
		goto fail;

	// Set the ePAPR version string.
	ret = snprintf(buf, sizeof(buf), "ePAPR-%u.%u", 1, 0);
	assert(ret < (int)sizeof(buf));

	ret = dt_set_prop(guest->devtree, "epapr-version", buf, ret + 1);
	if (ret < 0)
		goto fail;

	node = dt_get_subnode(guest->devtree, "hypervisor", 1);
	if (!node)
		goto nomem;

	if (!mpic_coreint) {
		ret = dt_set_prop(node, "fsl,hv-pic-legacy", NULL, 0);
		if (ret < 0)
			goto fail;
	}

	// FIXME: not in spec
	ret = dt_set_prop(node, "fsl,hv-version", hv_version, sizeof(hv_version));
	if (ret < 0)
		goto fail;

	propdata = prop->data;
	guest->dtb_gphys = int_from_tree(&propdata, rootnaddr);
	guest->dtb_window_len = int_from_tree(&propdata, rootnsize);

	guest->gphys = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	guest->gphys_rev = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	if (!guest->gphys || !guest->gphys_rev)
		goto nomem;

	ret = create_guest_spin_table(guest);
	if (ret < 0)
		goto fail;

	map_guest_mem(guest);

	list_init(&guest->dev_list);
	list_init(&guest->phandle_update_list);

 	read_phandle_aliases(guest);

	ret = init_guest_devs(guest);
	if (ret < 0)
		goto fail;

	send_dbell_partition_init(guest);
	recv_dbell_partition_init(guest);

	ret = dt_for_each_compatible(guest->partition, "managed-partition",
				     process_managed_partition_node, guest);
	if (ret < 0)
		goto fail;

	ret = partition_config(guest);
	if (ret < 0)
		goto fail;

	// Create the receive doorbell handles for this guest.
	ret = create_sdbell_receive_handles(guest);
	if (ret < 0)
		goto fail;

#ifdef CONFIG_BYTE_CHAN
	byte_chan_partition_init(guest);
#endif

#ifdef CONFIG_DEVICE_VIRT
	list_init(&guest->vf_list);
#endif

#ifdef CONFIG_DEBUG_STUB
	/* set a pointer to the stub's config node */
	if (guest->stub_ops && guest->stub_ops->compatible)
		get_gcpu()->dbgstub_cfg = find_stub_config_node(guest->stub_ops->compatible);

	if (get_gcpu()->dbgstub_cfg && guest->stub_ops && guest->stub_ops->vcpu_init) {
		guest->stub_ops->vcpu_init();
	}
#endif

	// Get the watchdog timeout options
	prop = dt_get_prop(guest->partition, "wd-mgr-notify", 0);
	guest->wd_notify = !!prop;

	ret = dt_process_node_update(guest, guest->devtree, guest->partition);
	if (ret < 0)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			 "%s: error %d merging partition node-update\n",
			 __func__, ret);

	dt_run_deferred_phandle_updates(guest);

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
} init_guest_ctx_t;

static int init_guest_one(dt_node_t *node, void *arg)
{
	init_guest_ctx_t *ctx = arg;
	int pir = mfspr(SPR_PIR);
	guest_t *guest;

	guest = node_to_partition(node);
	if (!guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest: node_to_partition failed\n");
		return ERR_NOMEM;
	}

	if (!cpu_in_cpulist(guest->cpulist, guest->cpulist_len, pir))
		return 0;

	if (ctx->guest) {
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
		         "init_guest: extra guest %s on core %d\n", guest->name, pir);
		return 0;
	}

	ctx->guest = guest;
	return 0;
}

/* init_guest() is called once per CPU
 */
__attribute__((noreturn)) void init_guest(void)
{
	unsigned int pir = mfspr(SPR_PIR);
	init_guest_ctx_t ctx = {};
	gcpu_t *gcpu = get_gcpu();

	dt_for_each_compatible(config_tree, "partition",
	                       init_guest_one, &ctx);

	if (ctx.guest) {
		guest_t *guest = ctx.guest;

		gcpu->guest = guest;

		mtspr(SPR_LPIDR, guest->lpid);

		printlog(LOGTYPE_PARTITION, LOGLEVEL_DEBUG,
		         "guest at %s on core %d\n", guest->name, pir);

		if (pir == guest->cpulist[0]) {
			/* Boot CPU */
			if (init_guest_primary(guest) == 0) {
				guest->state = guest_starting;
				setgevent(gcpu, gev_start_wait);
			}
		} else {
			register_gcpu_with_guest(guest);

#ifdef CONFIG_DEBUG_STUB
			/* set a pointer to the stub's config node */
			if (guest->stub_ops && guest->stub_ops->compatible)
				gcpu->dbgstub_cfg =
					find_stub_config_node(guest->stub_ops->compatible);

			if (get_gcpu()->dbgstub_cfg && guest->stub_ops
			    && guest->stub_ops->vcpu_init) {
				guest->stub_ops->vcpu_init();
			}
#endif
		}
	}

	gcpu->waiting_for_gevent = 1;

	if (cpu->ret_hook)
		send_doorbell(cpu->coreid);

	enable_int();
	idle_loop();

	BUG();
}
