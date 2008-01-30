/*!
 * @file
 * @brief  This file contain the implementation of a ns16550 device driver
 *
 */

/*
 * FIXME: move to libos
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

#include <stdint.h>
#include <string.h>

#include <interrupts.h>
#include <bytechan.h>
#include <ns16550.h>
#include <errors.h>
#include <hv.h>

#include <libos/libos.h>
#include <libos/io.h>

typedef struct {
	char *offset;        // Memory map
	int byte_chan;       // Byte channel handler
	int overrun_errors;  // Accumulated number of rx fifo errors
	int rx_fifo_errors;  // Number of rx fifo errors
	int tx_active;       // Transmit active
	int data_bits;       // Number of data bits
	int stop_bits;       // Number of stop bits 1 or 2
	int parity;          // 'n'-None, 'o'-Odd, 'e'-Even, 's'-Space, 'm'-Mark
	int baudrate;        // Baudrate
	int attributes;      // currently not used
} duart_t;

int duart_init(int byte_chan, const uart_param_t *uart_param);
void duart_start_tx(void *lld_handler);

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

void duart_start_tx(void *lld_handler)
{
	duart_t *uart = lld_handler;
	uint8_t intr_status, umsr, ulsr, data;
	uint8_t tmpReg;
	uint32_t i, count = 0;
	int counter = 0;

	//TODO DISABLE ISR UART ISR possible race condition
	if (uart->tx_active != 0)
		return;
	uart->tx_active = 1;

	while (READ_UART(uart, NS16550_LSR) & NS16550_LSR_THRE) {
		if (byte_chan_receive(uart->byte_chan, (char *)&data, 1) == 1) {
			tx_counter++;
			WRITE_UART(uart, NS16550_THR, data);
		} else {
			uart->tx_active = 0;
			WRITE_UART(uart, NS16550_IER, NS16550_IER_ERDAI);
			break;
		}

	}
	//TODO ENABLE ISR
	WRITE_UART(uart, NS16550_IER, NS16550_IER_ERDAI | NS16550_IER_ETHREI);

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

	intr_status = READ_UART(uart, NS16550_IIR);
	intr_status &= NS16550_IIR;

	if (intr_status == 0)
		msr = READ_UART(uart, NS16550_MSR);

	/* Receiver Line Status Error */
	if (intr_status == NS16550_IIR_RLSI) {
		lsr = READ_UART(uart, NS16550_LSR);
		if (lsr & NS16550_LSR_OE)
			uart->overrun_errors;
		if (lsr & NS16550_LSR_RFE)
			uart->rx_fifo_errors;
	}

	/*Either receiver data available or receiver timeout, call store */
	if ((intr_status & (NS16550_IIR_CTOI | NS16550_IIR_RDAI)) != 0) {
		while (1) {
			lsr = READ_UART(uart, NS16550_LSR);
			if (lsr & NS16550_LSR_DR) {
				int ret;
				data = READ_UART(uart, NS16550_RBR);
				rx_counter++;
				ret = byte_chan_send(uart->byte_chan, (char *)&data, 1);
				if (ret < 0)
					uart_err++;
			} else
				break;   
		}
	}

	/* Transmitter holding register empty */
	/* Try to transmit data - if no data available desactivate ISR */
	if ((intr_status & NS16550_IIR_THREI) != 0) {
		/*FIFO mode*/
		while (READ_UART(uart, NS16550_LSR) & NS16550_LSR_THRE) {
			if (byte_chan_receive(uart->byte_chan, (char *)&data, 1) == 1) {
				tx_counter++;
				WRITE_UART(uart, NS16550_THR, data);
			} else {
				/* desactivate ISR and channel */
				/* It will be activated by duart_start_tx call */
				uart->tx_active = 0;
				WRITE_UART(uart, NS16550_IER, NS16550_IER_ERDAI);
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

	if (uart == NULL) {
		return MEM_NOT_ENOUGH;
	}

	if (uart_param == NULL) {
		return NULL_POINTER;
	}

	if ((uart_param->baudrate < 600) || (uart_param->baudrate > 512000))
		return INVALID_PARAM;

	/* copy all the parameters for statistics */
	uart->data_bits  = uart_param->data_bits;
	uart->stop_bits  = uart_param->stop_bits;
	uart->parity     = uart_param->parity;
	uart->baudrate   = uart_param->baudrate;
	uart->attributes = uart_param->attributes;
	uart->offset     = (char*)uart_param->offset;
	uart->byte_chan  = byte_chan;

	byte_chan_reg_params.end_point_handler = uart;
	byte_chan_reg_params.byte_chan_rx_data_avail = duart_start_tx;
	byte_chan_reg_params.byte_chan_tx_space_avail = NULL;
	byte_chan_register(byte_chan, &byte_chan_reg_params);

	/* First calculate a divider */
	divisor = get_system_glock()*1000000 / uart_param->baudrate / 16;

	/* Set access to UDMB/UDLB/UAFR */
	WRITE_UART(uart, NS16550_LCR, NS16550_LCR_DLAB);

	WRITE_UART(uart, NS16550_DMB, (divisor >> 8) & 0xff);
	WRITE_UART(uart, NS16550_DLB, divisor & 0xff);

	/* Building NS16550_LCR register */
	switch (uart_param->parity) {
		case 'n': //Non
			tmp = 0;
			break;
		case 'o': //Odd
			tmp = NS16550_LCR_PEN;
			break;
		case 'e': //Even
			tmp = NS16550_LCR_PEN | NS16550_LCR_EPS;
			break;
		case 's': //Space
			tmp = NS16550_LCR_PEN | NS16550_LCR_EPS | NS16550_LCR_SP;
			break;
		case 'm': //Mark
			tmp = NS16550_LCR_PEN | NS16550_LCR_SP;
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

	WRITE_UART(uart, NS16550_LCR, tmp);

	/* Set NS16550_FCR register and reset FIFOs */
	tmp = NS16550_FCR_FEN | NS16550_FCR_RFR | NS16550_FCR_TFR;
	WRITE_UART(uart, NS16550_FCR, tmp);

	/* Set Ready To Send to 1 */
	//WRITE_UART(uart, NS16550_MCR, NS16550_MCR_RTS | NS16550_MCR_LOOP);
	WRITE_UART(uart, NS16550_MCR, NS16550_MCR_RTS);

	register_critical_handler(0, (int_handler_t)&duart_isr, (uint32_t)uart);

	// FIXME: check return code

	/* Enable interrupts */
	WRITE_UART(uart, NS16550_IER, NS16550_IER_ETHREI);

	return 1;
}
