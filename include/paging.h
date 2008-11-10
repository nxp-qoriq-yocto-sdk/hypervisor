
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#ifndef PAGING_H
#define PAGING_H

#include <hv.h>

#define GUEST_TLB_END 47

#define TEMPTLB1 48
#define TEMPTLB2 49
#define TEMPTLB3 50

#define DYN_TLB_START 51
#define DYN_TLB_END 57

#define PERM_TLB_START 58
#define PERM_TLB_END 62

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

/* The xlate function(s) turn virtual addresses into physical page
 * numbers, not physical addresses.  If attr->PTE_VALID is not set, then
 * the return value will indicate a page size mask that should be skipped
 * when scanning for a valid mapping (0 if there's a valid level 2 page
 * directory entry, 0x3ff if there is not).
 *
 * vptbl deals expects virtual page numbers in page directory entries.
 * levels should be PTE_VIRT_LEVELS for virtual->phys mappings, and
 * PTE_PHYS_LEVELS for guest phys->phys and reverse mappings.
 */

unsigned long vptbl_xlate(pte_t *tbl, unsigned long epn,
                          unsigned long *attr, int levels);

/* attr->PTE_SIZE should be 0; vptbl_map will use the largest
  page sizes it can. */
void vptbl_map(pte_t *tbl, unsigned long epn, unsigned long rpn,
               unsigned long npages, unsigned long attr, int levels);


void guest_set_tlb1(unsigned int entry, unsigned long mas1,
                    unsigned long epn, unsigned long grpn,
                    unsigned long mas2flags, unsigned long mas3flags);
unsigned int guest_tlb1_to_gtlb1(unsigned int idx);
int guest_find_tlb1(unsigned int entry, unsigned long mas1, unsigned long epn);

#define INV_IPROT 1
#define INV_TLB0  MMUCSR_L2TLB0_FI
#define INV_TLB1  MMUCSR_L2TLB1_FI

void guest_inv_tlb(register_t ivax, int pid, int flags);

int guest_set_tlb0(register_t mas0, register_t mas1, register_t mas2,
                   register_t mas3flags, unsigned long rpn, register_t mas8,
                   register_t guest_mas3flags);

void guest_reset_tlb(void);
void tlbsync(void);

#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_SIZE            TLB_TSIZE_16M

void tlb1_init(void);

/** Permanent 16MiB chunk of valloc space for temporary local mappings */
extern void *temp_mapping[2];

void *map_gphys(int tlbentry, pte_t *tbl, phys_addr_t addr,
                void *vpage, size_t *len, int maxtsize, int write);
void *map_phys(int tlbentry, phys_addr_t paddr, void *vpage,
               size_t *len, register_t mas2flags);

size_t copy_to_gphys(pte_t *tbl, phys_addr_t dest, void *src, size_t len);
size_t zero_to_gphys(pte_t *tbl, phys_addr_t dest, size_t len);
size_t copy_from_gphys(pte_t *tbl, void *dest, phys_addr_t src, size_t len);
size_t copy_between_gphys(pte_t *dtbl, phys_addr_t dest,
                           pte_t *stbl, phys_addr_t src, size_t len);

size_t copy_from_phys(void *dest, phys_addr_t src, size_t len);

size_t copy_phys_to_gphys(pte_t *dtbl, phys_addr_t dest,
                          phys_addr_t src, size_t len);

#define TLB_MISS_HANDLED 0
#define TLB_MISS_REFLECT 1
#define TLB_MISS_MCHECK  2

int guest_tlb1_miss(register_t vaddr, int space, int pid);
int guest_tlb_isi(register_t vaddr, int space, int pid);

struct trapframe;
struct gcpu;

void save_mas(struct gcpu *gcpu);
void restore_mas(struct gcpu *gcpu);

#define MAP_PIN    1
#define MAP_GLOBAL 2

void *map(phys_addr_t paddr, size_t len, int mas2flags, int mas3flags, int pin);
int handle_hv_tlb_miss(struct trapframe *regs, uintptr_t vaddr);

#endif
