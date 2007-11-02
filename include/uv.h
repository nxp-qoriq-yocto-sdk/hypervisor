#include <stddef.h>

#define stopsim() do { asm volatile("mr 2, 2" : : : "memory"); } while (0)
