#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define BASE_TLB_ENTRY 62
#define PHYSBASE 0x40000000
#define HYPERVISOR
#define INTERRUPTS

#define CCSRBAR_VA	0x01000000

#ifndef _ASM
typedef struct {
	struct gcpu_t *gcpu;
} client_cpu_t;

#include <interrupts.h>
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
#define EXC_HCALL_HANDLER hcall
#define EXC_EHPRIV_HANDLER hvpriv
#define EXC_DOORBELLC_HANDLER critical_doorbell_int

#endif
