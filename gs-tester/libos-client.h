
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
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

#define PHYSBASE 0
#define BASE_TLB_ENTRY 15

#define HYPERVISOR

#ifndef _ASM
typedef int client_cpu_t;
#endif

#define EXC_CRIT_INT_HANDLER trap
#define EXC_MCHECK_HANDLER trap
#define EXC_DSI_HANDLER trap
#define EXC_ISI_HANDLER trap
#define EXC_EXT_INT_HANDLER trap
#define EXC_ALIGN_HANDLER trap
#define EXC_PROGRAM_HANDLER trap
#define EXC_FPUNAVAIL_HANDLER trap
#define EXC_SYSCALL_HANDLER trap
#define EXC_AUXUNAVAIL_HANDLER trap
#define EXC_DECR_HANDLER trap
#define EXC_FIT_HANDLER trap
#define EXC_WDOG_HANDLER trap
#define EXC_DTLB_HANDLER trap
#define EXC_ITLB_HANDLER trap
#define EXC_DEBUG_HANDLER trap
#define EXC_PERFMON_HANDLER trap
#define EXC_DOORBELL_HANDLER trap
#define EXC_DOORBELLC_HANDLER trap
#define EXC_GDOORBELL_HANDLER trap
#define EXC_GDOORBELLC_HANDLER trap
#define EXC_HCALL_HANDLER trap
#define EXC_EHPRIV_HANDLER trap

#endif
