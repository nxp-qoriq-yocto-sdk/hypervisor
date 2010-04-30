/** @file
 * Threads and scheduling
 */

/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

#ifdef CONFIG_64BIT
extern unsigned long toc_start;
#endif

static void do_schedule(sched_t *sched)
{
	libos_thread_t *thread = &sched->idle.libos_thread;

	assert(cpu->traplevel <= TRAPLEVEL_NORMAL);

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
	thread_t *thread = cur_thread();

	assert(!is_idle());
	thread->state = sched_prep_block;
	smp_sync();
}

void block(void)
{
	thread_t *thread = cur_thread();
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
	unblock(thread_from_libos(lthread));
}

void new_thread_inplace(thread_t *thread, uint8_t *kstack,
                        void (*func)(trapframe_t *regs, void *arg),
                        void *arg, int prio)
{
	memset(thread, 0, sizeof(thread_t));

	thread->libos_thread.kstack = &kstack[KSTACK_SIZE - FRAMELEN];
	trapframe_t *regs = (trapframe_t *)thread->libos_thread.kstack;

	memset(regs, 0, sizeof(trapframe_t));

	thread->libos_thread.pc = ret_from_exception;
	thread->libos_thread.stack = regs;

	thread->sched = &scheds[cpu->coreid];
	thread->state = sched_blocked;
	thread->prio = prio;

	regs->gpregs[1] = (register_t)regs;
#ifndef CONFIG_64BIT
	regs->gpregs[2] = (register_t)cpu;
#else
	regs->gpregs[2] = (register_t)(&toc_start) + 0x8000UL;
	regs->gpregs[13] = (register_t)cpu;
#endif
	regs->gpregs[3] = (register_t)regs;
	regs->gpregs[4] = (register_t)arg;

#ifndef CONFIG_64BIT
	regs->srr0 = (register_t)func;
	regs->srr1 = MSR_ME | MSR_CE | MSR_EE | MSR_RI;
#else
	/* NOTE:
	 * function pointers are implemented as function descriptors on 64-bit
	 * As HV is using a single TOC and we don't need to save and restore
	 * pTOC, hence de-reference the function address from the descriptor.
	 * If HV ever uses multiple TOCs, support needs to be added here
	 */
	regs->srr0 = *(register_t *)func;
	regs->srr1 = MSR_CM | MSR_ME | MSR_CE | MSR_EE | MSR_RI;
#endif

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
