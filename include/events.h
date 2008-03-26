
#ifndef _EVENTS
#define _EVENTS

#include <percpu.h>


typedef void (*eventfp_t)(trapframe_t *regs);

void setevent(gcpu_t *gcpu, int event);

#define EV_ASSERT_VINT 0
#define EV_GDB 1

#endif 
