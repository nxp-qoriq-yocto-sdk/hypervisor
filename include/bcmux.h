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
    
#include  <libos/queue.h>
#include  <byte_chan.h>
#include  <errors.h>

/** byte_chans array and a next free pointer */

#ifndef MUX_COMPLEX_H_
#define MUX_COMPLEX_H_

#include <byte_chan.h>

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
