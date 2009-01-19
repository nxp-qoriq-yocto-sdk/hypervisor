/** @file
 * Threads and scheduling
 */
/*
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#ifndef THREAD_H
#define THREAD_H

#include <libos/percpu.h>
#include <libos/thread.h>
#include <libos/list.h>
#include <libos/io.h>

#define NUM_PRIOS 2

struct sched;

enum {
	sched_running,
	sched_prep_block,
	sched_blocked,
};

typedef struct thread {	
	libos_thread_t libos_thread; 
	list_t rq_node;
	struct sched *sched;
	int prio, state;
} thread_t;

typedef struct sched {
	cpu_t *sched_cpu;
	list_t rq[NUM_PRIOS];
	thread_t idle;
	uint32_t lock;
} sched_t;

static inline int is_idle(void)
{
	thread_t *thread = to_container(cpu->thread, thread_t, libos_thread);
	return thread == &thread->sched->idle;
}

static inline void prepare_to_block(void)
{
	thread_t *thread = to_container(cpu->thread, thread_t, libos_thread);
	
	assert(!is_idle());
	thread->state = sched_prep_block;
	smp_sync();
}

void block(void);
void unblock(thread_t *thread);

thread_t *new_thread(void (*func)(trapframe_t *regs, void *arg),
                     void *arg, int prio);
void new_thread_inplace(thread_t *thread, uint8_t *stack,
                        void (*func)(trapframe_t *regs, void *arg),
                        void *arg, int prio);
 
void sched_core_init(cpu_t *sched_cpu);
void sched_init(void);

#endif
