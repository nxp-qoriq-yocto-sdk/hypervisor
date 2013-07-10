
/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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


#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#ifdef CONFIG_LIBOS_64BIT
#define PHYSBASE 0x420000000
#define KSTACK_SIZE 2048*4
#else
#define KSTACK_SIZE 2048
#define PHYSBASE 0x20000000
#endif
#define BASE_TLB_ENTRY 15

#ifndef _ASM
typedef int client_cpu_t;

/* These are used by assembly code */
struct trapframe;

void dec_handler(struct trapframe *regs);
void ext_int_handler(struct trapframe *regs);
void crit_int_handler(struct trapframe *regs);
void mcheck_interrupt(struct trapframe *frameptr);
void debug_handler(struct trapframe *regs);
void fit_handler(struct trapframe *regs);
void ext_doorbell_handler(struct trapframe *regs);
void ext_critical_doorbell_handler(struct trapframe *regs);
void dtlb_handler(struct trapframe *regs);
void itlb_handler(struct trapframe *regs);
void watchdog_handler(struct trapframe *regs);
void program_handler(struct trapframe *regs);
void dsi_handler(struct trapframe *regs);
void perfmon_handler(struct trapframe *regs);
#endif

#define EXC_DECR_HANDLER dec_handler
#define EXC_EXT_INT_HANDLER ext_int_handler
#define EXC_CRIT_INT_HANDLER crit_int_handler
#define EXC_MCHECK_HANDLER mcheck_interrupt
#define EXC_DEBUG_HANDLER debug_handler
#define EXC_FIT_HANDLER fit_handler
#define EXC_DOORBELL_HANDLER ext_doorbell_handler
#define EXC_DOORBELLC_HANDLER ext_critical_doorbell_handler
#define EXC_DTLB_HANDLER dtlb_handler
#define EXC_ITLB_HANDLER itlb_handler
#define EXC_WDOG_HANDLER watchdog_handler
#define EXC_PROGRAM_HANDLER program_handler
#define EXC_DSI_HANDLER dsi_handler
#define EXC_PERFMON_HANDLER perfmon_handler

#endif
