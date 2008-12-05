
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#ifndef HV_DEVTREE_H
#define HV_DEVTREE_H

#include <libos/libos.h>
#include <libos/list.h>
#include <libfdt.h>

#define MAX_DT_PATH 256

#define MAX_ADDR_CELLS 4
#define MAX_SIZE_CELLS 2
#define MAX_INT_CELLS 4

#define CELL_SIZE 4

struct guest;

extern list_t hv_devs;

typedef struct alias {
	list_t list_node;
	const char *name;
} alias_t;

typedef struct dev_owner {
	list_t dev_node, guest_node;
	struct guest *guest; /* if NULL, owned by hypervisor */
	struct dt_node *hwnode, *cfgnode;
} dev_owner_t;

typedef struct dt_node {
	struct dt_node *parent;
	list_t children, child_node, props;
	char *name;

	device_t dev; /** libos device */
	list_t owners;
	struct guest *irq_owner;
	struct pma *pma;

	struct dt_node *endpoint;
	struct byte_chan *bc;
	struct byte_chan_handle *bch;
	struct mux_complex *bcmux;

	list_t aliases;
} dt_node_t;

typedef struct dt_prop {
	list_t prop_node;
	char *name;
	void *data;
	size_t len;
} dt_prop_t;

dt_node_t *unflatten_dev_tree(const void *fdt);
int flatten_dev_tree(dt_node_t *tree, void *fdt_window, size_t fdt_len);

dt_node_t *create_dev_tree(void);
void dt_delete_node(dt_node_t *tree);

typedef int (*dt_callback_t)(dt_node_t *node, void *arg);

int dt_for_each_node(dt_node_t *tree, void *arg,
                     dt_callback_t previsit, dt_callback_t postvisit);

dt_node_t *dt_get_subnode_namelen(dt_node_t *node, const char *name,
                                  size_t namelen, int create);
dt_node_t *dt_get_subnode(dt_node_t *node, const char *name, int create);
dt_prop_t *dt_get_prop(dt_node_t *node, const char *name, int create);
char *dt_get_prop_string(dt_node_t *node, const char *name);

int dt_set_prop(dt_node_t *node, const char *name, const void *data, size_t len);
int dt_set_prop_string(dt_node_t *node, const char *name, const char *str);

int dt_merge_tree(dt_node_t *dest, dt_node_t *src,
                  int (*cb)(dt_node_t *dest, dt_node_t *src), int deletion);
void dt_print_tree(dt_node_t *tree, struct queue *out);

int dt_node_is_compatible(dt_node_t *node, const char *compat);
int dt_for_each_compatible(dt_node_t *tree, const char *compat,
                           dt_callback_t callback, void *arg);

dt_node_t *dt_get_first_compatible(dt_node_t *tree, const char *compat);

int dt_for_each_prop_value(dt_node_t *tree, const char *propname,
                           const void *value, size_t len,
                           dt_callback_t callback, void *arg);

size_t dt_get_path(dt_node_t *tree, dt_node_t *node, char *buf, size_t buflen);

dt_node_t *dt_lookup_alias(dt_node_t *tree, const char *name);
dt_node_t *dt_lookup_path(dt_node_t *tree, const char *path, int create);
dt_node_t *dt_lookup_phandle(dt_node_t *tree, uint32_t phandle);

uint32_t dt_get_phandle(dt_node_t *node);

int dt_owned_by(dt_node_t *node, struct guest *guest);
void dt_read_aliases(void);

/** Hardware device tree passed by firmware */
extern dt_node_t *hw_devtree;

/** Hypervisor config tree passed in bootargs */
extern dt_node_t *config_tree;

/** #address-cells and #size-cells at root of hv tree */
extern uint32_t rootnaddr, rootnsize;

int fdt_next_descendant_by_compatible(const void *fdt, int offset,
                                      int *depth, const char *compatible);

int fdt_get_addr_format(const void *fdt, int offset,
                        uint32_t *naddr, uint32_t *nsize);
int get_addr_format(dt_node_t *node, uint32_t *naddr, uint32_t *nsize);
int get_addr_format_nozero(dt_node_t *node, uint32_t *naddr, uint32_t *nsize);

void *ptr_from_node(dt_node_t *node, const char *type);

dt_node_t *get_interrupt_domain(dt_node_t *tree, dt_node_t *node);
dt_node_t *get_interrupt(dt_node_t *tree, dt_node_t *node, int intnum,
                         const uint32_t **intspec, int *ncellsp);
int get_num_interrupts(dt_node_t *tree, dt_node_t *node);
int dt_get_int_format(dt_node_t *domain, uint32_t *nint, uint32_t *naddr);

void dt_assign_devices(dt_node_t *tree, struct guest *guest);
void dt_lookup_regs(dt_node_t *node);
void dt_lookup_irqs(dt_node_t *node);

int dt_bind_driver(dt_node_t *node);

phys_addr_t find_memory(void *fdt);

int xlate_one(uint32_t *addr, const uint32_t *ranges,
              int rangelen, uint32_t naddr, uint32_t nsize,
              uint32_t prev_naddr, uint32_t prev_nsize,
              phys_addr_t *rangesize);

int xlate_reg_raw(dt_node_t *node, const uint32_t *reg,
                  uint32_t *addrbuf, phys_addr_t *size,
                  uint32_t naddr, uint32_t nsize);

int xlate_reg(dt_node_t *node, const uint32_t *reg,
              phys_addr_t *addr, phys_addr_t *size);

int dt_get_reg(dt_node_t *node, int res,
               phys_addr_t *addr, phys_addr_t *size);

void copy_val(uint32_t *dest, const uint32_t *src, int naddr);

static inline void val_from_int(uint32_t *dest, phys_addr_t src)
{
	dest[0] = 0;
	dest[1] = 0;
	dest[2] = src >> 32;
	dest[3] = (uint32_t)src;
}

static inline uint64_t int_from_tree(const uint32_t **src, int ncells)
{
	uint64_t ret = 0;

	if (ncells == 2) {
		ret = *(*src)++;
		ret <<= 32;
	}

	return ret | *(*src)++;
}

void create_ns16550(void);
void open_stdout(void);
void open_stdin(void);

extern struct queue *stdin, *stdout;

dt_node_t *get_cpu_node(dt_node_t *tree, int cpu);
dt_node_t *get_handles_node(struct guest *guest);
void create_aliases(dt_node_t *node, dt_node_t *gnode, dt_node_t *tree);

int open_stdout_chardev(dt_node_t *node);
int open_stdout_bytechan(dt_node_t *node);

typedef struct pma {
	phys_addr_t start, size;
} pma_t;

pma_t *get_pma(dt_node_t *node);

#endif
