/*!
 *  (c) Copyright Freescale 2007, All Rights Reserved
 *
 *  @file byte_chan.c
 *
 *	
 */

#include  "queue.h"
#include  "libos/libos.h"
#include  "libos/bitops.h"
#include  "byte_chan.h"
#include  "uverrors.h"
#include  "stdint.h"
#include  "string.h"

bc_handle_t *channel_list_head = NULL; /*!< global byte-channel list */
bc_handle_t *channel_list_tail = NULL;

uart_param_t uart_params = 
{
	8, 1, 'n', 115200, 0, CCSRBAR_VA+0x11d500,0
};

#define QUEUE_SIZE 30 

/*! MAX_BYTE_CHAN defines a maximum number of byte_chan ports that can be opened
on all partitions together */
#define MAX_BYTE_CHAN  20

/*! byte_chans array and a next free pointer */
static byte_chan_t byte_chan_arr[MAX_BYTE_CHAN];
static int next_byte_chan;
static int byte_chan_init_guard;
static int byte_chan_get_data(queue_t* queue, char* str, int length);
static int byte_chan_add_data(queue_t* queue, char const* str, int length);


/*!

    @fn byte_chan_get_data

    @brief This function retrievs a data from byte_chan queue - it is typically
    used by LLD and not by upper level interfaces
    
    @param [in ]     byte_chan    IN a handler of byte_chan interface
    @param [out]     str       IN pointer to a string
    @param [in ]     length    IN string length

    @return int - number of retrieved bytes. -1 if fail. 
    
*/
static int byte_chan_get_data(queue_t* queue, char* str, int length)
{
	int i;
	for(i = 0; i < length; i++)
		if(queue_get(queue, &str[i]) == -1)
		{			
			return i;
		}			
	return length;
}



/*!

    @fn byte_chan_add_data

    @brief This function adds a data to byte_chan output queue - it is typically
    used by LLD and not by upper level interfaces
    
    @param [in]     byte_chan    IN a handler of byte_chan interface
    @param [in]     str       IN pointer to a string
    @param [in]     length    IN string length

    @return int - number of written bytes. -1 if fail. 
    
    @note 0 return value is a a valid value. The function fill return it
    in two cases:
    	1. length is 0
    	2. Not enough room for the entire string
    
*/

static int byte_chan_add_data(queue_t* queue, char const* str, int length)
{
	//TODO take a spinlock
	int i;
	int ret;
	
	if(queue_get_room(queue) < length)
		return -1;
	for(i = 0; i < length; i++)
	{
		ret = queue_add(queue, str[i]);
		assert(ret != -1);
		//This is impossible as we checked it before
	}	
	return length;
}

/*!
    @fn byte_chan_global_init

    @brief parses global device tree and sets up all byte-channels 

*/
void byte_chan_global_init(void)
{
	byte_chan_param_t byte_chan_param;
	bc_handle_t *ptr = NULL;
	int32_t handle;

	/* need to parse device tree here */
	/* loop over channels */

	/* FIXME: right now this is hardcoded to
           set up 1 uart channel only */

	ptr = alloc(sizeof(bc_handle_t),4);

	ptr->channel = 0;  /* FIXME - dev tree */

	byte_chan_param.queue_size_0_1 = QUEUE_SIZE;
	byte_chan_param.queue_size_1_0 = QUEUE_SIZE;

	/* alloc a channel */
	ptr->handle_A = byte_chan_alloc(&byte_chan_param);
	ptr->handle_B = ptr->handle_A + 1;

	/* connect it to a uart */  /* FIXME - dev tree */
	duart_init(ptr->handle_B, &uart_params);

	ptr->assigned_A = 0;
	ptr->assigned_B = 1;  /* assigned */
	ptr->next = NULL;

	if (channel_list_head == NULL) {
		channel_list_head = ptr;
		channel_list_tail = ptr;
	} else {
		channel_list_tail->next = ptr;
		channel_list_tail = ptr;
	}

}

void byte_chan_partition_init(guest_t *guest)
{
	int32_t channel;
	int32_t total;
	bc_handle_t *p;
	int assigned;

	/* need to parse partition device tree here */
	/* loop over virtual-devices node */
	/* determine how many byte channels the partition has*/

	total = 1;  /* FIXME - dev tree */

	if (total == 0)
		return;

	guest->bc = alloc(sizeof(uint32_t)*total,4);

	guest->bc_cnt = 0;

	/* get channel # */
	channel = 0;  /* FIXME - hardcoded */

	assigned = 0;
	for (p = channel_list_head; p != NULL; p = p->next) {
		assert(guest->bc_cnt <= total);
		if (channel == p->channel) {
			if (!p->assigned_A) {
				guest->bc[guest->bc_cnt] = p->handle_A;
				guest->bc_cnt++;
				p->assigned_A = 1;
				assigned = 1;
			} else if (!p->assigned_B) {
				guest->bc[guest->bc_cnt] = p->handle_B;
				guest->bc_cnt++;
				p->assigned_B = 1;
				assigned = 1;
			}
		}
	}

	if (assigned == 0) {
		printf("ERROR: partition was allocated non-existent byte-channel\n");
	}
}

/*!

    @fn byte_chan_alloc

    @brief This function initializes byte channels and returns a handle
     
    @param [in]     byte_chan_param

    @return int - handle if succesefull. negative if fail
    
    @note Can be called from any partition however pay attention
    
*/

int byte_chan_alloc(byte_chan_param_t* byte_chan_param)
{
	int                ret;
	byte_chan_t       *byte_chan;
	int                byte_channel_handle;
	void*              lld_handler;	
	
	/* Open device with given parameters */
	if(next_byte_chan == MAX_BYTE_CHAN)	
		return NO_MORE_CHANNELS;
	
	byte_channel_handle = next_byte_chan;
	byte_chan = &byte_chan_arr[next_byte_chan++];
	
	ret = queue_create(byte_chan_param->queue_size_0_1, &byte_chan->queue[0]);
	if(ret != 1)
		return ret;
	
	ret = queue_create(byte_chan_param->queue_size_1_0, &byte_chan->queue[1]);
	if(ret != 1)
		return ret;
	

	/*------------ ETC ETC ETC -----------*/

	return byte_channel_handle<<1;
}



/*!
    @fn byte_chan_send

    @brief This function sends a data to byte channel

    The function will take its input and add it into a queue.
    Then it sends a signal to a receive channel.

    @pre    N/A
    @post   N/A

    @param [in]   byte_chan_handle  IN a handler of byte_chan interface
    @param [out]  buf               IN data's pointer
    @param [in]   length            IN parameter description

    @return int - number of written bytes. -1 if fail

    @note It can be called from any partition however pay attention that
    any callbacks (like Tx or Rx) will be called in native partition context
    
*/
int byte_chan_send(int byte_chan_handle, char const* buf, int length)
{
	byte_chan_t       *byte_chan;
	int end_point = byte_chan_handle & 1;
	int transmitted_length;
	long x;

	x = mfmsr();
	mtmsr(x&~(MSR_CE));

	if((byte_chan_handle > (MAX_BYTE_CHAN >> 1)) ||  (byte_chan_handle < 0))
		return INVALID_PARAM;
	
	byte_chan_handle = byte_chan_handle & (~1);
	byte_chan = &byte_chan_arr[byte_chan_handle>>1];
	
	if((byte_chan == NULL) || (buf == NULL)) {
		return NULL_POINTER;
	}
	
	
	/* Send data */
	/* First we queue data to our output queue */
	transmitted_length = byte_chan_add_data(byte_chan->queue[end_point], buf, length);
	
	/* Here we have to decide how to act */
	/* We have two options			 */
	/* 1. If there is no enough room - return 0 and believe that a user */
	/* 	  will implement retransmit by himself with Tx interrupt                */
	/* 2. try to transmit something even if is less then length           */
	
	/* For now we implement option 1.									*/
	
	/* this is a callback */
	/* In multicore case it will signal instead of directly calling it */
	if(transmitted_length > 0)
	if(byte_chan->byte_chan_rx_data_avail[!end_point])
		byte_chan->byte_chan_rx_data_avail[!end_point](byte_chan->end_point_handler[!end_point]);

	mtmsr(x);
	
	return transmitted_length;
		
}


/*!
    @fn byte_chan_receive

    @brief Returns number of bytes that are available in byte channel's queue

    @param [in]  byte_chan_handle    IN a handler of byte_chan interface
    @param [in]  buf                 IN pointer to buffer
    @param [in]  length              IN buffer's length

    @return int - number of received bytes.
    
    @note It can be called from any partition
*/

int byte_chan_receive(int byte_chan_handle, char* buf, int length)
{

	byte_chan_t       *byte_chan;
	int end_point = byte_chan_handle & 1;
	
	if((byte_chan_handle > (MAX_BYTE_CHAN >> 1)) ||  (byte_chan_handle < 0))
		return INVALID_PARAM;
	
	byte_chan_handle = byte_chan_handle & (~1);
	byte_chan = &byte_chan_arr[byte_chan_handle>>1];
	
	//TODO check a hanlder
	
	int received_length;
	if(byte_chan == NULL || buf == NULL) {
		return NULL_POINTER;
	}

//TODO	if(!byte_chan->lld_dev.rx_enabled)
//	{
//		return DEVICE_DISABLED;
//	}

	
	/* dequeue data from our input queue */
	received_length = byte_chan_get_data(byte_chan->queue[!end_point], buf, length);
	
	/* this is a callback */
	/* In multicore case it will signal instead of directly calling it */
	if(received_length)
	if(byte_chan->byte_chan_tx_space_avail[!end_point])
		byte_chan->byte_chan_tx_space_avail[!end_point](byte_chan->end_point_handler[!end_point]);
	
	return 	received_length;
}


/*!
    @Function      byte_chan_get_space
	
    @brief Returns number of bytes that are available in byte channel's queue

    @pre    N/A
    @post   N/A

    @param [in]     byte_chan_handle    IN a handler of byte_chan interface

    @return int - number of available bytes.
    
    @note It can be called from any partition
*/

int byte_chan_get_space(int byte_chan_handle)
{
	byte_chan_t       *byte_chan;
	int end_point = byte_chan_handle & 1;
	
	if((byte_chan_handle > (MAX_BYTE_CHAN >> 1)) ||  (byte_chan_handle < 0))
		return INVALID_PARAM;

	byte_chan_handle = byte_chan_handle & (~1);
	byte_chan = &byte_chan_arr[byte_chan_handle>>1];
	
	return queue_get_room(byte_chan->queue[end_point]);
	
};

/*!
    @fn      byte_chan_register
	
    @brief Register an end-point

    @pre    N/A
    @post   N/A

    @param [in]  byte_chan_handle     IN a handler of byte_chan interface
    @param [in]  byte_chan_reg_params IN pointer to buffer

    @return int - 1 if successeful, negative if fail
    
    @note It can be called from any partition
*/

int byte_chan_register(int byte_chan_handle, byte_chan_reg_params_t* byte_chan_reg_params)
{
	byte_chan_t       *byte_chan;
	int end_point = byte_chan_handle & 1;
	
	if((byte_chan_handle > (MAX_BYTE_CHAN >> 1)) ||  (byte_chan_handle < 0))
		return INVALID_PARAM;
	
	byte_chan_handle = byte_chan_handle & (~1);
	byte_chan = &byte_chan_arr[byte_chan_handle>>1];
	
	byte_chan->end_point_handler[end_point]  = byte_chan_reg_params->end_point_handler;
	byte_chan->byte_chan_rx_data_avail[end_point]  = byte_chan_reg_params->byte_chan_rx_data_avail;
	byte_chan->byte_chan_tx_space_avail[end_point] = byte_chan_reg_params->byte_chan_tx_space_avail;

	return 1;
}
