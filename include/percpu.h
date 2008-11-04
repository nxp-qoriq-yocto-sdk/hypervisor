/** @file
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */
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

#ifndef PERCPU_H
#define PERCPU_H


#ifndef _ASM
#include <stdint.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/percpu.h>
#include <hv.h>
#include <vpic.h>
#include <tlbcache.h>
#endif

#define TLB1_GSIZE 16 /* As seen by the guest */

#ifndef _ASM
struct pte_t;
struct guest;

#define MAX_HANDLES 1024

struct handle;
struct ipi_doorbell;

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
  Add pointers here for other handle types as they are needed.*/
typedef struct handle {
	handle_ops_t *ops;
	struct byte_chan_handle *bc;
	struct vmpic_interrupt *intr;
	struct ipi_doorbell_handle *db;
	struct pamu_handle *pamu;
	struct ppid_handle *ppid;
	struct guest *guest;
} handle_t;

/** operations for debug stubs
 *
 */
typedef struct {
	int (*debug_int_handler)(trapframe_t *trap_frame);
	void (*wait_at_start_hook)(uint32_t entry, register_t msr);
} stub_ops_t;

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
	guest_stopping = 3
} gstate_t;

typedef struct guest {
	vpic_t vpic;
	struct pte *gphys;      /**< guest phys to real phys mapping */
	struct pte *gphys_rev;	/**< real phys to guest phys mapping */
	const char *name;
	void *devtree;
	handle_t handle;        /**< The handle of *this* guest */
	handle_t *handles[MAX_HANDLES];
	/** Used as a reference count when stopping a partition. */
	unsigned long active_cpus;
	struct gcpu **gcpus;
	struct boot_spin_table *spintbl;
	/** Guest physical/virtual addr of the OS entry point. */
	register_t entry;       
	phys_addr_t dtb_gphys;  /**< Guest physical addr of DTB image */
	register_t tlbivax_addr;/**< Used for tlbivax shootdown IPIs */

	/** Countdown to wait for all cores to invalidate */
	register_t tlbivax_count;

	unsigned int cpucnt;    /**< The number of entries in gcpus[] */
	uint32_t lpid;

	/** Offset to partition node in main device tree. */
	int partition;

	/** guest debug mode **/
	int guest_debug_mode;
	
	/** #address-cells and #size-cells at root of guest tree */
	uint32_t naddr, nsize;

	stub_ops_t *stub_ops;
	uint32_t state_lock, inv_lock;
	gstate_t state;

	int guest_cache_lock;

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
} guest_t;

#define MAX_PARTITIONS 8

extern struct guest guests[MAX_PARTITIONS];
extern unsigned long last_lpid;

#define GCPU_PEND_DECR     0x00000001 /* Decrementer event pending */
#define GCPU_PEND_TCR_DIE  0x00000002 /* Set TCR[DIE] after pending decr. */
#define GCPU_PEND_MSGSND   0x00000004 /* Guest OS msgsnd */
#define GCPU_PEND_MSGSNDC  0x00000008 /* Guest OS critical doorbell msgsnd */
#define GCPU_PEND_FIT      0x00000010 /* FIT event pending */
#define GCPU_PEND_TCR_FIE  0x00000020 /* Set TCR[FIE] after pending decr. */
#define GCPU_PEND_VIRQ     0x00000040 /* Virtual IRQ pending */
#define GCPU_PEND_WATCHDOG 0x00000080 /* Watchdog timeout event pending */

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

enum gcpu_stats {
	stat_emu_total, /**< Total emulated instructions */
	stat_emu_tlbwe, /**< Emulated tlbwe instructions */
	stat_emu_spr,   /**< Emulated SPR accesses */
	stat_decr,      /**< Decrementer interrupts */
	num_gcpu_stats
};
	
typedef struct gcpu {
	kstack_t hvstack;
	guest_t *guest;
	cpu_t *cpu;
	tlbmap_t tlb1_inuse;
	tlbmap_t tlb1_map[TLB1_GSIZE];
	tlb_entry_t gtlb1[TLB1_GSIZE];
	unsigned long cdbell_pending;
	unsigned long gevent_pending;
	int gcpu_num, waiting_for_gevent;
	vpic_cpu_t vpic;

	/* Fields after this point are cleared on reset -- do not
	 * insert new fields before gdbell_pending.
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
	int evict_tlb1;
	int watchdog_timeout;	/* 0=normal, 1=next WD int restarts partition */
	
	unsigned int stats[num_gcpu_stats];
	void *debug_stub_data;
} gcpu_t;

#define get_gcpu() (cpu->client.gcpu)

int alloc_guest_handle(guest_t *guest, handle_t *handle);
guest_t *node_to_partition(int partition);

#endif
#endif
