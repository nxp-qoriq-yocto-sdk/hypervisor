/**
 * @file
 *
 *
 */
/*
 * Copyright (C) 2007-2009 Freescale Semiconductor, Inc.
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

#ifndef DOORBELL_H
#define DOORBELL_H

#include <libos/percpu.h>
#include <libos/core-regs.h>

#define MSG_DBELL      0x00000000
#define MSG_DBELL_CRIT 0x08000000
#define MSG_GBELL      0x10000000
#define MSG_GBELL_CRIT 0x18000000
#define MSG_GBELL_MCHK 0x20000000

static inline void send_doorbell_msg(unsigned long msg)
{
	/* msgsnd is ordered as a store relative to sync instructions,
	 * but not as a cacheable store, so we need a full sync
	 * to order with any previous stores that the doorbell handler
	 * needs to see.
	 */
	asm volatile("msync; msgsnd %0" : : "r" (msg) : "memory");
}

/** Send local guest doorbell.
 *
 *  Sends a guest doorbell to the current
 *  cpu.
 */
static inline void send_local_guest_doorbell(void)
{
	send_doorbell_msg(MSG_GBELL | (get_gcpu()->guest->lpid << 14) |
	                  mfspr(SPR_GPIR));
}

/** Send local critical guest doorbell.
 *
 *  Sends a guest critical doorbell to the current cpu.
 */
static inline void send_local_crit_guest_doorbell(void)
{
	send_doorbell_msg(MSG_GBELL_CRIT | (get_gcpu()->guest->lpid << 14) |
	                  mfspr(SPR_GPIR));
}

/** Send critical doorbell.
 *
 *  Always for hypervisor internal use only so
 *  the lpid is always 0.
 */
static inline void send_crit_doorbell(int cpunum)
{
	send_doorbell_msg(MSG_DBELL_CRIT | cpunum);
}

/** Send doorbell.
 *
 *  Always for hypervisor internal use only so
 *  the lpid is always 0.
 */
static inline void send_doorbell(int cpunum)
{
	send_doorbell_msg(MSG_DBELL | cpunum);
}

#endif
