#ifndef PERCPU_H
#define PERCPU_H

/*
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */

#ifndef _ASM
#include <stdint.h>
#include "uvtypes.h"
#include "tlb.h"
#include <uv.h>
#endif

#define CPUSAVE_LEN 2
#define TLB1_SIZE  64 // Physical TLB size
#define TLB1_RSVD  62 // First TLB1 entry reserved for the hypervisor.
                      // Entries below this but above TLB1_GSIZE are
                      // used to break up guest TLB1 entries due to
                      // alignment, size, or permission holes.
#define TLB1_GSIZE 16 // As seen by the guest
#define UVSTACK_SIZE 2048

#ifndef _ASM
typedef uint8_t uvstack_t[UVSTACK_SIZE] __attribute__((aligned(16)));

struct pte_t;

typedef struct {
	uint64_t mem_start, mem_end; // guest physical address range
	uint64_t mem_real;           // real physical start addr
	struct pte_t *gphys;         // guest phys to real phys mapping
	struct pte_t *gphys_rev;     // real phys to guest phys mapping
	uint32_t lpid;
} guest_t;

#define GCPU_PEND_DECR	0x00000001 // Decrementer event pending

typedef unsigned long tlbmap_t[(TLB1_SIZE + LONG_BITS - 1) / LONG_BITS];

typedef struct {
	uvstack_t uvstack;
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

typedef struct {
	gcpu_t *gcpu;
	register_t normsave[CPUSAVE_LEN];
	register_t critsave[CPUSAVE_LEN];
	register_t machksave[CPUSAVE_LEN];
	register_t dbgsave[CPUSAVE_LEN];
	tlb_entry_t tlb1[TLB1_SIZE];
	int coreid;
	uvstack_t debugstack, critstack, mcheckstack;
} hcpu_t;

register hcpu_t *hcpu asm("r2");
#endif

#endif
