
/*
 * Initial hack:
 *   hypervisor is at 0x10000000 physical
 *   guest is at 0x20000000 physical
 *
 */

#include <percpu.h>
#include "tlb.h"

void  branch_to_guest(uint32_t vaddr);

guest_t guest;

void start_guest(void)
{

#define GUEST_PA              0x20000000
#define GUEST_VA              0x00000000
#define GUEST_SIZE            0x01000000
#define GUEST_TID             0x0
#define GUEST_GS              0x1

	hcpu->gcpu->guest = &guest;
	guest.mem_start = 0;
	guest.mem_end = GUEST_SIZE;
	guest.mem_real = GUEST_PA;
    
    /* set up a tlb mapping for the guest */
    __tlb1_set_entry(0, GUEST_VA, GUEST_PA, GUEST_SIZE, _TLB_ENTRY_MEM, UV_TID, 0, GUEST_GS);

   branch_to_guest(GUEST_VA);

   /* this never returns */

}
