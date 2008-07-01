/*
 *  @file   byte_chan.h
 *
 *  @brief  This file contain the implementation of general byte_chan interface
 *   The byte_chan interface usually implements 
 *
 *  The byte_chan generic interface is a user accessible layer that is responcible primarily
 *  for data bufferization. The idea is to separate a LLD (Low Level Driver) and
 *  a higher level, more OS dependent layer.
 */
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

#include  <libos/queue.h>
#include  <byte_chan.h>
#include  <errors.h>

/** byte_chans array and a next free pointer */

#ifndef MUX_COMPLEX_H_
#define MUX_COMPLEX_H_

#include <byte_chan.h>

#define MAX_MUX_CHANNELS 32

typedef struct mux_complex {
	byte_chan_handle_t *byte_chan;
	struct connected_bc *current_tx_bc;
	struct connected_bc *current_rx_bc;
	struct connected_bc *first_bc;
	int current_tx;
	int current_rx;
	int rx_flag_state;
	int tx_flag_state;
	int tx_flag_num;
	int rx_discarded;
	int num_of_channels;
	int rx_count;
} mux_complex_t;

typedef struct connected_bc {
	byte_chan_handle_t *byte_chan;
	mux_complex_t *mux_complex; 
	struct connected_bc *next;
	char num;
} connected_bc_t;

int mux_complex_add(mux_complex_t *mux_complex, 
                    byte_chan_t *byte_chan,
                    char multiplexing_id);

void create_muxes(void);

#endif
