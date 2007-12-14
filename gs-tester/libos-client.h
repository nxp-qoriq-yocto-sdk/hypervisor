#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define PHYSBASE 0
#define BASE_TLB_ENTRY 15

#define HYPERVISOR

#ifndef _ASM
typedef int client_cpu_t;
#endif

#define EXC_CRIT_INT_HANDLER trap
#define EXC_MCHECK_HANDLER trap
#define EXC_DSI_HANDLER trap
#define EXC_ISI_HANDLER trap
#define EXC_EXT_INT_HANDLER trap
#define EXC_ALIGN_HANDLER trap
#define EXC_PROGRAM_HANDLER trap
#define EXC_FPUNAVAIL_HANDLER trap
#define EXC_SYSCALL_HANDLER trap
#define EXC_AUXUNAVAIL_HANDLER trap
#define EXC_DECR_HANDLER trap
#define EXC_FIT_HANDLER trap
#define EXC_WDOG_HANDLER trap
#define EXC_DTLB_HANDLER trap
#define EXC_ITLB_HANDLER trap
#define EXC_DEBUG_HANDLER trap
#define EXC_PERFMON_HANDLER trap
#define EXC_DOORBELL_HANDLER trap
#define EXC_DOORBELLC_HANDLER trap
#define EXC_GDOORBELL_HANDLER trap
#define EXC_GDOORBELLC_HANDLER trap
#define EXC_HCALL_HANDLER trap
#define EXC_EHPRIV_HANDLER trap

#endif
