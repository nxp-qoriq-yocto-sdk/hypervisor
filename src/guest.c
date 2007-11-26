
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

static const unsigned long guest_io_pages[] = {
	0xfe11c, TLB_TSIZE_4K, // DUART
	0xfe11d, TLB_TSIZE_4K, // DUART
	0xfe040, TLB_TSIZE_256K, // MPIC
	0xfe118, TLB_TSIZE_4K, // I2C
	0xfe31c, TLB_TSIZE_16K, // ethernet
};

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

	vptbl_map(guest.gphys_rev, GUEST_RPN, GUEST_EPN,
	          tsize_to_pages(GUEST_SIZE), PTE_ALL);

	for (int i = 0; i < sizeof(guest_io_pages) / sizeof(long); i+= 2) {
		unsigned long page = guest_io_pages[i];
		unsigned long len = tsize_to_pages(guest_io_pages[i + 1]);
		
		vptbl_map(guest.gphys, page, page, len, PTE_ALL);
		vptbl_map(guest.gphys_rev, page, page, len, PTE_ALL);
	}

	guest_set_tlb1(0, GUEST_SIZE << MAS1_TSIZE_SHIFT,
	               GUEST_EPN, GUEST_EPN,
	               TLB_MAS2_MEM, TLB_MAS3_KERN);

	branch_to_guest(0x00f00000, 0, 0, 0, 0, GUEST_EPN << PAGE_SHIFT);

   /* this never returns */

}
