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

#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define BASE_TLB_ENTRY 63 /**< TLB entry used for initial mapping */
#define PHYSBASE 0x400000 /**< Virtual base of text mapping */
#define PHYSMAPSIZE TLB_TSIZE_4M
#define BIGPHYSBASE 0x40000000 /**< Virtual base of large page physical mapping */
#define VMAPBASE 0x10000000 /**< Base of dynamically allocated virtual space */
#define HYPERVISOR /**< Indicates that we have the Embedded Hypervisor APU */
#define INTERRUPTS /**< Indicates that we are interrupt-driven */
#define TOPAZ /**< Turns on Topaz-specfic hacks in libos */
#ifndef CONFIG_LIBOS_64BIT
#define KSTACK_SIZE 4096
#else
#define KSTACK_SIZE (4096 * 4)
#endif
/* This stack size applies to all exception levels. It
 * changed from 2KB to 4KB due to stack pressure with the
 * TRK debug stub.
 */

#ifndef _ASM

#include <stdint.h>

extern uint64_t text_phys, bigmap_phys;

#define HAVE_VIRT_TO_PHYS
static inline uint64_t virt_to_phys(void *ptr)
{
	if ((uintptr_t)ptr >= BIGPHYSBASE)
		return (uintptr_t)ptr - BIGPHYSBASE + bigmap_phys;

	return (uintptr_t)ptr - PHYSBASE + text_phys;
}

typedef struct {
	/** Fast exception save area
	 * The entire cache line will be clobbered.  This may not be used
	 * within critical interrupts (or similar elevated exception levels). 
	 * This must be the first thing in cpu_t.
	 */
	char fastsave[16 * sizeof(unsigned long)] __attribute__((aligned(64)));

	struct gcpu *gcpu;

	struct tlbcset *tlbcache;

	/* The number of virtual address bits used as an index
	 * into the TLB cache.
	 */
	unsigned int tlbcache_bits;

	/* Indicates if the TLB cache for TLB 0 is enabled */
	int tlbcache_enable;

	/* indicates if TLB 1 is virtualized. TLB 1 is virtualized when
	 * the tlb misses are directed to the Hypervisor */
	int tlb1_virt;

	/** HV dynamic TLB round-robin eviction pointer */
	int next_dyn_tlbe;

	uint64_t previous_tb; // The previous value of tb

	/** When set, sync_nap() will put the core into nap state.  When
	 * clear, sync_nap() will wake the core from nap state.
	 */
	int nap_request;
} client_cpu_t;

extern unsigned long CCSRBAR_VA; /**< Deprecated virtual base of CCSR */

#define libos_prepare_to_block prepare_to_block
#define libos_block block

struct libos_thread;

void prepare_to_block(void);
void block(void);
void libos_unblock(struct libos_thread *thread);

/* These are used by assembly code */
struct trapframe;

void critical_interrupt(struct trapframe *frameptr);
void guest_doorbell(struct trapframe *regs);
void guest_critical_doorbell(struct trapframe *regs);
void inst_storage(struct trapframe *regs);
void external_int(struct trapframe *regs);
void powerpc_mchk_interrupt(struct trapframe *frameptr);
void program_trap(struct trapframe *regs);
void debug_trap(struct trapframe *regs);
void data_storage(struct trapframe *regs);
void tlb_miss(struct trapframe *regs);
void doorbell_int(struct trapframe *regs);
void crit_dbell_int(struct trapframe *regs);
void hvpriv(struct trapframe *regs);
void decrementer(struct trapframe *regs);
void fit(struct trapframe *regs);
void watchdog_trap(struct trapframe *regs);
void hcall(struct trapframe *regs);
void align_trap(struct trapframe *regs);
void fpunavail(struct trapframe *regs);
void perfmon_int(struct trapframe *regs);
#endif

#define EXC_CRIT_INT_HANDLER critical_interrupt
#define EXC_MCHECK_HANDLER powerpc_mchk_interrupt
#define EXC_DSI_HANDLER data_storage
#define EXC_ISI_HANDLER inst_storage
#define EXC_EXT_INT_HANDLER external_int
#define EXC_ALIGN_HANDLER align_trap
#define EXC_PROGRAM_HANDLER program_trap
#define EXC_FPUNAVAIL_HANDLER fpunavail
#define EXC_DECR_HANDLER decrementer
#define EXC_FIT_HANDLER fit
#define EXC_DTLB_HANDLER tlb_miss
#define EXC_ITLB_HANDLER tlb_miss
#define EXC_GDOORBELL_HANDLER guest_doorbell
#define EXC_GDOORBELLC_HANDLER guest_critical_doorbell
#define EXC_HCALL_HANDLER hcall
#define EXC_EHPRIV_HANDLER hvpriv
#define EXC_DOORBELL_HANDLER doorbell_int
#define EXC_DOORBELLC_HANDLER crit_dbell_int
#define EXC_DEBUG_HANDLER debug_trap
#define EXC_WDOG_HANDLER watchdog_trap
#define EXC_PERFMON_HANDLER perfmon_int

#ifdef CONFIG_STATISTICS
#define UPDATE_STATS statistics_stop
#endif

#define LIBOS_RET_HOOK return_hook

#define LOGTYPE_GUEST_MMU    (CLIENT_BASE_LOGTYPE + 0)
#define LOGTYPE_EMU          (CLIENT_BASE_LOGTYPE + 1)
#define LOGTYPE_PARTITION    (CLIENT_BASE_LOGTYPE + 2)
#define LOGTYPE_DEBUG_STUB   (CLIENT_BASE_LOGTYPE + 3)
#define LOGTYPE_BYTE_CHAN    (CLIENT_BASE_LOGTYPE + 4)
#define LOGTYPE_DOORBELL     (CLIENT_BASE_LOGTYPE + 5)
#define LOGTYPE_BCMUX        (CLIENT_BASE_LOGTYPE + 6)
#define LOGTYPE_DEVTREE      (CLIENT_BASE_LOGTYPE + 7)
#define LOGTYPE_PAMU         (CLIENT_BASE_LOGTYPE + 8)
#define LOGTYPE_CCM          (CLIENT_BASE_LOGTYPE + 9)
#define LOGTYPE_CPC          (CLIENT_BASE_LOGTYPE + 10)
#define LOGTYPE_GUTS         (CLIENT_BASE_LOGTYPE + 11)
#define LOGTYPE_PM           (CLIENT_BASE_LOGTYPE + 12)
#define LOGTYPE_DDR          (CLIENT_BASE_LOGTYPE + 13)
#define LOGTYPE_ERRORQ       (CLIENT_BASE_LOGTYPE + 14)

#endif
