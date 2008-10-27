
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

#include <percpu.h>
#include <vpic.h>

typedef struct guest_recv_dbell_list {
	vpic_interrupt_t *guest_vint;
	struct guest_recv_dbell_list *next;
} guest_recv_dbell_list_t;

typedef struct ipi_doorbell {
	struct guest_recv_dbell_list *recv_head;
	uint32_t dbell_lock;
} ipi_doorbell_t;

typedef struct ipi_doorbell_handle {
	ipi_doorbell_t *dbell;
	handle_t user;
} ipi_doorbell_handle_t;

/* Prototypes for functions in ipi_doorbell.c */
int send_doorbells(struct ipi_doorbell *dbell);
int attach_receive_doorbell(guest_t *guest, struct ipi_doorbell *dbell, int offset);
void create_doorbells(void);
void send_dbell_partition_init(guest_t *guest);
void recv_dbell_partition_init(guest_t *guest);
