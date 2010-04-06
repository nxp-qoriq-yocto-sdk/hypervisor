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
#endif

extern cpu_t secondary_cpus[MAX_CORES - 1];

#define TLB1_GSIZE 16 /* As seen by the guest */

#ifndef _ASM
struct pte_t;
struct guest;

#define MAX_HANDLES 1024

struct handle;
struct ipi_doorbell;
struct stub_ops;

/** General handle operations that many types of handle will implement.
 * Any member may be NULL.
 */
typedef struct {
	/** Reset the handle to partition boot state.
	 * If the handle was dynamically created, rather than
	 * device-tree-originated, then close the handle.
	 */
	void (*reset)(struct handle *h);
} handle_ops_t;

/* An extremely crude form of RTTI/multiple interfaces...
 * Add pointers here for other handle types as they are needed.
 */
typedef struct handle {
	handle_ops_t *ops;
	struct byte_chan_handle *bc;
	struct vmpic_interrupt *intr;
	struct ipi_doorbell_handle *db;
	struct pamu_handle *pamu;
	struct ppid_handle *ppid;
	struct guest *guest;
} handle_t;

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
} gstate_t;

typedef struct guest {
	vpic_t vpic;
	struct pte *gphys;      /**< guest phys to real phys mapping */
	struct pte *gphys_rev;	/**< real phys to guest phys mapping */
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

	/** Countdown to wait for all cores to invalidate */
	register_t tlbivax_count;

	unsigned int cpucnt;    /**< The number of entries in gcpus[] */
	uint32_t *cpulist;
	uint32_t cpulist_len;
	uint32_t lpid;

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

	/** Watchdog notify manager partition on time-out: 0=no, 1=yes */
	int wd_notify;

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
} guest_t;

#define MAX_PARTITIONS 8

extern struct guest guests[MAX_PARTITIONS];
extern unsigned long last_lpid;

/* The following flags correspond to gdbell_pending */
#define GCPU_PEND_DECR     0x00000001 /* Decrementer event pending */
#define GCPU_PEND_TCR_DIE  0x00000002 /* Set TCR[DIE] after pending decr. */
#define GCPU_PEND_MSGSND   0x00000004 /* Guest OS msgsnd */
#define GCPU_PEND_FIT      0x00000010 /* FIT event pending */
#define GCPU_PEND_TCR_FIE  0x00000020 /* Set TCR[FIE] after pending decr. */
#define GCPU_PEND_VIRQ     0x00000040 /* Virtual IRQ pending */

/* The following flags correspond to crit_gdbell_pending */
#define GCPU_PEND_MSGSNDC  0x00000008 /* Guest OS critical doorbell msgsnd */
#define GCPU_PEND_WATCHDOG 0x00000080 /* Watchdog timeout event pending */
#define GCPU_PEND_CRIT_INT 0x00000100 /* Guest Critical interrupt */
#define GCPU_PEND_PERFMON  0x00000200 /* Performance monitor interrupt */

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

typedef struct gcpu {
	kstack_t hvstack;
	guest_t *guest;
	cpu_t *cpu;
	thread_t thread;
	tlbmap_t tlb1_inuse;
	tlbmap_t tlb1_map[TLB1_GSIZE];
	tlb_entry_t gtlb1[TLB1_GSIZE];
	unsigned long dbell_pending;
	unsigned long gevent_pending;
	unsigned int gcpu_num;
	vpic_cpu_t vpic;
	register_t tsr;	// Upon watchdog reset, TSR[WRS] <- TCR[WRC]
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
	uint32_t timer_flags;
	int evict_tlb1, clean_tlb, clean_tlb_pid;
	int watchdog_timeout;	/* 0=normal, 1=next WD int restarts partition */

/*** gcpu is napping on explicit request */
#define GCPU_NAPPING_HCALL 1
/*** gcpu is napping because of guest is paused/stopped, or no guest on core */
#define GCPU_NAPPING_STATE 2
	unsigned long napping;

#ifdef CONFIG_STATISTICS
	struct benchmark benchmarks[num_benchmarks];
#endif
} gcpu_t;

#define get_gcpu() (cpu->client.gcpu)

struct dt_node;

int set_guest_global_handle(guest_t *guest, handle_t *handle,
                            int global_handle);
int alloc_global_handle(void);
int alloc_guest_handle(guest_t *guest, handle_t *handle);
guest_t *node_to_partition(struct dt_node *partition);

#endif
#endif
