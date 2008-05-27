#ifndef HV_H
#define HV_H

#include <libos/libos.h>

#define FH_API_VERSION 1
#define FH_API_COMPAT_VERSION 1

struct guest;

int start_guest(struct guest *guest);
int stop_guest(struct guest *guest);
phys_addr_t find_lowest_guest_phys(void);

#endif
