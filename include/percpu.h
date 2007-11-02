#ifndef PERCPU_H
#define PERCPU_H

/*
 * Per-guest-cpu and Per-hypervisor-cpu data structures
 */

#ifndef _ASM
#include <stdint.h>
#include "uvtypes.h"
#include "tlb.h"
#endif

#define CPUSAVE_LEN 1
#define TLB1_SIZE 16
#define UVSTACK_SIZE 2048

#ifndef _ASM
typedef uint8_t uvstack_t[UVSTACK_SIZE] __attribute__((aligned(16)));

typedef struct {
	uvstack_t uvstack;
	uint64_t mem_start, mem_end; // guest physical address range
	uint64_t mem_real;           // real physical start addr
	uint32_t lpid;
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
