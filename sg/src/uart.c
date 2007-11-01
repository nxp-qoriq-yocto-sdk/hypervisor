
#include "uart_defs.h"
#include "pio.h"
#include "console.h"

/*
 * Note: this is a hack for now
 *
 */


void uart_init(void)
{

}


void uart_putc(uint8_t c) 
{

    unsigned long addr = 0xf0000000 + 0x11c100 + REG_DATA;

    out8(addr,c);
    
}

void printh(unsigned char *s)
{

    if (s == 0)
        return;

    while (*s != 0) {
        uart_putc(*s);
        if (*s == '\n') {
            uart_putc('\r');
        }
        s++;
    }

}
