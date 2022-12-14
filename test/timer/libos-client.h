
/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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

#define PHYSBASE 0x20000000
#define BASE_TLB_ENTRY 15
#define KSTACK_SIZE 8192

#ifndef _ASM
typedef int client_cpu_t;
#endif

#define EXC_FIT_HANDLER fit_handler

#define CONFIG_LIBOS_MAX_BUILD_LOGLEVEL LOGLEVEL_NORMAL
#define CONFIG_LIBOS_DEFAULT_LOGLEVEL LOGLEVEL_NORMAL
#define CONFIG_LIBOS_INIT 1
#define CONFIG_LIBOS_MP 1
#define CONFIG_LIBOS_NS16550 1
#define CONFIG_LIBOS_EXCEPTION 1
#define CONFIG_LIBOS_SIMPLE_ALLOC 1
#define CONFIG_LIBOS_PHYS_64BIT 1
#define CONFIG_LIBOS_FSL_BOOKE_TLB 1
#define CONFIG_LIBOS_ALLOC_IMPL 1
#define CONFIG_LIBOS_VIRT_ALLOC 1
#define CONFIG_LIBOS_CONSOLE 1
#define CONFIG_LIBOS_LIBC 1

#endif
