/*-
 * Copyright (c) 1982, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)genassym.c	5.11 (Berkeley) 5/10/91
 * $FreeBSD: src/sys/powerpc/powerpc/genassym.c,v 1.58 2006/11/30 04:17:05 jb Exp $
 */

#include <stddef.h>
#include "assym.h"
#include "vcpu.h"
#include "frame.h"

ASSYM(CPUSAVE_R28, CPUSAVE_R28*4);
ASSYM(CPUSAVE_R29, CPUSAVE_R29*4);
ASSYM(CPUSAVE_R30, CPUSAVE_R30*4);
ASSYM(CPUSAVE_R31, CPUSAVE_R31*4);
ASSYM(CPUSAVE_DEAR, CPUSAVE_DEAR*4);
ASSYM(CPUSAVE_ESR, CPUSAVE_ESR*4);
ASSYM(CPUSAVE_SRR0, CPUSAVE_SRR0*4);
ASSYM(CPUSAVE_SRR1, CPUSAVE_SRR1*4);

ASSYM(VCPU_SIZE, sizeof(vcpu_t));
ASSYM(VCPU_NORMSAVE, offsetof(vcpu_t, normsave));
ASSYM(VCPU_CRITSAVE, offsetof(vcpu_t, critsave));
ASSYM(VCPU_MACHKSAVE, offsetof(vcpu_t, machksave));
ASSYM(VCPU_DBGSAVE, offsetof(vcpu_t, dbgsave));
ASSYM(UVSTACK, offsetof(vcpu_t, uvstack));

ASSYM(FRAMELEN, FRAMELEN);
ASSYM(FRAME_0, offsetof(trapframe_t, fixreg[0]));
ASSYM(FRAME_1, offsetof(trapframe_t, fixreg[1]));
ASSYM(FRAME_2, offsetof(trapframe_t, fixreg[2]));
ASSYM(FRAME_3, offsetof(trapframe_t, fixreg[3]));
ASSYM(FRAME_LR, offsetof(trapframe_t, lr));
ASSYM(FRAME_CR, offsetof(trapframe_t, cr));
ASSYM(FRAME_DEAR, offsetof(trapframe_t, dear));
ASSYM(FRAME_ESR, offsetof(trapframe_t, esr));
ASSYM(FRAME_CTR, offsetof(trapframe_t, ctr));
ASSYM(FRAME_XER, offsetof(trapframe_t, xer));
ASSYM(FRAME_SRR0, offsetof(trapframe_t, srr0));
ASSYM(FRAME_SRR1, offsetof(trapframe_t, srr1));
ASSYM(FRAME_EXC, offsetof(trapframe_t, exc));

