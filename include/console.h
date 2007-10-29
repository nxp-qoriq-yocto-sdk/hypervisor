#ifndef _CONSOLE_H
#define	_CONSOLE_H

#include <stdint.h>

void printh(unsigned char *s);
void puts_len(char *s, int len);
size_t printf(const char *str, ...);

#endif  /* _CONSOLE_H */
