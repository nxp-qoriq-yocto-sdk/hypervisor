/** @file
 * Byte channel interface.
 */
/*
 * Copyright (C) 2008,2009 Freescale Semiconductor, Inc.
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

#ifndef BYTE_CHAN_H
#define BYTE_CHAN_H

#include <libos/queue.h>
#include <libos/chardev.h>
#include <stdint.h>
#include <percpu.h>
#include <devtree.h>

typedef struct byte_chan_handle {
	queue_t *tx;      /**< queue for transmitting data */
	queue_t *rx;      /**< queue for receiving data */
	uint32_t tx_lock; /**< lock for transmitting data */
	uint32_t rx_lock; /**< lock for receiving data */
	int attached;     /**< non-zero if something has attached to this endpoint. */
	handle_t user;    /**< user handle */
} byte_chan_handle_t;

/** byte_chan_t - This is a generic byte_chan device description  */
typedef struct byte_chan {
	queue_t q[2];  /**< queues */
	byte_chan_handle_t handles[2];
} byte_chan_t;

void byte_chan_partition_init(guest_t *guest);

byte_chan_t *byte_chan_alloc(void);

ssize_t byte_chan_send(byte_chan_handle_t *bc,
                       const uint8_t *buf, size_t length);
ssize_t byte_chan_receive(byte_chan_handle_t *bc,
                          uint8_t *buf, size_t length);

byte_chan_handle_t *byte_chan_claim(byte_chan_t *bc);
int byte_chan_attach_chardev(byte_chan_t *bc, chardev_t *cd);

struct dt_node;
byte_chan_t *other_attach_byte_chan(struct dt_node *node,
                                    struct dt_node *endpoint);

int init_byte_channel(dt_node_t *node);

#endif
