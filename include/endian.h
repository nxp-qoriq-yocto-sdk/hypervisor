#ifndef ENDIAN_H
#define ENDIAN_H

#include <stdint.h>

static inline uint16_t swap16(uint16_t val)
{
	return (uint16_t)val | (((uint16_t)val >> 8) << 8);
}

static inline uint32_t swap32(uint32_t val)
{
	return ((val & 0x000000ff) << 24) |
	       ((val & 0x0000ff00) <<  8) |
	       ((val & 0x00ff0000) >>  8) |
	       ((val & 0xff000000) >> 24);
}

static inline uint64_t swap64(uint64_t val)
{
	return (uint64_t)swap32(val) | ((uint64_t)swap32(val >> 32) << 32);
}

#ifdef _BIG_ENDIAN
#define cpu_to_le16 swap16
#define cpu_to_le32 swap32
#define cpu_to_le64 swap64
#define cpu_to_be16(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be64(x) (x)
#define cpu_from_le16 swap16
#define cpu_from_le32 swap32
#define cpu_from_le64 swap64
#define cpu_from_be16(x) (x)
#define cpu_from_be32(x) (x)
#define cpu_from_be64(x) (x)
#elif defined(_LITTLE_ENDIAN)
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define cpu_to_be16 swap16
#define cpu_to_be32 swap32
#define cpu_to_be64 swap64
#define cpu_from_le16(x) (x)
#define cpu_from_le32(x) (x)
#define cpu_from_le64(x) (x)
#define cpu_from_be16 swap16
#define cpu_from_be32 swap32
#define cpu_from_be64 swap64
#else
#error Please specify endianness.
#endif

#endif
