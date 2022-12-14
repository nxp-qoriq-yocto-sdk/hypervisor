/** @file
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */
/*
 * Copyright (C) 2007-2010 Freescale Semiconductor, Inc.
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

#ifndef PERCPU_H
#define PERCPU_H

#ifndef _ASM
#include <stdint.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/percpu.h>
#include <libos/queue.h>
#include <hv.h>
#include <vpic.h>
#include <tlbcache.h>
#include <devtree.h>
#include <thread.h>
#include <benchmark.h>
#include <handle.h>
#endif

extern cpu_t secondary_cpus[CONFIG_LIBOS_MAX_CPUS - 1];

#define TLB1_GSIZE 16 /* As seen by the guest */

#ifndef _ASM
struct pte_t;
struct guest;

#define MAX_HANDLES 1024

struct ipi_doorbell;
struct stub_ops;

/**
 * Guest states
 *
 * The first state (value 0) must be the default state
 *
 * This list is ordered based on the GET_STATUS hypercall API.
 */
typedef enum {
	guest_stopped = 0,
	guest_running = 1,
	guest_starting = 2,
	guest_stopping = 3,
	guest_pausing = 4,
	guest_paused = 5,
	guest_resuming = 6,

	/* Internal substates */
	guest_starting_min,
	guest_starting_uninit,
	guest_starting_max,

	guest_stopping_min,
	guest_stopping_percpu,
	guest_stopping_max,
} gstate_t;

/** Possible watchdog timeout actions */
typedef enum {
	wd_reset = 0, /**< Reset partition on watchdog timeout */
	wd_notify = 1, /**< Notify manager partition on watchdog timeout */
	wd_stop = 2, /**< Stop partition on watchdog timeout */
} wd_action_t;

typedef struct guest {
	vpic_t vpic;
	struct pte *gphys;      /**< guest phys to real phys mapping */
	struct pte *gphys_rev;	/**< real phys to guest phys mapping */
	phys_addr_t end_of_gphys;
	const char *name;
	struct dt_node *devtree;
	struct dt_node *partition; /**< Partition node in config tree. */

	/** Container for devices in guest tree.  This is normally the root
	 * node, however an explicit container node will be created if
	 * device-ranges is specified.
	 */
	struct dt_node *devices;
	
	/* device-ranges property, or NULL if none */
	struct dt_prop *devranges;

	handle_t handle;        /**< The handle of *this* guest */
	handle_t *handles[MAX_HANDLES];
	/** Used as a reference count when stopping a partition. */
	unsigned long active_cpus;
	struct gcpu **gcpus;
	struct boot_spin_table *spintbl;
	/** Guest physical/virtual addr of the OS entry point. */
	register_t entry;       

	phys_addr_t dtb_gphys;  /**< Guest physical addr of DTB image */
	phys_addr_t dtb_window_len; /**< Length of guest DTB window */
	register_t tlbivax_addr; /**< Used for tlbivax shootdown IPIs */
	register_t tlbivax_mas6; /**< Used for tlbivax ipis */

	/** Countdown to wait for all cores to invalidate */
	register_t tlbivax_count;

	unsigned int cpucnt;    /**< The number of entries in gcpus[] */
	uint32_t *cpulist;
	uint32_t cpulist_len;
	uint32_t id;

	/** guest debug mode **/
	int guest_debug_mode;
	
#ifdef CONFIG_DEBUG_STUB
	struct stub_ops *stub_ops;
#endif
	uint32_t state_lock, sync_ipi_lock;
	gstate_t state;

	int guest_cache_lock;

	int mpic_direct_eoi;

	int privileged;

	/** Action takeon on watchdog expiration.
	 *See #wd_action_t for possible values.
	 */
	wd_action_t wd_action;

	/** Period with which to autostart the guest watchdog at guest
	 *  start or -1 to disable this feature.
	 */
	int wd_autostart;

	/** On partition stop, defer DMA disable to manager hcall, but
	 *  disable as usual on cold boot and partition reset.
	 */
	int defer_dma_disable;

	/** Partition-scope -- opt out of the feature completely */
	int no_dma_disable;

	/** The doorbell handle to use to signal a state change in the
	 *  this partition.  The receivers are the managers of this partition.
	 */
	struct ipi_doorbell *dbell_state_change;

	/** The doorbell handle to use to signal the expiration of
	 *  watchdog in this partition.  The receivers are the managers of
	 *  this partition.
	 */
	struct ipi_doorbell *dbell_watchdog_expiration;

	/** The doorbell handle to use to request a restart of
	 *  this partition.  The receivers are the managers of this partition.
	 */
	struct ipi_doorbell *dbell_restart_request;

	/** List of owned devices (dev_owner_t objects) */
	list_t dev_list;

	/** Error event queue */
	queue_t error_event_queue;
	handle_t error_queue_handle;  /**< handle for the error event queue */

	/** Platform error log producer lock*/
	uint32_t error_log_prod_lock;

	/** List of deferred node-update-phandles.  Nodes are update_phandle_t. */
	list_t phandle_update_list;

	/** The doorbell handle to use by a manager partition to send a shutdown
	 *  request to this partition
	 */
	struct ipi_doorbell *dbell_shutdown;

#ifdef CONFIG_DEVICE_VIRT
	/** List of virtualized devices (vf_list_t objects). */
	list_t vf_list;
#endif
#ifdef CONFIG_VIRTUAL_I2C
	/** Emulated SR register */
	uint8_t i2c_sr;
#endif

	/** phandle of the guest vmpic node */
	uint32_t vmpic_phandle;

	/** If !0, then don't load images from image-table on start */
	int no_auto_load;

	/** Error queue lock, to synchronize access to the error queue */
	uint32_t error_queue_lock;

	/** VCPU used for sending critical interrupts to global manager */
	struct gcpu *err_destcpu;

	/** portal-devices node under partition config node */
	dt_node_t *portal_devs;

	/** allow TLB management instructions (tlbwe, tlbilx) to be executed
	 * in guest state
	 */
	int direct_guest_tlb_mgt;

	/** TLB miss interrupts are taken directly by the guest */
	int direct_guest_tlb_miss;

} guest_t;

extern struct guest guests[CONFIG_MAX_PARTITIONS];
extern unsigned long num_guests;
extern unsigned long active_guests;

/* The following flags correspond to gdbell_pending */
#define GCPU_PEND_MSGSND   0x00000004 /* Guest OS msgsnd */
#define GCPU_PEND_VIRQ     0x00000040 /* Virtual IRQ pending */
#define GCPU_PEND_PERFMON  0x00000200 /* Performance monitor interrupt */

/* The following flags correspond to crit_gdbell_pending */
#define GCPU_PEND_MSGSNDC  0x00000008 /* Guest OS critical doorbell msgsnd */
#define GCPU_PEND_CRIT_INT 0x00000100 /* Guest Critical interrupt */

// Bit mask of TCR bits that guest can access directly
#define GCPU_TCR_HW_BITS   (TCR_DIE | TCR_ARE)

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

typedef struct gcpu {
	kstack_t hvstack;
	guest_t *guest;
	cpu_t *cpu;
	thread_t thread;
	tlbmap_t tlb1_map[TLB1_GSIZE];
	tlb_entry_t gtlb1[TLB1_GSIZE];
	unsigned long split_gtlb1_map;

#ifdef CONFIG_FAST_TLB1
	/* Maps a real tlb1 entry to the corresponding guest tlb1 entry */
	int fast_tlb1_to_gtlb1[GUEST_TLB_END + 1];
#endif

	unsigned long dbell_pending;
	unsigned long gevent_pending;
	unsigned int gcpu_num;
	/* lpid used by this cpu / thread */
	uint32_t lpid;
	vpic_cpu_t vpic;
	uint32_t watchdog_tsr;	// Upon watchdog reset, TSR[WRS] <- TCR[WRC]
#ifdef CONFIG_DEBUG_STUB
	void *dbgstub_cpu_data;
	dt_node_t *dbgstub_cfg;
#endif

	/* Fields after this point are cleared on partition reset -- do
	 * not insert new fields between this comment and gdbell_pending.
	 */
	unsigned long gdbell_pending;
	unsigned long crit_gdbell_pending;
	register_t csrr0, csrr1, mcsrr0, mcsrr1, mcsr, dsrr0, dsrr1;
	uint64_t mcar;
	register_t ivpr;
	register_t ivor[38];
	register_t sprg[6]; /* Guest SPRG4-9 */
	register_t mas0, mas1, mas2, mas3, mas6, mas7;
	uint32_t gtcr;  // virtualized guest TCR
	// gtsr should be a u32, but atomic_or() takes an unsigned long
	unsigned long gtsr;  // virtualized guest TSR
	int clean_tlb, clean_tlb_pid;

/*** gcpu is napping on explicit request */
#define GCPU_NAPPING_HCALL 1
/*** gcpu is napping because of guest is paused/stopped, or no guest on core */
#define GCPU_NAPPING_STATE 2
	unsigned long napping;

#ifdef CONFIG_STATISTICS
	struct benchmark benchmarks[num_benchmarks];
#endif
} gcpu_t;

typedef struct shared_cpu {
	/** HV dynamic TLB round-robin eviction pointer */
	int next_dyn_tlbe;

	/** Spinlock used to synchronize TLB1 operations done on hw threads */
	uint32_t tlblock;

	tlbmap_t tlb1_inuse;

	int evict_tlb1;

	/* lrat entries are allocated in a round robin fashion */
	int lrat_next_entry;

	/** Spinlock used to synchronize L1 flushes done on hw threads */
	uint32_t cachelock;

	/** PWRMGTCR0 spr for each hardware thread */
	register_t pwrmgtcr0_sprs[CONFIG_LIBOS_MAX_HW_THREADS];
	uint32_t pwrmgtcr0_lock;
} shared_cpu_t;

#define get_shared_cpu() (cpu->client.shared)

#if CONFIG_LIBOS_MAX_HW_THREADS > 1

static inline register_t tlb_lock(void)
{
	return spin_lock_intsave(&get_shared_cpu()->tlblock);
}

static inline void tlb_unlock(register_t saved)
{
	spin_unlock_intsave(&get_shared_cpu()->tlblock, saved);
}

#else

static inline register_t tlb_lock(void)
{
	return 0;
}

static inline void tlb_unlock(register_t saved)
{
}

#endif

#define get_gcpu() (cpu->client.gcpu)

struct dt_node;

int set_guest_global_handle(guest_t *guest, handle_t *handle,
                            int global_handle);
int alloc_global_handle(void);
int alloc_guest_handle(guest_t *guest, handle_t *handle);
guest_t *node_to_partition(struct dt_node *partition);

#endif
#endif
