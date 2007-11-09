
#include "frame.h"

void
trap(trapframe_t *frameptr)
{

    frameptr->srr0 += 4;

    __asm__ volatile("mr 22, 22");

}
