
#include "frame.h"

extern void trap(trapframe_t *);


void critical_interrupt(trapframe_t *frameptr)
{
#if 0
        printf("powerpc_mchk_interrupt: machine check interrupt!\n");
        dump_frame(framep);
#endif
}

void powerpc_mchk_interrupt(trapframe_t *frameptr)
{
#if 0
        printf("powerpc_mchk_interrupt: machine check interrupt!\n");
        dump_frame(framep);
#endif
}

