/*!
    @file   byte_chan.h

    @brief  This file contain the implementation of general byte channel interface
 */

 /* (c) Copyright Freescale 20XY, All Rights Reserved */

#ifndef BYTE_CHAN_H_
#define BYTE_CHAN_H_

#include "queue.h"
#include <stdint.h>
#include <percpu.h>
#include <16552D.h>

typedef void (*byte_chan_rx_data_avail_t)(void* handler);
typedef void (*byte_chan_tx_space_avail_t)(void* handler);

/* API between LLD and upper level */
/* LLD calls upper level with the following API */
typedef int (*data_rx_t) (void* byte_chan, char const *, int);

typedef int  (*data_get_t)(void* byte_chan, char*, int);

#if 0
/* LLD API */
typedef int (*lld_init_t)(void** lld_handler,
	 int        lld_num,
	 void*      byte_chan, 
	 void*      params,
	 data_rx_t  data_rx,
	 data_get_t data_get);
#endif

typedef void (*lld_start_tx_t)(void* lld_handler);

/** byte_chan_t - This is a generic byte_chan device description  */
typedef struct byte_chan_s {
	byte_chan_rx_data_avail_t  byte_chan_rx_data_avail [2]; /**< rx callback */
	byte_chan_tx_space_avail_t byte_chan_tx_space_avail[2]; /**< tx callback */
	void *end_point_handler[2];  /**< Example stuff 0 */
	queue_t *queue[2];  /**< queues */
	int byte_chan_handle;
} byte_chan_t;

typedef struct byte_chan_reg_params_s {
	byte_chan_rx_data_avail_t  byte_chan_rx_data_avail; /**< rx callback */
	byte_chan_tx_space_avail_t byte_chan_tx_space_avail; /**< tx callback */
	void* end_point_handler;         /**< Example stuff 0 */	
} byte_chan_reg_params_t;

typedef struct byte_chan_param_s {
	int       queue_size_0_1;    /**< size of output buffer */
	int       queue_size_1_0;     /**< size of input buffer */       
} byte_chan_param_t;

typedef struct bc_handle {
	int32_t channel;
	int32_t handle_A;
	int32_t assigned_A;
	int32_t handle_B;
	int32_t assigned_B;
	struct bc_handle *next;
}bc_handle_t;

void byte_chan_global_init(void);

void byte_chan_partition_init(guest_t *guest);

int byte_chan_alloc(byte_chan_param_t* byte_chan_param);

int byte_chan_send(int byte_channel_handle, char const* str, int length);

int byte_chan_receive(int byte_chan_handle, char* buf, int length);

int byte_chan_get_space(int byte_chan_handle);

int byte_chan_register(int byte_chan_handle, byte_chan_reg_params_t* byte_chan_reg_params);

int duart_init(int  byte_chan, uart_param_t const *uart_param);

#endif  //BYTE_CHAN_H_
