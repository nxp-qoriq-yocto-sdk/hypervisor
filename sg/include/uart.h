
#ifndef _UART_H
#define	_UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(uint8_t c);

#endif  /* _UART_H */
