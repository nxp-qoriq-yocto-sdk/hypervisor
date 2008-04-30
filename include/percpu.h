/** @file
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */

#ifndef PERCPU_H
#define PERCPU_H


#ifndef _ASM
#include <stdint.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/percpu.h>
#include <hv.h>
#include <vpic.h>
#endif

#define TLB1_GSIZE 16 /* As seen by the guest */

#ifndef _ASM
struct pte_t;
struct guest;

#define MAX_HANDLES 32

struct handle;

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
	guest_stopping = 3
} gstate_t;

typedef struct guest {
	vpic_t vpic;
	/**  config option whether we are in coreint mode */
	int coreint:1;
	struct pte *gphys;      /**< guest phys to real phys mapping */
	struct pte *gphys_rev;	/**< real phys to guest phys mapping */
	char *name;
	void *devtree;
	handle_t handle;        /**< The handle of *this* guest */
	handle_t *handles[MAX_HANDLES];
	/** Used as a reference count when stopping a partition. */
	unsigned long active_cpus;
	unsigned int cpucnt;    /**< The number of entries in gcpus[] */
	struct gcpu **gcpus;
	struct boot_spin_table *spintbl;
	uint32_t lpid;
	phys_addr_t entry;	/* Guest physical addr of the OS entry point */

	/** Offset to partition node in main device tree. */
	int partition;
	uint32_t lock;
	gstate_t state;
} guest_t;

#define GCPU_PEND_DECR     0x00000001 /* Decrementer event pending */
#define GCPU_PEND_TCR_DIE  0x00000002 /* Set TCR[DIE] after pending decr. */
#define GCPU_PEND_MSGSND   0x00000004 /* Guest OS msgsnd */
#define GCPU_PEND_MSGSNDC  0x00000008 /* Guest OS critical doorbell msgsnd */
#define GCPU_PEND_FIT      0x00000010 /* FIT event pending */
#define GCPU_PEND_TCR_FIE  0x00000020 /* Set TCR[FIE] after pending decr. */
#define GCPU_PEND_VIRQ     0x00000040 /* Virtual IRQ pending */

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

typedef struct gcpu {
	kstack_t uvstack;
	guest_t *guest;
	cpu_t *cpu;
	register_t ivpr;
	register_t ivor[38];
	tlbmap_t tlb1_inuse;
	tlbmap_t tlb1_map[TLB1_GSIZE];
	tlb_entry_t gtlb1[TLB1_GSIZE];
	register_t csrr0, csrr1, mcsrr0, mcsrr1, mcsr;
	uint64_t mcar;
	unsigned long gdbell_pending;
	unsigned long cdbell_pending;
	unsigned long gevent_pending;
	uint32_t timer_flags;
	int gcpu_num, waiting_for_gevent;
	vpic_cpu_t vpic;
} gcpu_t;

#define get_gcpu() (cpu->client.gcpu)

int alloc_guest_handle(guest_t *guest, handle_t *handle);
guest_t *node_to_partition(int partition);

#endif
#endif
