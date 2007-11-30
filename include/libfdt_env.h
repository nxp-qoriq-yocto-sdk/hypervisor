#ifndef _LIBFDT_ENV_H
#define _LIBFDT_ENV_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>

#define fdt32_to_cpu cpu_from_be32
#define cpu_to_fdt32 cpu_to_be32
#define fdt64_to_cpu cpu_from_be64
#define cpu_to_fdt64 cpu_to_be64

#endif /* _LIBFDT_ENV_H */
