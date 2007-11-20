#ifndef UV_H
#define UV_H

#include <stddef.h>
#include <stdint.h>
#include <console.h>

#define stopsim() do { \
	asm volatile("mr 22, 22" : : : "memory"); \
	for(;;); \
} while (0)

#define BUG() do { \
	printf("Assertion failure at %s:%d\n", __FILE__, __LINE__); \
	__builtin_trap(); \
} while (0)

#define assert(x) do { if (__builtin_expect(!(x), 0)) BUG(); } while (0)

typedef uint64_t physaddr_t;

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define LONG_BITS (sizeof(long) * 8)

#define max(x, y) ({ \
	typeof(x) _x = (x); \
	typeof(y) _y = (y); \
	_x > _y ? _x : _y; \
})

#define min(x, y) ({ \
	typeof(x) _x = (x); \
	typeof(y) _y = (y); \
	_x < _y ? _x : _y; \
})

void *alloc(unsigned long size, unsigned long align);

#endif
