#ifndef HV_H
#define HV_H

#include <libos/libos.h>

struct guest;

void *alloc(unsigned long size, unsigned long align);
int start_guest(struct guest *guest);
int stop_guest(struct guest *guest);

#endif
