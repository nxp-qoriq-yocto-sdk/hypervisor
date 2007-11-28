
#include "uv.h"
#include "percpu.h"

hcpu_t hcpu0;

extern void tlb1_init(void);

static void  core_init(void);

void init(unsigned long devtree_ptr)
{

    core_init();

    printf("Simple Guest 0.1\n");

}


static void core_init(void)
{

    /* set up a TLB entry for CCSR space */
    tlb1_init();

}


