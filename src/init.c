
#include "uv.h"

extern void tlb1_init(void);

static void  core_init(void);

void uart_putc(uint8_t);


void init(unsigned long devtree_ptr)
{

    core_init();

    printh("=======================================\n");
    printh("Freescale Ultravisor 0.1\n");

#if 0

    if (first instance) {
        platform_hw_init();
        uv_global_init();
    }

    uv_instance_init();

    guest_init();

    run_guest();

    /* run_guest() never returns */

#endif

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}


