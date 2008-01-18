#ifndef PERCPU_H
#define PERCPU_H

/*
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */

#ifndef _ASM
#include <stdint.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/percpu.h>
#include <uv.h>
#endif

#define TLB1_GSIZE 16 // As seen by the guest

#ifndef _ASM
struct pte_t;

typedef struct {
	struct pte_t *gphys;         // guest phys to real phys mapping
	struct pte_t *gphys_rev;     // real phys to guest phys mapping
	char *name;
	void *devtree;
	uint32_t lpid;
	uint32_t *bc;                // array of byte channel handles
	uint32_t bc_cnt;             // # of channels
} guest_t;

#define GCPU_PEND_DECR	0x00000001 // Decrementer event pending

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

typedef struct gcpu_t {
	kstack_t uvstack;
	guest_t *guest;
	register_t ivpr;
	register_t ivor[38];
	tlbmap_t tlb1_free;
	tlbmap_t tlb1_used[TLB1_GSIZE];
	tlb_entry_t gtlb1[TLB1_GSIZE];
	register_t csrr0, csrr1, mcsrr0, mcsrr1, mcsr;
	uint64_t mcar;
	uint32_t timer_flags;
	int pending;
} gcpu_t;

#define get_gcpu() (cpu->client.gcpu)

#endif
#endif
