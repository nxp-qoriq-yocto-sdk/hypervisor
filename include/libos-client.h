#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define BASE_TLB_ENTRY 61 /**< TLB entry used for initial mapping */
#define PHYSBASE 0x40000000 /**< Virtual base of physical address space */
#define HYPERVISOR /**< Indicates that we have the Embedded Hypervisor APU */
#define INTERRUPTS /**< Indicates that we are interrupt-driven */
#define TOPAZ /**< Turns on Topaz-specfic hacks in libos */

#ifndef _ASM
typedef struct {
	struct gcpu *gcpu;
} client_cpu_t;

extern unsigned long CCSRBAR_VA; /**< Deprecated virtual base of CCSR */
#endif

#define EXC_CRIT_INT_HANDLER critical_interrupt
#define EXC_MCHECK_HANDLER powerpc_mchk_interrupt
#define EXC_DSI_HANDLER data_storage
#define EXC_ALIGN_HANDLER reflect_trap
#define EXC_PROGRAM_HANDLER reflect_trap
#define EXC_FPUNAVAIL_HANDLER reflect_trap
#define EXC_DECR_HANDLER decrementer
#define EXC_FIT_HANDLER fit
#define EXC_DTLB_HANDLER dtlb_miss
#define EXC_GDOORBELL_HANDLER guest_doorbell
#define EXC_GDOORBELLC_HANDLER guest_critical_doorbell
#define EXC_HCALL_HANDLER hcall
#define EXC_EHPRIV_HANDLER hvpriv
#define EXC_DOORBELLC_HANDLER critical_doorbell_int

#define LIBOS_RET_USER_HOOK ret_to_guest

#define LOGTYPE_GUEST_MMU    (CLIENT_BASE_LOGTYPE + 0)
#define LOGTYPE_EMU          (CLIENT_BASE_LOGTYPE + 1)
#define LOGTYPE_PARTITION    (CLIENT_BASE_LOGTYPE + 2)
#define LOGTYPE_GDB_STUB     (CLIENT_BASE_LOGTYPE + 3)
#define LOGTYPE_BYTE_CHAN    (CLIENT_BASE_LOGTYPE + 4)
#define LOGTYPE_DOORBELL     (CLIENT_BASE_LOGTYPE + 5)

#endif
