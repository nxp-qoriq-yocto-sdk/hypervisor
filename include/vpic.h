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

#include <libos/trapframe.h>
#include <percpu.h>
#include <stdint.h>

/* irq descriptor */
typedef struct {
	int irq;
	guest_t *guest;
} vint_desc_t;

int vpic_alloc_irq(guest_t *guest);
int vpic_process_pending_ints(guest_t *guest);
void vpic_assert_vint_rxq(queue_t *q);
void vpic_assert_vint_txq(queue_t *q);
void vpic_assert_vint(guest_t *guest, int irq);
void vpic_irq_mask(int irq);
void vpic_irq_unmask(int irq);
void vpic_irq_set_vector(int irq, uint32_t vector);
uint32_t vpic_irq_get_vector(int irq);
void vpic_irq_set_destcpu(int irq, uint8_t destcpu);
uint8_t vpic_irq_get_destcpu(int irq);
void critdbell_to_gdbell_glue(trapframe_t *regs);
void vpic_eoi(int coreid);


#endif
