
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


void uart_putc(uint8_t c) 
{

    unsigned long addr = 0xf0000000 + 0x4500 + REG_DATA;

    out8(addr,c);
    
}
