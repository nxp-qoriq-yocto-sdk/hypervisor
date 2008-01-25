/*!
    @file 16552D.c

    @brief  This file contain the implementation of 16552D device driver
    
    The driver should work along with byte channel module and it acts as one
    of the endpoints

*/

#include "byte_chan.h"
#include "16552D.h"
#include "uverrors.h"
#include "uv.h"

#include <stdint.h>
#include <string.h>
#include <libos/libos.h>
#include <interrupts.h>
#include <libos/io.h>

// FIXME -- get clock from device tree
#define get_system_glock() 266

/** byte_chans array and a next free pointer */
static void duart_isr(unsigned long lld_handler);

#define WRITE_UART(p, reg, val) out8((uint8_t *)(p->offset+reg),val)
#define READ_UART(p, reg) in8((uint8_t *)(p->offset+reg))

int tx_counter;
int rx_counter;

/*!
	
    @brief starts transmit process

    If transmit is active this function returns immediately
    The function will try to transmit as much as possible and 
    activate Tx FIFO empty interrupt.
    
    @param [in]     lld_handler  IN a handler of UART LLD.

    @return void
    
    @note It can be called from any partition
    
*/

void duart_start_tx(void* lld_handler)
{
	duart_t		 *uart	   = lld_handler;
	uint8_t		 intr_status, umsr, ulsr, data;
	uint8_t		 tmpReg;
	uint32_t		i, count=0;
	int counter	 = 0;
	
	assert(uart != NULL);

	//TODO DISABLE ISR UART ISR possible race condition
	if(uart->tx_active != 0)
		return;
	uart->tx_active = 1;
	
	//TODO ENABLE ISR
	WRITE_UART(uart, IER, IER_ERDAI | IER_ETHREI);
	
	return;
}

/*!
	
    @brief UART ISR for tx and rx

    The function handles hardware interrupt of UART

    @param [in]     lld_handler  IN a handler of UART LLD.

    @return void

    @note Called on primary partition
    
*/

int uart_err;
static void duart_isr(unsigned long lld_handler)
{
	duart_t *uart       = (duart_t *)lld_handler;
	uint8_t intr_status, msr, lsr, data;
	uint8_t tmpReg;
	uint32_t i, count=0;
    
	assert(uart != NULL);

	intr_status = READ_UART(uart, IIR);
	intr_status &= DUART_INTR_MASK;
    
	if(intr_status == 0)
		msr = READ_UART(uart, MSR);

	/* Receiver Line Status Error */
	if(intr_status == IIR_RLSI) {
		lsr = READ_UART(uart, LSR);
		if(lsr & LSR_OE)
			uart->overrun_errors;
		if(lsr & LSR_RFE)
			uart->rx_fifo_errors;   
	}

	/*Either receiver data available or receiver timeout, call store */
	if((intr_status & (IIR_CTOI | IIR_RDAI)) != 0) {
		while(1) {
			lsr = READ_UART(uart, LSR);
			if(lsr & LSR_DR) {    
				int ret;			
				data = READ_UART(uart, RBR);
				rx_counter++;
				ret = byte_chan_send(uart->byte_chan, (char *)&data, 1);
				if(ret < 0)
					uart_err++;
			} else
				break;   			
		}
	}

	/* Transmitter holding register empty */
	/* Try to transmit data - if no data available desactivate ISR */
	if((intr_status & IIR_THREI) != 0) {
		/*FIFO mode*/
		while(READ_UART(uart, LSR) & LSR_THRE) {
			if(byte_chan_receive(uart->byte_chan, (char *)&data, 1) == 1) {
				tx_counter++;
				WRITE_UART(uart, THR, data);
			} else {
				/* desactivate ISR and channel */
				/* It will be activated by duart_start_tx call */
				uart->tx_active = 0;
				WRITE_UART(uart, IER, IER_ERDAI);
				break;
			}
		}
	}

}

/*!
	
    @brief Initialize UART

    The function will take its input and queue it into output queue.
    Then it will activate a LLD Tx operation
    
    @param [in]     byte_chan  IN a handler of byte_chan interface
    @param [in]     uart_param IN parameter description

    @return int -   1 if successful. negative if fail
    
    @note Should be called from the master partition
    
*/
int duart_init(int  byte_chan, uart_param_t const *uart_param)
{
	uint8_t tmp;	
	duart_t *uart;
	int divisor;
	byte_chan_reg_params_t byte_chan_reg_params;
	
	uart = alloc(sizeof(duart_t),4);
	
	if(uart == NULL) {
		return MEM_NOT_ENOUGH;	
	}
	
	if(uart_param == NULL) {
		return NULL_POINTER;
	}
	
	if((uart_param->baudrate < 600) || (uart_param->baudrate > 512000))
		return INVALID_PARAM;

	/* copy all the parameters for statistics */
	uart->data_bits  = uart_param->data_bits ; 
	uart->stop_bits  = uart_param->stop_bits ; 
	uart->parity     = uart_param->parity    ;    
	uart->baudrate   = uart_param->baudrate  ;  
	uart->attributes = uart_param->attributes;
	uart->offset     = (char*)uart_param->offset;
	uart->byte_chan  = byte_chan;
	
	byte_chan_reg_params.end_point_handler = uart;
	byte_chan_reg_params.byte_chan_rx_data_avail  = duart_start_tx;
	byte_chan_reg_params.byte_chan_tx_space_avail = NULL;
	byte_chan_register(byte_chan, &byte_chan_reg_params);
	
	/* First calculate a divider */
	divisor = get_system_glock()*1000000 / uart_param->baudrate / 16;
 
	/* Set access to UDMB/UDLB/UAFR */
	WRITE_UART(uart, LCR, LCR_DLAB);
	
	WRITE_UART(uart, DMB, (divisor >> 8) & 0xff);
	WRITE_UART(uart, DLB, divisor & 0xff);

	/* Building LCR register */
	switch(uart_param->parity) {
		case 'n': //Non
			tmp = 0;
			break;
		case 'o': //Odd
			tmp = LCR_PEN;
			break;
		case 'e': //Even
			tmp = LCR_PEN | LCR_EPS;
			break;
		case 's': //Space
			tmp = LCR_PEN | LCR_EPS | LCR_SP;
			break;
		case 'm': //Mark
			tmp = LCR_PEN | LCR_SP;
			break;
		default: 
			return INVALID_PARAM;
	}

	/* Data bits configuration */
	if ( (uart_param->data_bits < 5) || (uart_param->data_bits > 8) )
		return INVALID_PARAM;	
	tmp |= uart_param->data_bits - 5;
	
	/* Stop bits configuration */
	if ( (uart_param->stop_bits != 1) && (uart_param->data_bits != 2) )
		return INVALID_PARAM;	
	tmp |= ((uart_param->stop_bits - 1) << 2);
	
	WRITE_UART(uart, LCR, tmp);
	
	/* Set FCR register and reset FIFOs */
	tmp = FCR_FEN | FCR_RFR | FCR_TFR;
	WRITE_UART(uart, FCR, tmp);

	/* Set Ready To Send to 1 */
	//WRITE_UART(uart, MCR, MCR_RTS | MCR_LOOP);
	WRITE_UART(uart, MCR, MCR_RTS);
	
	register_critical_handler(0, (int_handler_t)&duart_isr, (uint32_t)uart);

	// FIXME: check return code

	/* Enable interrupts */
	WRITE_UART(uart, IER, IER_ETHREI);

	return 1;
		
}
