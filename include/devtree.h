/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <handle.h>

#include <cpc.h>

#define MAX_DT_PATH 256

#define MAX_ADDR_CELLS 4
#define MAX_SIZE_CELLS 2
#define MAX_INT_CELLS 4

#define CELL_SIZE 4

struct guest;
struct queue;
#ifdef CONFIG_DEVICE_VIRT
struct vf_range;
#endif

extern list_t hv_devs;
extern uint32_t dt_owner_lock;

typedef struct alias {
	list_t list_node;
	const char *name;
} alias_t;

typedef enum claimable {
	not_claimable = 0,
	claimable_standby,
	claimable_active,
} claimable_t;

struct dev_owner;

struct ipi_doorbell;

typedef struct claim_action {
	/** Perform a subsystem-specific action to claim this device.
	 * 
	 * @param[in] action context pointer
	 * @param[in] owner dev_owner corresponding to the new owner
	 * @param[in] prev dev_owner corresponding to the existing owner
	 *            This partition will be in the stopped state.
	 * @return An error in the hcall error space.  If nonzero, this
	 *         will abort further action on this claim hcall.
	 */
	int (*claim)(struct claim_action *action, struct dev_owner *owner,
	             struct dev_owner *prev);
	struct claim_action *next;
} claim_action_t;

typedef struct dev_owner {
	list_t dev_node, guest_node;
	struct guest *guest; /* if NULL, owned by hypervisor */
	struct dt_node *hwnode, *cfgnode, *gnode;
	struct dev_owner *direct; /**< nearest ancestor which is directly assigned */
	handle_t handle;
#ifdef CONFIG_PAMU
	uint32_t *liodn_handles;
	int num_liodns;
#endif
#ifdef CONFIG_CLAIMABLE_DEVICES
	claimable_t claimable;
	claim_action_t *claim_actions;
#endif
} dev_owner_t;

#ifdef CONFIG_CLAIMABLE_DEVICES
static inline claimable_t get_claimable(dev_owner_t *owner)
{
	return owner->claimable;
}

void check_compatible_owners(dev_owner_t *new, dev_owner_t *old);
#else
static inline claimable_t get_claimable(dev_owner_t *owner)
{
	return not_claimable;
}

static inline void check_compatible_owners(dev_owner_t *new, dev_owner_t *old)
{
}
#endif

typedef struct intmap_entry {
	struct interrupt *irq; /* If the interrupt goes through vmpic, else NULL */
	uint32_t parent; /* Interrupt-parent phandle */
	uint32_t intspec[MAX_ADDR_CELLS + MAX_INT_CELLS];
	uint32_t parent_intspec[MAX_ADDR_CELLS + MAX_INT_CELLS];

	uint32_t parent_naddr, parent_nint;
	int valid;
} intmap_entry_t;

typedef struct update_phandle {
	list_t node;
	struct dt_node *dest; /**< Guest node to merge into */
	struct dt_node *src; /**< node-update-phandle node */

	/** Tree containing phandles that src refers to.  This is usually 
	 * the config tree, but in the future could be an hcall-supplied tree.
	 */
	struct dt_node *tree; 
} update_phandle_t;

typedef struct dt_node {
	struct dt_node *parent;
	list_t children, child_node, props;
	char *name;

	struct csd_info *csd;

	struct cpc_part_reg *cpc_reg[max_num_mem_tgts];

#ifdef CONFIG_DEVICE_VIRT
	struct vf_range *vf;
#endif

	device_t dev; /** libos device */
	list_t owners;
	struct guest *irq_owner;
	struct pma *pma;

	struct dt_node *endpoint;
	struct dt_node *upstream; /** Node that this node was merged from */
	struct byte_chan *bc;
	struct byte_chan_handle *bch;
	struct mux_complex *bcmux;
	struct ipi_doorbell *dbell;

	list_t aliases;

#ifdef CONFIG_CLAIMABLE_DEVICES
	/** Current active owner of a claimable device */
	dev_owner_t *claimable_owner;
	struct dt_node *dma_window; /* ptr to dma config node for reconfig */
#endif

	intmap_entry_t *intmap;
	uint32_t intmap_mask[MAX_ADDR_CELLS + MAX_INT_CELLS];
	int irqs_looked_up, intmap_len;

	/* This is the phandle that will be used in the guest tree.
	 * It is not the same as the phandle that exists as a property
	 * in the config tree.  When this node is merged into another
	 * tree, that node will have a phandle created if does not already
	 * exist.  The phandle of the destination node, whether pre-existing
	 * or newly created, will be placed in this field.
	 *
	 * Device config nodes, while not actually merged into the guest tree,
	 * will also have this field updated to match the phandle of the node
	 * in the guest tree.
	 */
	uint32_t guest_phandle;
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
void dt_delete_prop(dt_prop_t *prop);

typedef int (*dt_callback_t)(dt_node_t *node, void *arg);

int dt_for_each_node(dt_node_t *tree, void *arg,
                     dt_callback_t previsit, dt_callback_t postvisit);

dt_node_t *dt_get_subnode_namelen(dt_node_t *node, const char *name,
                                  size_t namelen, int create);
dt_node_t *dt_get_subnode(dt_node_t *node, const char *name, int create);
dt_prop_t *dt_get_prop(dt_node_t *node, const char *name, int create);
dt_prop_t *dt_get_prop_len(dt_node_t *node, const char *name, size_t length);
char *dt_get_prop_string(dt_node_t *node, const char *name);

int dt_set_prop(dt_node_t *node, const char *name, const void *data, size_t len);
int dt_set_prop_string(dt_node_t *node, const char *name, const char *str);

typedef int __bitwise dt_merge_flags_t;

enum {
	/** Enable the special properties of node-update, such as
	 * delete-prop and prepend-strlist (see external documentation
	 * for a complete list).
	 */
	dt_merge_special = (__force dt_merge_flags_t)1,

	/** Create a phandle in the destination if it does not already
	 * have one.  Record that phandle in the guest_phandle field
	 * of the source.  Do not allow any phandle property of the
	 * source to be copied to the destination.
	 *
	 * Set this flag when merging from config tree into the guest.
	 * Do not set it when merging from the hardware tree.
	 *
	 * To avoid conflicting phandles, any merges from the config tree
	 * must happen after any potential merge from the hardware tree
	 * into the same guest node.
	 */
	dt_merge_new_phandle = (__force dt_merge_flags_t)2,

	/** Internal use only -- to differentiate recursive invocations. */
	dt_merge_notfirst = (__force dt_merge_flags_t)4,
};

struct guest;

int dt_merge_tree(dt_node_t *dest, dt_node_t *src, dt_merge_flags_t flags);
int dt_process_node_update(struct guest *guest, dt_node_t *target,
                           dt_node_t *config);
void dt_run_deferred_phandle_updates(struct guest *guest);
void dt_print_tree(dt_node_t *tree, struct queue *out);

int dt_node_is_compatible(dt_node_t *node, const char *compat);
int dt_node_is_compatible_list(dt_node_t *node, const char **compats);
int dt_for_each_compatible(dt_node_t *tree, const char *compat,
                           dt_callback_t callback, void *arg);

int dt_for_each_compatible_list(dt_node_t *tree, const char **compat,
                                dt_callback_t callback, void *arg);

dt_node_t *dt_get_first_compatible(dt_node_t *tree, const char *compat);
dt_node_t *dt_get_first_compatible_list(dt_node_t *tree, const char **compat);

int dt_for_each_prop_value(dt_node_t *tree, const char *propname,
                           const void *value, size_t len,
                           dt_callback_t callback, void *arg);
dt_node_t *dt_get_first_prop_value(dt_node_t *tree, const char *propname,
                                   const void *value, int len);

size_t dt_get_path(dt_node_t *tree, dt_node_t *node, char *buf, size_t buflen);
int dt_copy_properties(dt_node_t *source, dt_node_t *target);

dt_node_t *dt_lookup_alias(dt_node_t *tree, const char *name);
dt_node_t *dt_lookup_path(dt_node_t *tree, const char *path, int create);
dt_node_t *dt_lookup_phandle(dt_node_t *tree, uint32_t phandle);

uint32_t dt_get_phandle(dt_node_t *node, int create);
int dt_record_guest_phandle(dt_node_t *gnode, dt_node_t *cfgnode);

dev_owner_t *dt_owned_by(dt_node_t *node, struct guest *guest);
void dt_read_aliases(void);

/** Hardware device tree passed by firmware */
extern dt_node_t *hw_devtree;

/** Spinlock for hardware device tree.  Grab this before modifying the
 *  hardware tree (e.g. adding properties).
 */
extern uint32_t hw_devtree_lock;

/** Hypervisor config tree passed in bootargs */
extern dt_node_t *config_tree;

/** Virtual tree used for hardware-like virtual devices */
extern dt_node_t *virtual_tree;

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
void dt_lookup_intmap(dt_node_t *node);

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
	int use_no_law;
} pma_t;

uint64_t dt_get_timebase_freq(void);

int fdt_xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                      uint32_t *addrbuf, phys_addr_t *size,
                      uint32_t naddr, uint32_t nsize);
int fdt_xlate_reg(const void *tree, int node, const uint32_t *reg,
                  phys_addr_t *addr, phys_addr_t *size);
int fdt_get_reg(const void *tree, int node, int res,
                phys_addr_t *addr, phys_addr_t *size);


#endif
