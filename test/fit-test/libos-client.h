#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define CCSRBAR_VA 0xfe000000

#define PHYSBASE 0x20000000
#define BASE_TLB_ENTRY 15

#ifndef _ASM
typedef int client_cpu_t;
#endif

#define EXC_FIT_HANDLER fit_handler

#define CONFIG_LIBOS_QUEUE
#define CONFIG_LIBOS_NS16550

#endif
