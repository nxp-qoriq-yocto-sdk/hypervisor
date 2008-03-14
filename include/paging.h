#ifndef PAGING_H
#define PAGING_H

#include <hv.h>

#define TEMPTLB1 62
#define TEMPTLB2 63

#define PHYS_BITS 36
#define VIRT_BITS 32

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define PGDIR_SIZE 1024
#define PGDIR_SHIFT 10

#define L2PAGE_SIZE (PAGE_SIZE * PGDIR_SIZE)
#define L2PAGE_SHIFT (PAGE_SHIFT + PGDIR_SHIFT)

#define PTE_VIRT_LEVELS ((VIRT_BITS - PAGE_SHIFT + PGDIR_SHIFT - 1) / PGDIR_SHIFT)
#define PTE_PHYS_LEVELS ((PHYS_BITS - PAGE_SHIFT + PGDIR_SHIFT - 1) / PGDIR_SHIFT)

/* PTEs are two words.  The upper word is the physical page number.
 * The lower word contains attributes.  Page tables consist of
 * 1024 ptes on an 8K-aligned page pair.  
 *
 * Note that SR must be set if SX is set -- or emulation will not work.
 */
#define PTE_SR      0x00000001 /* Supervisor Read */
#define PTE_UR      0x00000002 /* User Read */
#define PTE_SW      0x00000004 /* Supervisor Write */
#define PTE_UW      0x00000008 /* User Write */
#define PTE_SX      0x00000010 /* Supervisor Exec */
#define PTE_UX      0x00000020 /* User Exec */
#define PTE_VF      0x00000040 /* Virtualization Fault -- HV only */
#define PTE_GS      0x00000080 /* Guest access -- HV only */
#define PTE_VALID   0x00001000
#define PTE_GLOBAL  0x00002000 /* If set, PTE has PID zero */
#define PTE_E       0x00010000 /* Little Endian */
#define PTE_G       0x00020000 /* Guarded */
#define PTE_M       0x00040000 /* Memory Coherence Required */
#define PTE_I       0x00080000 /* Caching Inhibited */
#define PTE_W       0x00100000 /* Write-through */
#define PTE_VLE     0x00200000 /* Variable Length Encoding */
#define PTE_ACM     0x00c00000 /* Alternate Coherency Mode */
#define PTE_SIZE    0xf0000000 /* log4(page size):
                                * <=5 valid on level 1 ptes,
                                * >5 valid on level 2 (toplevel) ptes
                                * Should be zero on toplevel
                                * pointers to level 1 page tables
                                */

#define PTE_MAS1_VALID_SHIFT 19
#define PTE_MAS8_SHIFT 24
#define PTE_MAS8_MASK 0xc0000000
#define PTE_MAS3_MASK (PTE_SR | PTE_UR | PTE_SW | \
                       PTE_UW | PTE_SX | PTE_UX)

#define PTE_SIZE_SHIFT 28

#define PTE_ALL (PTE_MAS3_MASK | PTE_VALID | PTE_GS)

typedef struct pte {
	unsigned long page;
	unsigned long attr;
} pte_t;

// The xlate function(s) turn virtual addresses into physical page
// numbers, not physical addresses.  If attr->PTE_VALID is not set, then
// the return value will indicate a page size mask that should be skipped
// when scanning for a valid mapping (0 if there's a valid level 2 page
// directory entry, 0x3ff if there is not).
//
// vptbl deals expects virtual page numbers in page directory entries.
// levels should be PTE_VIRT_LEVELS for virtual->phys mappings, and
// PTE_PHYS_LEVELS for guest phys->phys and reverse mappings.

unsigned long vptbl_xlate(pte_t *tbl, unsigned long epn,
                          unsigned long *attr, int levels);

// attr->PTE_SIZE should be 0; vptbl_map will use the largest
// page sizes it can.
void vptbl_map(pte_t *tbl, unsigned long epn, unsigned long rpn,
               unsigned long npages, unsigned long attr, int levels);


void guest_set_tlb1(unsigned int entry, uint32_t mas1,
                    unsigned long epn, unsigned long grpn,
                    uint32_t mas2flags, uint32_t mas3flags);
unsigned int guest_tlb1_to_gtlb1(unsigned int idx);

#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M

void tlb1_init(void);

/** Permanent 16MiB chunk of valloc space for temporary local mappings */
extern void *temp_mapping[2];

size_t copy_to_gphys(pte_t *tbl, physaddr_t dest, void *src, size_t len);
size_t zero_to_gphys(pte_t *tbl, physaddr_t dest, size_t len);
size_t copy_from_gphys(pte_t *tbl, void *dest, physaddr_t src, size_t len);
size_t copy_between_gphys(pte_t *dtbl, physaddr_t dest,
                           pte_t *stbl, physaddr_t src, size_t len);

#endif
