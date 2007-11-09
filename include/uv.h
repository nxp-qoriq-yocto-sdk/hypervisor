#include <stddef.h>

#define stopsim() do { asm volatile("mr 22, 22" : : : "memory"); } while (0)
