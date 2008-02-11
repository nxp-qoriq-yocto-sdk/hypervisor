#ifndef HV_H
#define HV_H

#include <libos/libos.h>

void *alloc(unsigned long size, unsigned long align);
void start_guest(void);

#endif
