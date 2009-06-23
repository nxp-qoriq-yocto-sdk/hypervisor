/** @file
 * Platform error logging
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


#include <libos/alloc.h>
#include <libos/platform_error.h>
#include <libos/errors.h>

#include <percpu.h>
#include <pamu.h>
#include <events.h>
#include <error_log.h>

void error_log_init(queue_t *q)
{
	int ret;

	ret =  queue_init(q, sizeof(error_info_t) * MAX_ERROR_EVENTS);

	if (ret)
		printlog(LOGTYPE_PARTITION, LOGLEVEL_ERROR,
			"%s error event queue init failed error = %d\n",
			(q == &global_event_queue) ? "Global" : "Guest",
			ret);
}

int error_get(queue_t *q, error_info_t *err, unsigned long *flag,
		 unsigned long mask)
{
	int ret;

	ret = queue_read(q, (uint8_t *)err, sizeof(error_info_t), 0);

	if (flag) {
		atomic_and(flag, ~mask);
		if (!queue_empty(q))
			atomic_or(flag, mask);
	}

	return ret ? 0 : ENOENT;
}

void error_log(queue_t *q, error_info_t *err, uint32_t *lock)
{
	int ret;

	spin_lock(lock);
	ret = queue_write(q, (const uint8_t *)err, sizeof(error_info_t));
	/* if queue is full we drop the errors */
	/* FIXME: should do more here than a printlog */
	if (!ret) {
		spin_unlock(lock);
		printlog(LOGTYPE_MISC, LOGLEVEL_DEBUG,
				"Error event queue full, dropping errors\n");
		return;
	}
	spin_unlock(lock);

	if (q == &global_event_queue) {
		atomic_or(&error_manager_guest->gcpus[error_manager_gcpu]->crit_gdbell_pending,
				 GCPU_PEND_CRIT_INT);
		setevent(error_manager_guest->gcpus[error_manager_gcpu], EV_GUEST_CRIT_INT);
	} else {
		guest_t *guest = to_container(q, guest_t, error_event_queue);
		atomic_or(&guest->gcpus[0]->mcsr, MCSR_MCP);
		setevent(guest->gcpus[0], EV_MCP);
	}

}
