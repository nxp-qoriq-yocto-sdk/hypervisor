
#ifndef _EVENTS
#define _EVENTS

#include <percpu.h>


typedef void (*eventfp_t)(trapframe_t *regs);

void setevent(gcpu_t *gcpu, int event);
void setgevent(gcpu_t *gcpu, int event);

void stop_core(trapframe_t *regs);
void start_core(trapframe_t *regs);
void restart_core(trapframe_t *regs);
void start_wait_core(trapframe_t *regs);
void wait_for_gevent(trapframe_t *regs);
void wait_for_gevent_loop(void);

#define EV_ASSERT_VINT 0
#define EV_GDB 1

#define GEV_STOP       0 /**< Stop guest on this core */
#define GEV_START      1 /**< Start guest on this core */
/**< GEV_STOP, plus send GEV_START_WAIT to primary. */
#define GEV_RESTART    2 
/**< GEV_START, but wait if no image; primary core only. */
#define GEV_START_WAIT 3

#endif 
