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

#ifndef _VMPIC_H_
#define _VMPIC_H_

#include <libos/trapframe.h>
#include <percpu.h>
#include <handle.h>

struct dev_owner;

/*
 * generic handle to represent all interrupt (hwint,vint) types
 */

typedef struct vmpic_interrupt {
	struct interrupt *irq;
	guest_t *guest;

	handle_t user;
	int handle;

	int config;

#ifdef CONFIG_CLAIMABLE_DEVICES
	claim_action_t claim_action;
	int claimed;
#endif
} vmpic_interrupt_t;

vmpic_interrupt_t *vmpic_alloc_handle(guest_t *guest, interrupt_t *irq,
                                      int config, int standby);
int vmpic_alloc_mpic_handle(struct dev_owner *owner, interrupt_t *irq, int standby);
void vmpic_global_init(void);
void vmpic_partition_init(guest_t *guest);
void hcall_int_set_config(trapframe_t *regs);
void hcall_int_get_config(trapframe_t *regs);
void hcall_int_set_mask(trapframe_t *regs);
void hcall_int_eoi(trapframe_t *regs);
void hcall_int_iack(trapframe_t *regs);
void hcall_int_get_mask(trapframe_t *regs);
void hcall_int_get_activity(trapframe_t *regs);
void hcall_vmpic_get_msir(trapframe_t *regs);

#ifdef CONFIG_CLAIMABLE_DEVICES
static inline void vmpic_set_claimed(vmpic_interrupt_t *vmpic, int claimed)
{
	vmpic->claimed = claimed;
}

static inline int vmpic_is_claimed(vmpic_interrupt_t *vmpic)
{
	return vmpic->claimed;
}
#else
static inline void vmpic_set_claimed(vmpic_interrupt_t *vmpic, int claimed)
{
}

static inline int vmpic_is_claimed(vmpic_interrupt_t *vmpic)
{
	return 1;
}
#endif

#endif
