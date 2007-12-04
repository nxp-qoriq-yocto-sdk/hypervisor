
#include "os.h"
#include "pcpu.h"
#include "tlb.h"

hcpu_t hcpu0;

static void tlb1_init(void);

static void  core_init(void);

void init(unsigned long devtree_ptr)
{

    core_init();

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}

/*
 *    after tlb1_init:
 *        TLB1[0] = CCSR
 *        TLB1[1] = hv image 16M
 *
 *
 */

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0xf0000000
#define CCSRBAR_SIZE            0x01000000

static void tlb1_init(void)
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

//        __asm __volatile("mr 0,0");

}

