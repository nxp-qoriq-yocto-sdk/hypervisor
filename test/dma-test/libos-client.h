#ifndef LIBOS_CLIENT_H
#define LIBOS_CLIENT_H

#define CCSRBAR_VA              0xfe000000

#define PHYSBASE 0x20000000
#define BASE_TLB_ENTRY 15

#ifndef _ASM
typedef int client_cpu_t;
#endif

#define EXC_DECR_HANDLER dec_handler
#define EXC_EXT_INT_HANDLER ext_int_handler

#define CONFIG_LIBOS_QUEUE
#define CONFIG_LIBOS_NS16550
#define CONFIG_LIBOS_MAX_BUILD_LOGLEVEL LOGLEVEL_NORMAL
#define CONFIG_LIBOS_DEFAULT_LOGLEVEL LOGLEVEL_NORMAL

#endif
