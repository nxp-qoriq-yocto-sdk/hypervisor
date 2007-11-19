#ifndef UV_H
#define UV_H

#include <stddef.h>
#include <console.h>

#define stopsim() do { asm volatile("mr 22, 22" : : : "memory"); } while (0)

#define BUG() do { \
	printf("Assertion failure at %s:%d\n", __FILE__, __LINE__); \
	__builtin_trap(); \
} while (0)

#define assert(x) do { if (__builtin_expect(!(x), 0)) BUG(); } while (0)

#endif
