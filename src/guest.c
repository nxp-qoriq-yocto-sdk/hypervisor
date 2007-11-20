
/*
 * Initial hack:
 *   hypervisor is at 0x10000000 physical
 *   guest is at 0x20000000 physical
 *
 */

#include <uv.h>
#include <percpu.h>
#include <paging.h>
#include "tlb.h"

void branch_to_guest(register_t r3, register_t r4, register_t r5,
                     register_t r6, register_t r7, uint32_t vaddr);

guest_t guest;

void start_guest(void)
{

#define GUEST_RPN             (0x20000000 >> PAGE_SHIFT)
#define GUEST_EPN             (0x00000000 >> PAGE_SHIFT)
#define GUEST_SIZE            TLB_TSIZE_256M

	hcpu->gcpu->guest = &guest;
	guest.mem_start = 0;
	guest.mem_end = tsize_to_pages(GUEST_SIZE) << PAGE_SHIFT;
	guest.mem_real = GUEST_RPN << PAGE_SHIFT;
	guest.gphys = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
	guest.gphys_rev = alloc(PAGE_SIZE * 2, PAGE_SIZE * 2);
   
	vptbl_map(guest.gphys, GUEST_EPN, GUEST_RPN,
	          tsize_to_pages(GUEST_SIZE), PTE_ALL);

	vptbl_map(guest.gphys, 0xfe000, 0xfe000,
	          tsize_to_pages(TLB_SIZE_16M), PTE_ALL);

	vptbl_map(guest.gphys_rev, GUEST_RPN, GUEST_EPN,
	          tsize_to_pages(GUEST_SIZE), PTE_ALL);

	vptbl_map(guest.gphys_rev, 0xfe000, 0xfe000,
	          tsize_to_pages(TLB_SIZE_16M), PTE_ALL);

	guest_set_tlb1(0, GUEST_SIZE << MAS1_TSIZE_SHIFT,
	               GUEST_EPN, GUEST_EPN,
	               TLB_MAS2_MEM, TLB_MAS3_KERN);

	branch_to_guest(0x00f00000, 0, 0, 0, 0, GUEST_EPN << PAGE_SHIFT);

   /* this never returns */

}
