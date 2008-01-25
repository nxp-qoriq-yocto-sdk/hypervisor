/**************************************************************************//*
    @file   byte_chan.h

    @brief  This file contain the implementation of general byte_chan interface
    The byte_chan interface usually implements 
	
    
    The byte_chan generic interface is a user accessible layer that is responcible primarily
    for data bufferization. The idea is to separate a LLD (Low Level Driver) and
    a higher level, more OS dependent layer.
    

    $Id: $
 **************************************************************************/

#include  "libos/libos.h"
#include  "queue.h"
#include  "byte_chan.h"
#include  "bcmux.h"
#include  "uverrors.h"
#include  <string.h>


#define printf(X...)
/*==================================================================================================
                                       LOCAL VARIABLES
==================================================================================================*/

/** byte_chans array and a next free pointer */


#define TX_THRESHOLD_QUEUE   8


#define TX_SEND_ESCAPE      0x12
#define TX_SEND_DATA        0x13
#define RX_BUFF_SIZE 		20

/*==================================================================================================
                                       LOCAL FUNCTIONS
==================================================================================================*/

/**************************************************************************//**

	@Function      channel_find
	
    @brief This function finds a channel according to id

    This function reads data from multiplexed channel and writes it
    to connected channels using mux protocol
    
    @pre    N/A
    @post   N/A

    @param [in]     mux_p   IN multiplexer handler

    @return void
    
    @note It can be called from any partition.
    
*//***************************************************************************/

static connected_bc_t* channel_find(mux_complex_t* mux, char id)
{
	connected_bc_t* bc = mux->first_bc;
	
	while(bc)
	{
		if(bc->num == id)
			return bc;
		else
		{
			bc = bc->next;
		}
	}
	return NULL;
}




/**************************************************************************//**

	@Function      mux_get_data
	
    @brief This function sends data to the connected channels

    This function reads data from multiplexed channel and writes it
    to connected channels using mux protocol
    
    @pre    N/A
    @post   N/A

    @param [in]     mux_p   IN multiplexer handler

    @return void
    
    @note It can be called from any partition.
    
*//***************************************************************************/
int mux_rx;
int mux_err;
int mux_err_1;
static void mux_get_data(void* mux_p)
{
	mux_complex_t* mux = mux_p;
	char str[RX_BUFF_SIZE];
	int i;
	int length;
	connected_bc_t* bc;
	
	//osHwiSwiftDisable();
	
	/* Add a string to byte channel */
	while((length = byte_chan_receive(mux->byte_chan, str, RX_BUFF_SIZE)) > 0)
		for(i = 0; i < length; i++)
		{
			mux_rx++;
			if(mux->rx_flag_state == 1)
			{
				mux->rx_flag_state = 0;
				/* If not B it means that it is an escape */
				if(str[i] != 'B')
				{
					bc = channel_find(mux, str[i]);					
					/* Can not find this one */
					/* for now skip it       */
					if(bc == NULL)
						mux->rx_problem++;
					else
					{
						mux->current_rx_bc = bc;
						mux->tx_flag_state = TX_SEND_ESCAPE;
					}
						
					continue;

				}
			} else if(str[i] == 'B')
			{
				mux->rx_flag_state++;
				continue;
			}
			if(mux->current_rx_bc)
			{
				int ret;
				ret = byte_chan_send(mux->current_rx_bc->byte_chan, &str[i], 1);
				if(ret < 0)
					mux_err_1++;
			}
		    	

		}
	//osHwiSwiftEnable();
};


static char str_escape[] = "BXBX";
static char str_char[] = "BB";

/**************************************************************************//**

	@Function      mux_send_data

    @brief This function sends data to a multiplexed channel 

    The function checks all its channels for input and sends the data 
    to a multiplexed channel. This function sends the data from the same
    channel. If specific channel does not have a data it will send the data
    from the next channel.

    @pre    N/A
    @post   N/A

    @param [in]     mux_p   IN multiplexer handler

    @return void

    @note It can be called from any partition.

*//***************************************************************************/
int err_counter;
void mux_send_data(void* mux_p)
{
	mux_complex_t*  mux = mux_p;
	int counter = 0;
	connected_bc_t* bc;
	
	bc = mux->current_tx_bc;
	int num;
	char c;
	//TODO protect with spinlock
	//osHwiSwiftDisable();
	printf("mux_send_data\n");
	while(byte_chan_get_space(mux->byte_chan) > TX_THRESHOLD_QUEUE)
	{
		num = byte_chan_receive(bc->byte_chan, &c, 1);
		if(num)
			printf("Num is %d Char is %c\n", num, c);
		else
		{
			printf("No data\n");
			if(bc == mux->current_tx_bc)
				mux->tx_flag_state = TX_SEND_DATA;
			else	
				mux->current_tx_bc = bc;
		}
		if(num == 0)
		{
			mux->tx_flag_state = TX_SEND_ESCAPE;
			//Here switch to next vuart
			printf("Falg is ESCAPE\n");
			bc = bc->next;
			if(bc == NULL)
				bc = mux->first_bc;
			counter++;
			if(counter >= mux->num_of_channels)
				break;	
			continue;
			
		}

		/* If we are here it means that num is 1                */
		/* at this point we may need to create an flag symbol */

		/* SEND_ESCAPE here we need to send an flag character */
		printf("tx_flag = %x\n", mux->tx_flag_state);
		if(mux->tx_flag_state == TX_SEND_ESCAPE)
		{
			/* TODO workarround */
//			str_escape[3] = bc->num;
			printf("sending escape %p\n", bc);
			str_escape[1] = bc->num;
			byte_chan_send(mux->byte_chan, str_escape, 2);
			mux->tx_flag_state = TX_SEND_DATA;
		}

		if(c == 'B')
		{
			byte_chan_send(mux->byte_chan, str_char, 2);
			continue;			
		}

		if(byte_chan_send(mux->byte_chan, &c, 1) < 0)
			err_counter++;
		printf("Sending %c\n",c);

	}
	//osHwiSwiftEnable();
};

/*==================================================================================================
                                       GLOBAL FUNCTIONS
==================================================================================================*/

/**************************************************************************//**

	@Function      mux_complex_init

    @brief This function initializes a mux complex and returns its poinnter

    The function allocates a memory for multiplexer and initializes its
    members.

    @pre    N/A
    @post   N/A

    @param [in]     byte_chan   IN byte channel handler of multiplexed channel

    @return void

    @note It can be called from any partition.

*//***************************************************************************/

int mux_complex_init(int byte_chan, mux_complex_t** mux_p)
{
	int                    byte_channel_handle;
	mux_complex_t*         mux;
	byte_chan_reg_params_t byte_chan_reg_params;
	
	mux = alloc(sizeof(mux_complex_t), sizeof(int));
	if(mux == NULL)
	{
		return MEM_NOT_ENOUGH;	
	}

	mux->byte_chan     = byte_chan;
	mux->first_bc      = NULL;
	mux->rx_flag_state = 0;
	mux->current_tx_bc = NULL;
	mux->current_rx_bc = NULL;
	mux->tx_flag_state = TX_SEND_ESCAPE;
	mux->rx_flag_state = 0;
	mux->tx_flag_num   = 0;
	mux->num_of_channels = 0;

	/* Here we register a mux as an end point for byte channel */
	byte_chan_reg_params.end_point_handler        = mux;
	byte_chan_reg_params.byte_chan_rx_data_avail  = mux_get_data;
	byte_chan_reg_params.byte_chan_tx_space_avail = mux_send_data;

	byte_chan_register(byte_chan, &byte_chan_reg_params);	

	*mux_p = mux;
	return 1;
}

/**************************************************************************//**

	@Function      mux_complex_add

    @brief Adding a new byte channel to multiplexer

    @pre    N/A
    @post   N/A

    @param [in]     byte_chan   IN byte channel handler of multiplexed channel

    @return void

    @note It can be called from any partition.

*//***************************************************************************/

int mux_complex_add(mux_complex_t* mux_complex, 
					int  byte_chan,
					char multiplexing_id)
{

	connected_bc_t* bc;
	bc = alloc(sizeof(connected_bc_t), sizeof(int));
	byte_chan_reg_params_t byte_chan_reg_params;
	
	bc->num         = multiplexing_id;
	bc->byte_chan   = byte_chan;
	bc->mux_complex = mux_complex;
//	bc->next        = NULL;
	
	if(mux_complex->first_bc == NULL)
		mux_complex->first_bc = bc;
	else
	{
		connected_bc_t* bc_chain = mux_complex->first_bc;

		while(bc_chain->next != NULL)
			bc_chain = bc_chain->next;
		bc_chain->next = bc;
	}
	
	byte_chan_reg_params.end_point_handler        = mux_complex;
	byte_chan_reg_params.byte_chan_rx_data_avail  = mux_send_data;
	byte_chan_reg_params.byte_chan_tx_space_avail = mux_get_data;
	
	byte_chan_register(byte_chan, &byte_chan_reg_params);	
	
	mux_complex->current_tx_bc = bc;
	mux_complex->num_of_channels++;
	return 1;
}

#define TEST
#ifdef TEST

uart_param_t test_uart_params = 
{
	8, 1, 'n', 115200, 0, CCSRBAR_VA+0x11d500,0
};


void test_byte_chan_mux(void)
{
        byte_chan_param_t byte_chan_param;
        char str[10];
        int byte_chan;
        int num;
	int byte_chan0;
	int byte_chan1;
	struct mux_complex_s* vuart_complex;	
	int byte_chan_to_lld;


	byte_chan_param.queue_size_0_1 = 0x100;
	byte_chan_param.queue_size_1_0 = 0x100;


	byte_chan0 = byte_chan_alloc(&byte_chan_param);
	
	byte_chan1 = byte_chan_alloc(&byte_chan_param);
			
	byte_chan_to_lld = byte_chan_alloc(&byte_chan_param);

	duart_init(byte_chan_to_lld + 1, &test_uart_params);
	
	mux_complex_init(byte_chan_to_lld, &vuart_complex);
	
	mux_complex_add(vuart_complex, byte_chan0 + 1, 48);
	mux_complex_add(vuart_complex, byte_chan1 + 1, 49);

	byte_chan_send(byte_chan0, "ABCDEF\n\r", 8);
	byte_chan_send(byte_chan1, "UUUU", 4);
	byte_chan_send(byte_chan0, "GGGG", 4);
	byte_chan_send(byte_chan1, "DDDD", 4);

        while(1)
        {
                num = byte_chan_receive(byte_chan0, str, 1);
                if(num == 1)
                {
                       // printf("Chan 0 = %c", str[0]);
			byte_chan_send(byte_chan1, str, 1);
	
                }

               num = byte_chan_receive(byte_chan1, str, 1);
                if(num == 1)
                {
//                        printf("Chan 1 = %c", str[0]);
			byte_chan_send(byte_chan0, str, 1);

                }


        }
	

	

}

#endif
