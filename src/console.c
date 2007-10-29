#include <stdarg.h>
#include <string.h>
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

void puts_len(char *s, int len)
{
    while (*s && len--) {
        if (*s == '\n')
            uart_putc('\r');

        uart_putc(*s++);
    }
}

size_t printf(const char *str, ...)
{
	// FIXME: lock buffer

	enum {
		buffer_size = 4096,
	};

	static char buffer[buffer_size];

	va_list args;
	va_start(args, str);
	size_t ret = vsnprintf(buffer, buffer_size, str, args);
	va_end(args);
	
	if (ret > buffer_size)
		ret = buffer_size;
	
	puts_len(buffer, ret);
	return ret;
}
