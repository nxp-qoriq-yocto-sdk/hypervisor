
#include "frame.h"

void
trap(trapframe_t *frameptr)
{

    frameptr->srr0 += 4;

}
