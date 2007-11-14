/*-
 * Copyright (C) 2006 Semihalf, Rafal Jaworowski <raj@semihalf.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

/* IVORs for exeception handling */

#ifndef TRAP_BOOKE_H
#define TRAP_BOOKE_H

#define	EXC_CRIT	0    /* critical input */
#define	EXC_MCHK	1    /* machine check, error report */
#define	EXC_DSI		2    /* data storage */
#define	EXC_ISI		3    /* instruction storage */
#define	EXC_EXI		4    /* external input */
#define	EXC_ALI		5    /* alignment */
#define	EXC_PGM		6    /* program-- illegal, privileged, trap */
#define EXC_FPUNAVAIL   7    /* fpu unavailable */
#define	EXC_SC		8    /* system call */
#define	EXC_DECR	10   /* decrementer */
#define	EXC_FIT		11   /* fixed interval */
#define	EXC_WDOG	12   /* watchdog */
#define	EXC_DTLB	13   /* data tlb error */
#define	EXC_ITLB	14   /* instruction tlb error */
#define	EXC_DEBUG	15   /* debug */
#define	EXC_PERF	35   /* performance monitor */
#define	EXC_DOORBELL	36   /* doorbell */
#define	EXC_DOORBELLC	37   /* doorbell critical */
#define	EXC_GDOORBELL	38   /* guest doorbell */
#define	EXC_GDOORBELLC	39   /* guest doorbell critical, machine check */
#define	EXC_HCALL	40   /* hcall */
#define	EXC_EHPRIV	41   /* hypervisor privilege trap */

#define	EXC_LAST	255

#endif
