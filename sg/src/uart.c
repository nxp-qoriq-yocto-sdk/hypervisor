
#include "uart_defs.h"
#include "pio.h"
#include "uart.h"

/*
 * Note: this is a hack for now
 *
 */


void uart_init(void)
{

}

#define UART1_OFFSET 0x11c600


void uart_putc(uint8_t c) 
{

    unsigned long addr = 0xf0000000 + UART1_OFFSET + REG_DATA;

    out8(addr,c);
    
}
