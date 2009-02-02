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

/* To block waiting for a condition, a task enters the sched_prep_block
 * state (via prepare_to_block()), then tests the condition.  If the
 * condition is still false, it calls block(), which will remove
 * the thread from the runqueue and set the state to sched_blocked --
 * unless unblock() has been called on the thread in the meantime.
 *
 * Any code that alters the condition such that the the task should
 * no longer block must call unblock() *after* setting the condition.
 */
enum {
	sched_running,
	sched_prep_block,
	sched_blocked,
};

typedef struct thread {	
	libos_thread_t libos_thread; 
	list_t rq_node; /* Run queue node */
	struct sched *sched; /* Scheduler of CPU to which this thread is bound */
	int prio, state;
} thread_t;

typedef struct sched {
	cpu_t *sched_cpu; /* CPU which this scheduler controls */
	list_t rq[NUM_PRIOS]; /* FIFO run queue per priority */
	thread_t idle;
	uint32_t lock;
} sched_t;

static inline int is_idle(void)
{
	thread_t *thread = to_container(cpu->thread, thread_t, libos_thread);
	return thread == &thread->sched->idle;
}

thread_t *new_thread(void (*func)(trapframe_t *regs, void *arg),
                     void *arg, int prio);
void new_thread_inplace(thread_t *thread, uint8_t *stack,
                        void (*func)(trapframe_t *regs, void *arg),
                        void *arg, int prio);
 
void sched_core_init(cpu_t *sched_cpu);
void sched_init(void);

void unblock(thread_t *thread);
void schedule(trapframe_t *regs);

#endif
