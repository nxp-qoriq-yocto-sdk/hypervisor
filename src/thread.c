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

#include <libos/trapframe.h>
#include <libos/trap_booke.h>
#include <libos/alloc.h>

#include <thread.h>
#include <hv.h>
#include <events.h>

static sched_t scheds[MAX_CORES];

static void do_schedule(sched_t *sched)
{
	libos_thread_t *thread = &sched->idle.libos_thread;

	assert(cpu->traplevel == 0);

	for (int i = 0; i < NUM_PRIOS; i++)
		if (!list_empty(&sched->rq[i]))
			thread = &to_container(sched->rq[i].next,
			                       thread_t, rq_node)->libos_thread;

	spin_unlock(&sched->lock);

	switch_thread(thread);
}

void schedule(trapframe_t *regs)
{
	sched_t *sched = &scheds[cpu->coreid];

	register_t saved = spin_lock_intsave(&sched->lock);
	do_schedule(sched);
	restore_int(saved);
}

void prepare_to_block(void)
{
	thread_t *thread = to_container(cpu->thread, thread_t, libos_thread);

	assert(!is_idle());
	thread->state = sched_prep_block;
	smp_sync();
}

void block(void)
{
	thread_t *thread = to_container(cpu->thread, thread_t, libos_thread);
	sched_t *sched = thread->sched;
	register_t saved;

	assert(!is_idle());

	saved = spin_lock_intsave(&sched->lock);

	if (thread->state == sched_prep_block) {
		thread->state = sched_blocked;
		list_del(&thread->rq_node);
		do_schedule(sched);
		restore_int(saved);
		return;
	}

	spin_unlock_intsave(&sched->lock, saved);
}

void unblock(thread_t *thread)
{
	sched_t *sched = thread->sched;
	register_t saved;

	smp_sync();

	saved = spin_lock_intsave(&sched->lock);

	if (thread->state == sched_blocked) {
		list_add(&sched->rq[thread->prio], &thread->rq_node);
		setevent(sched->sched_cpu->client.gcpu, EV_RESCHED);
	}

	thread->state = sched_running;
	spin_unlock_intsave(&sched->lock, saved);
}

void libos_unblock(libos_thread_t *lthread)
{
	thread_t *thread = to_container(lthread, thread_t, libos_thread);
	unblock(thread);
}

void new_thread_inplace(thread_t *thread, uint8_t *kstack,
                        void (*func)(trapframe_t *regs, void *arg),
                        void *arg, int prio)
{
	thread->libos_thread.kstack = &kstack[KSTACK_SIZE - FRAMELEN];
	trapframe_t *regs = (trapframe_t *)thread->libos_thread.kstack;

	thread->libos_thread.pc = ret_from_exception;
	thread->libos_thread.stack = regs;

	thread->sched = &scheds[cpu->coreid];
	thread->state = sched_blocked;
	thread->prio = prio;

	regs->gpregs[1] = (register_t)regs;
	regs->gpregs[2] = (register_t)cpu;
	regs->gpregs[3] = (register_t)regs;
	regs->gpregs[4] = (register_t)arg;

	regs->srr0 = (register_t)func;
	regs->srr1 = MSR_ME | MSR_CE | MSR_EE;

	regs->lr = (register_t)ret_from_exception;
}

thread_t *new_thread(void (*func)(trapframe_t *regs, void *arg),
                     void *arg, int prio)
{
	thread_t *thread = malloc(sizeof(thread_t));
	if (!thread)
		return NULL;

	uint8_t *stack = alloc_type(kstack_t);
	if (!stack) {
		free(thread);
		return NULL;
	}

	new_thread_inplace(thread, stack, func, arg, prio);
	return thread;
}

void sched_core_init(cpu_t *sched_cpu)
{
	sched_t *sched = &scheds[sched_cpu->coreid];

	sched->sched_cpu = sched_cpu;
	sched->idle.sched = sched;
	sched->idle.libos_thread.kstack = sched_cpu->kstack;

	for (int i = 0; i < NUM_PRIOS; i++)
		list_init(&sched->rq[i]);

	sched_cpu->thread = &sched->idle.libos_thread;
}

void sched_init(void)
{
}
