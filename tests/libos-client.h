#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define PHYSBASE 0
#define BASE_TLB_ENTRY 15

#ifndef _ASM
typedef int client_cpu_t;
#endif

#if 0
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
#endif

#endif
