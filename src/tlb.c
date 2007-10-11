
#include "tlb.h"
#include "spr.h"

static void
tlb1_write_entry(unsigned int idx, uint32_t mas1,
                 uint32_t mas2, uint32_t mas3, uint32_t mas7);
static unsigned int
size2tsize(uint32_t size);


/*
 *    after tlb1_init:
 *        TLB1[0] = CCSR
 *        TLB1[1] = hv image 16M
 *
 *
 */

#define CCSRBAR_PA              0xffe00000
#define CCSRBAR_VA              0xf0000000
#define CCSRBAR_SIZE            0x00100000

#if 0
        /* Mapin CCSRBAR in TLB1[0] */
        __tlb1_set_entry(0, CCSRBAR_VA, ccsrbar, CCSRBAR_SIZE,
            _TLB_ENTRY_IO, KERNEL_TID, 0);
#endif


void
tlb1_init(void)
{
        uint32_t mas1, mas2, mas3, mas7;
        uint32_t tsize,tid,ts;

        /* Convert size to TSIZE */
        tsize = size2tsize(CCSRBAR_SIZE);
        tid = (UV_TID <<  MAS1_TID_SHIFT) & MAS1_TID_MASK;
        ts = 0;
        mas1 = MAS1_VALID | MAS1_IPROT | ts | tid;
        mas1 |= ((tsize << MAS1_TSIZE_SHIFT) & MAS1_TSIZE_MASK);

        mas2 = (CCSRBAR_VA & MAS2_EPN) | _TLB_ENTRY_IO;

        /* Set supervisor rwx permission bits */
        mas3 = (CCSRBAR_PA & MAS3_RPN) | MAS3_SR | MAS3_SW | MAS3_SX;

        mas7 = 0;

        tlb1_write_entry(0, mas1, mas2, mas3, mas7);

        __asm __volatile("mr 0,0");

#if 0
        uint32_t mas0;

        /* TBL1[1] is used to map the kernel. Save that entry. */
        mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(1);
        mtspr(SPR_MAS0, mas0);
        __asm __volatile("isync; tlbre");

        tlb1[1].mas1 = mfspr(SPR_MAS1);
        tlb1[1].mas2 = mfspr(SPR_MAS2);
        tlb1[1].mas3 = mfspr(SPR_MAS3);

        /* Mapin CCSRBAR in TLB1[0] */
        __tlb1_set_entry(0, CCSRBAR_VA, ccsrbar, CCSRBAR_SIZE,
            _TLB_ENTRY_IO, KERNEL_TID, 0);

        /* Setup TLB miss defaults */
        set_mas4_defaults();

        /* Reset next available TLB1 entry index. */
        tlb1_idx = 2;
#endif

}


static void
tlb1_write_entry(unsigned int idx, uint32_t mas1,
                 uint32_t mas2, uint32_t mas3, uint32_t mas7)
{
        uint32_t mas0;

        //debugf("tlb1_write_entry: s\n");

        /* Select entry */
        mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(idx);
        //debugf("tlb1_write_entry: mas0 = 0x%08x\n", mas0);

        mtspr(SPR_MAS0, mas0);
        __asm volatile("isync");
        mtspr(SPR_MAS1, mas1);
        __asm volatile("isync");
        mtspr(SPR_MAS2, mas2);
        __asm volatile("isync");
        mtspr(SPR_MAS3, mas3);
        __asm volatile("isync");
        mtspr(SPR_MAS7, mas7);
        __asm volatile("isync; tlbwe; isync; msync");

        //debugf("tlb1_write_entry: e\n");;
}

/*
 * Return the largest uint value log such that 2^log <= num.
 */
static unsigned int
ilog2(unsigned int num)
{
        int lz;

        __asm ("cntlzw %0, %1" : "=r" (lz) : "r" (num));
        return (31 - lz);
}


/*
 * Convert TLB TSIZE value to mapped region size.
 */
static uint32_t
tsize2size(unsigned int tsize)
{
        /*
         * size = 4^tsize KB
         * size = 4^tsize * 2^10 = 2^(2 * tsize - 10)
         */

        return (1 << (2 * tsize)) * 1024;
}


/*
 * Convert region size (must be power of 4) to TLB TSIZE value.
 */
static unsigned int
size2tsize(uint32_t size)
{
        /*
         * tsize = log2(size) / 2 - 5
         */

        return (ilog2(size) / 2 - 5);
}

