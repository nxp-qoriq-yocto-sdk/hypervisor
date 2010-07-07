/** @file
 * Platform error logging
 */

/*
 * Copyright (C) 2009, 2010 Freescale Semiconductor, Inc.
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


#include <libos/alloc.h>
#include <libos/platform_error.h>
#include <libos/errors.h>

#include <percpu.h>
#include <events.h>
#include <error_log.h>

void error_log_init(queue_t *q)
{
	int ret;

	ret =  queue_init(q, 32768);
	if (ret)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s error event queue init failed error = %d\n",
			(q == &hv_global_event_queue) ? "HV global" :
			((q == &global_event_queue) ? "Global" : "Guest"),
			ret);
}

int error_get(queue_t *q, hv_error_t *err, unsigned long *flag,
		 unsigned long mask, int peek)
{
	int ret;

	ret = queue_read(q, (uint8_t *)err, sizeof(hv_error_t), peek);

	if (flag) {
		atomic_and(flag, ~mask);
		if (!queue_empty(q))
			atomic_or(flag, mask);
	}

	return ret ? 0 : EV_ENOENT;
}

void error_log(queue_t *q, hv_error_t *err, uint32_t *lock)
{
	if (!q || !q->size)
		return;

	spin_lock(lock);

	/* Only write whole errors into the queue. */
	if (queue_get_space(q) < sizeof(hv_error_t)) {
		/* If the queue is full, we drop the errors.
		 * FIXME: should do more here than a printlog
		 */
		spin_unlock(lock);
		printlog(LOGTYPE_MISC, LOGLEVEL_DEBUG,
		         "Error event queue full, dropping errors\n");
		return;
	}

	queue_write(q, (const uint8_t *)err, sizeof(hv_error_t));
	spin_unlock(lock);

	if (q == &hv_global_event_queue) {
		setevent(get_gcpu(), EV_DUMP_HV_QUEUE);
	} else if (q == &global_event_queue) {
		if (error_manager_guest) {
			gcpu_t *vcpu = error_manager_guest->err_destcpu;
			atomic_or(&vcpu->crit_gdbell_pending,
				  GCPU_PEND_CRIT_INT);
			setevent(vcpu, EV_GUEST_CRIT_INT);
		}
	} else {
		guest_t *guest = to_container(q, guest_t, error_event_queue);
		gcpu_t *gcpu = guest->gcpus[0];

		atomic_or(&gcpu->mcsr, MCSR_MCP);
		setevent(gcpu, EV_MCP);
		wake_hcall_nap(gcpu);
	}
}
