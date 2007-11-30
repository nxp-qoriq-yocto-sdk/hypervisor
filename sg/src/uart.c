
#include "uart_defs.h"
#include "pio.h"
#include "uart.h"

void uart_init(void)
{
}

#define UART0_OFFSET 0x11d500

void uart_putc(uint8_t c) 
{
	unsigned long addr = 0xf0000000 + UART0_OFFSET + REG_DATA;

	while (!(in8(addr + 5) & 0x20));
	out8(addr, c);
}
