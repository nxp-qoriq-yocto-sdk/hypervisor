#ifndef UV_H
#define UV_H

#include <libos/libos.h>

void *alloc(unsigned long size, unsigned long align);
void start_guest(void);
extern void *fdt;

int get_addr_format(const void *tree, int node,
                    uint32_t *naddr, uint32_t *nsize);

#endif
