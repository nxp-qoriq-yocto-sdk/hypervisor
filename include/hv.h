
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

#ifndef HV_H
#define HV_H

#include <libos/libos.h>

#define FH_API_VERSION 1
#define FH_API_COMPAT_VERSION 1

#define MAX_CORES 8

struct guest;

int start_guest(struct guest *guest);
int stop_guest(struct guest *guest);
int restart_guest(struct guest *guest);
phys_addr_t find_lowest_guest_phys(void *fdt);

char *stripspace(const char *str);
char *nextword(char **str);
uint64_t get_number64(queue_t *out, const char *numstr);
int64_t get_snumber64(queue_t *out, const char *numstr);
uint32_t get_number32(queue_t *out, const char *numstr);
int32_t get_snumber32(queue_t *out, const char *numstr);
int vcpu_to_cpu(const uint32_t *cpulist, unsigned int len, int vcpu);

void branch_to_reloc(void *bigmap_text_base,
                     register_t mas3, register_t mas7);

#endif
