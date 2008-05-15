#ifndef HV_H
#define HV_H

#include <libos/libos.h>

struct guest;

int start_guest(struct guest *guest);
int stop_guest(struct guest *guest);

#endif
