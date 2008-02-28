#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#include <config/autoconf.h>

#define BASE_TLB_ENTRY 61 /**< TLB entry used for initial mapping */
#define PHYSBASE 0x40000000 /**< Virtual base of physical address space */
#define HYPERVISOR /**< Indicates that we have the Embedded Hypervisor APU */
#define INTERRUPTS /**< Indicates that we are interrupt-driven */

#ifndef _ASM
typedef struct {
	struct gcpu_t *gcpu;
} client_cpu_t;

#include <interrupts.h>

extern unsigned long CCSRBAR_VA; /**< Deprecated virtual base of CCSR */
#endif

#define EXC_CRIT_INT_HANDLER critical_interrupt
#define EXC_MCHECK_HANDLER powerpc_mchk_interrupt
#define EXC_DSI_HANDLER data_storage
#define EXC_ALIGN_HANDLER reflect_trap
#define EXC_PROGRAM_HANDLER reflect_trap
#define EXC_FPUNAVAIL_HANDLER reflect_trap
#define EXC_DECR_HANDLER decrementer
#define EXC_DTLB_HANDLER dtlb_miss
#define EXC_GDOORBELL_HANDLER guest_doorbell
#define EXC_GDOORBELLC_HANDLER guest_critical_doorbell
#define EXC_HCALL_HANDLER hcall
#define EXC_EHPRIV_HANDLER hvpriv
#define EXC_DOORBELLC_HANDLER critical_doorbell_int

#endif
