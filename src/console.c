
#include "uart.h"
#include "console.h"

void console_init(void)
{
    uart_init();
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
