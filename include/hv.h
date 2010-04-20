/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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
#include <libos/thread.h>
#include <libos/trapframe.h>

#define FH_API_VERSION 1
#define FH_API_COMPAT_VERSION 1

#define MAX_CORES 8

struct guest;
struct queue;

int start_guest(struct guest *guest, int load);
int stop_guest(struct guest *guest, const char *reason, const char *who);
int restart_guest(struct guest *guest, const char *reason, const char *who);
int pause_guest(struct guest *guest);
int resume_guest(struct guest *guest);
phys_addr_t find_lowest_guest_phys(void *fdt);

char *stripspace(const char *str);
char *nextword(char **str);
uint64_t get_number64(struct queue *out, const char *numstr);
int64_t get_snumber64(struct queue *out, const char *numstr);
uint32_t get_number32(struct queue *out, const char *numstr);
int32_t get_snumber32(struct queue *out, const char *numstr);
int vcpu_to_cpu(const uint32_t *cpulist, unsigned int len, unsigned int vcpu);
uint32_t *write_reg(uint32_t *reg, phys_addr_t start, phys_addr_t size);

void branch_to_reloc(void *bigmap_text_base,
                     register_t mas3, register_t mas7);

void reflect_trap(trapframe_t *regs);
void reflect_mcheck(trapframe_t *regs, register_t mcsr, uint64_t mcar);
void reflect_crit_int(trapframe_t *regs, int trap_type);
int reflect_errint(void *arg);

void set_hypervisor_strprop(struct guest *guest, const char *prop, const char *value);

phys_addr_t get_ccsr_phys_addr(size_t *ccsr_size);

__attribute__((noreturn)) void init_guest(void);

struct guest *handle_to_guest(int handle);
void hcall_get_core_state(trapframe_t *regs);
void hcall_enter_nap(trapframe_t *regs);
void hcall_exit_nap(trapframe_t *regs);

int flush_disable_l1_cache(void *disp_addr, uint32_t timeout);
int flush_disable_l2_cache(uint32_t timeout, int unlock, uint32_t *old_l2csr0);
int check_perfmon(trapframe_t *regs);

extern char *displacement_flush_area[MAX_CORES];

#define set_cache_reg(reg, val) do { \
	sync(); \
	isync(); \
	mtspr((reg), (val)); \
	isync(); \
} while (0)

void flush_caches(void);

#endif
