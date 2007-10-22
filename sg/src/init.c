
#include "uv.h"

extern void tlb1_init(void);

static void  core_init(void);

void uart_putc(uint8_t);

void init(unsigned long devtree_ptr)
{

    core_init();

    printh("Simple Guest 0.1\n");

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}


