#include <stddef.h>
#include <libos/assym.h>

#include <percpu.h>
#include <libos-client.h>

#ifdef CONFIG_TLB_CACHE
ASSYM(TLB_MISS_COUNT, offsetof(gcpu_t, stats[stat_tlb_miss_count]));
#endif
ASSYM(CLIENT_GCPU, offsetof(client_cpu_t, gcpu));
