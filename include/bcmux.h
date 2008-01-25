/**************************************************************************//*
    @file   byte_chan.h

    @brief  This file contain the implementation of general byte_chan interface
    The byte_chan interface usually implements 
	
    
    The byte_chan generic interface is a user accessible layer that is responcible primarily
    for data bufferization. The idea is to separate a LLD (Low Level Driver) and
    a higher level, more OS dependent layer.
    

    $Id: $
 **************************************************************************/


#include  "queue.h"
#include  "byte_chan.h"
#include  "uverrors.h"


/*==================================================================================================
                                       LOCAL VARIABLES
==================================================================================================*/

/** byte_chans array and a next free pointer */

#ifndef MUX_COMPLEX_H_
#define MUX_COMPLEX_H_



typedef struct mux_complex_s
{
	int  byte_chan;
	struct connected_bc_s* current_tx_bc;
	struct connected_bc_s* current_rx_bc;
	struct connected_bc_s* first_bc;	
	int current_tx;
	int current_rx;
	int rx_flag_state;
//	queue_t*        output_queue;
	int tx_flag_state;
	int tx_flag_num;
	int rx_problem;
//	int curr_rx_length;
	int num_of_channels;
}mux_complex_t;

typedef struct connected_bc_s
{
	char                   num;
	int                    byte_chan;
	mux_complex_t*         mux_complex; 
	struct connected_bc_s* next;
	
}connected_bc_t;


int mux_complex_init(int byte_chan, mux_complex_t** mux);

int vuart_init(void**      lld_handler,
			   int        lld_num, 
			   void*      byte_chan, 
			   void*      lld_param,
			   data_rx_t  data_rx,
			   data_get_t data_get);

int mux_complex_add(mux_complex_t* mux_complex, 
					int  byte_chan,
					char multiplexing_id);


#endif


/*================================================================================================*/



