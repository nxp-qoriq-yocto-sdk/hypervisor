#ifndef STDINT_H
#define STDINT_H

#include <stddef.h>

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef char                int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif

#ifndef _INTPTR_T
#define _INTPTR_T
typedef long intptr_t;
#endif

#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef unsigned long uintptr_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long ptrdiff_t;
#endif

#endif
