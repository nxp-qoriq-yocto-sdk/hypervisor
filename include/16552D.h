
/*!
 *  @file 16552D.h
 *
 *  @brief  This file contain the implementation of general byte_chan interface
 *  The byte_chan interface usually implements 
 *
 *  The byte_chan generic interface is a user accessible layer that is responcible primarily 
 *  for data bufferization. The idea is to separate a LLD (Low Level Driver) and
 *  a higher level, more OS dependent layer.
 *
 */

 /* (c) Copyright Freescale 2008, All Rights Reserved */

#ifndef F16552D_H_
#define F16552D_H_

/*! byte_chans array and a next free pointer */

#define RBR  0x00 //UART receiver buffer register
#define THR  0x00 //UART transmitter holding register
#define DLB  0x00 //UART divisor least significant byte register	
#define IER  0x01 //UART interrupt enable register
#define DMB  0x01 //UART divisor most significant byte register 
#define IIR  0x02 //UART interrupt ID register
#define FCR  0x02 //UART FIFO control register
#define AFR  0x02 //UART alternate function register
#define LCR  0x03 //UART line control register 
#define MCR  0x04 //UART modem control register
#define LSR  0x05 //UART line status register
#define MSR  0x06 //UART modem status register 
#define SCR  0x07 //UART scratch register 
#define DSR  0x10 //UART DMA status register					 

/*! UIIR (Interrupt Identification Register) */
#define	IIR_RLSI	0x06		/* priority 1 - receiver line status */
#define	IIR_RDAI	0x04		/* priority 2 - receiver data available*/
#define	IIR_THREI	0x02		/* priority 3 - transmitter holding register empty */
#define	IIR_MSI		0x00		/* priority 4 - modem status */
#define	IIR_CTOI	0x08		/* priority 2 - character time-out*/

/** ULSR (Line Status Register) */
#define	LSR_RFE		0x80
#define	LSR_OE		0x20
#define	LSR_DR		0x01	   	/* 1: Data ready */
#define	LSR_THRE	0x20		/* 1: transmitter holding register empty */
#define	LSR_TEMT	0x40		/* 1: transmitter empty */

/*! UMCR (Modem Control Register) */
#define	MCR_RTS	0x02		/* 1: Ready to send */
#define	MCR_LOOP	0x10		/* 1: Loop enable */

/*! UMSR (Modem Status Register) */
#define	MSR_CTS		0x10		/* CTS status */

/*! UIER (Interrupt Enable Register) */
#define	IER_ERDAI	0x01		/* 1: enable received data available int. */
#define	IER_ETHREI	0x02		/* 1: enable transmitter holding register empty int. */
#define	IER_ERLSI	0x04		/* 1: enable receiver line status interrupt */
#define	IER_EMSI	0x08		/* 1: enable modem status interrput */

/*! ULCR (Line Contorl Register) */
#define	LCR_NTSB	0x04		/* 1: number of stop bits */
#define	LCR_EPS		0x10		/* 1: Even parity selected */
#define	LCR_PEN		0x08		/* 1: Parity enable */
#define	LCR_SP	    0x20		/* 1: Stick parity */
#define	LCR_DLAB	0x80		/* 1: Divisor latch access bit */

/*! UFCR (FIFO Control Register) - write only */
#define	FCR_FEN		0x01		/* 1: Enable FIFO */
#define	FCR_RFR		0x02		/* 1: Receiver FIFO reset */
#define	FCR_TFR		0x04		/* 1: Transmitter FIFO clear */
#define	FCR_DMS		0x08		/* 1: DMA mode select */

/*! UDSR (DMA Status Register)*/
#define DSR_RXRDY   0x01        /* 1: Receiver ready, status depends on DMA mode selected*/
#define DSR_TXRDY   0x02        /* 1: Tranmitter ready, status depends on DMA mode selected */

#define DUART_FIFO_SIZE 16

#define DUART_B_OFFSET  0x100 /* offset between uart0 and uart1 */
#define DUART_INTR_MASK 0xE   /* mask to get relavent bits in the DUART interrupt status register */

typedef struct duart_s {
	char* offset;        /* Memory map */
	int byte_chan;       /* Byte channel handler */
	int overrun_errors;  /* Accumulated number of rx fifo errors */
	int rx_fifo_errors;  /* Number of rx fifo errors */
	int tx_active;       /* Transmit active */
	int data_bits ;      /* Number of data bits */   
	int stop_bits ;      /* Number of stop bits 1 or 2 */
	int parity    ;      /*'n'-Non, 'o'-Odd, 'e'-Even, 's'-Space, 'm'-Mark */
	int baudrate  ;      /* Baudrate */
	int attributes;      /* currently not used */
} duart_t;

typedef struct uart_param_s {
	int data_bits ;
	int stop_bits ;
	int parity    ; 
	int baudrate  ;
	int attributes;
	unsigned int offset;
	int hwi_num;
} uart_param_t;

int duart_init(int  byte_chan,uart_param_t const *uart_param);
void duart_start_tx(void* lld_handler);

#endif
