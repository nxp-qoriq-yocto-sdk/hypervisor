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

#ifndef _VPIC_H
#define _VPIC_H

#include <libos/interrupts.h>
#include <libos/trapframe.h>
#include <stdint.h>

#define MAX_VINT_CNT 32

struct guest;

typedef struct vpic_interrupt {
	interrupt_t irq;
	struct guest *guest;
	uint32_t destcpu;
	uint8_t enable, irqnum, pending, active, config;
} vpic_interrupt_t;

typedef struct vpic {
	vpic_interrupt_t ints[MAX_VINT_CNT];
	int alloc_next;
	uint32_t lock;
} vpic_t;

typedef struct vpic_cpu {
	uint32_t active;
	uint32_t pending;
} vpic_cpu_t;

vpic_interrupt_t *vpic_alloc_irq(struct guest *guest, int config);
int vpic_alloc_handle(vpic_interrupt_t *vpic, uint32_t *intspec);

void vpic_assert_vint_rxq(queue_t *q);
void vpic_assert_vint_txq(queue_t *q);
void vpic_assert_vint(vpic_interrupt_t *irq);
void vpic_deassert_vint(vpic_interrupt_t *irq);

void critdbell_to_gdbell_glue(trapframe_t *regs);
interrupt_t *vpic_iack(void);

extern int_ops_t vpic_ops;

#endif
