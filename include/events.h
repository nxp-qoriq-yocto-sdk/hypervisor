
/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _EVENTS
#define _EVENTS

#include <percpu.h>

extern queue_t pend_virq;

typedef void (*eventfp_t)(trapframe_t *regs);

void setevent(gcpu_t *gcpu, int event);
void setgevent(gcpu_t *gcpu, int event);

void tlbivax_ipi(trapframe_t *regs);

void do_stop_core(trapframe_t *regs, int restart);
void stop_core(trapframe_t *regs);
void start_core(trapframe_t *regs);
void restart_core(trapframe_t *regs);
void load_guest(trapframe_t *regs);
void start_load_guest(trapframe_t *regs);
void wait_for_gevent(trapframe_t *regs);
int register_gevent(eventfp_t handler);
void init_gevents(void);
void idle_loop(void);
void pause_core(trapframe_t *regs);
void resume_core(trapframe_t *regs);
void deliver_nmi(trapframe_t *regs);
void reflect_mcp_mcheck(trapframe_t *regs);
void dbell_to_mcgdbell_glue(trapframe_t *regs);
void dbell_to_cgdbell_glue(trapframe_t *regs);
void halt_core(trapframe_t *regs);
void dump_hv_queue(trapframe_t *regs);
void deliver_pend_vint(trapframe_t *regs);
void sync_nap(trapframe_t *regs);

#define EV_ASSERT_VINT            0
#define EV_TLBIVAX                1
#define EV_RESCHED                2
#define EV_MCP                    3
#define EV_GUEST_CRIT_INT         4
#define EV_DUMP_HV_QUEUE          5
#define EV_DELIVER_PEND_VINT        6
#define EV_SYNC_NAP               7 /* must run on boot core */

extern int gev_stop; /**< Stop guest on this core */
extern int gev_start; /**< Start guest on this core */
/*** gev_stop, plus send gev_start_load to primary. */
extern int gev_restart;
/*** gev_start, but wait if no image; primary core only. */
/*** Load the guest's images, but don't start -- partition boot core only */
extern int gev_load;
/*** Load and start the guest -- partition boot core only */
extern int gev_start_load; 
/*** Pause the guest on this core */
extern int gev_pause;
/*** Unpause the guest on this core */
extern int gev_resume;
/*** Deliver a non-maskable interrupt on this gcpu */
extern int gev_nmi;

#endif 
