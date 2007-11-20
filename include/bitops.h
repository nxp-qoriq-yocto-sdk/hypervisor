#ifndef BITOPS_H
#define BITOPS_H

#include <uv.h>

// Returns non-zero if the operation succeeded.
static inline int compare_and_swap(unsigned long *ptr,
                                   unsigned long old,
                                   unsigned long new)
{
	unsigned long ret;

	// FIXME 64-bit
	asm volatile("1: lwarx %0, 0, %1;"
	             "cmpw %0, %2;"
	             "bne 2f;"
	             "stwcx. %3, 0, %1;"
	             "bne 1b;"
	             "2:" :
	             "=&r" (ret) :
	             "r" (ptr), "r" (old), "r" (new) :
	             "memory");

	return ret == old;
}

static inline unsigned long atomic_or(unsigned long *ptr, unsigned long val)
{
	unsigned long ret;

	// FIXME 64-bit
	asm volatile("1: lwarx %0, 0, %1;"
	             "ori %0, %0, %2;"
	             "stwcx. %0, 0, %1;"
	             "bne 1b;" :
	             "=&r" (ret) :
	             "r" (ptr), "r" (val) :
	             "memory");

	return ret;
}

static inline unsigned long atomic_and(unsigned long *ptr, unsigned long val)
{
	unsigned long ret;

	// FIXME 64-bit
	asm volatile("1: lwarx %0, 0, %1;"
	             "andi %0, %0, %2;"
	             "stwcx. %0, 0, %1;"
	             "bne 1b;" :
	             "=&r" (ret) :
	             "r" (ptr), "r" (val) :
	             "memory");

	return ret;
}

static inline int count_msb_zeroes(unsigned long val)
{
	int ret;

	// FIXME 64-bit
	asm("cntlzw %0, %1" : "=r" (ret) : "r" (val));
	return ret;
}

static inline int count_lsb_zeroes(unsigned long val)
{
	return LONG_BITS - 1 - count_msb_zeroes(val & ~(val - 1));
}

#endif
