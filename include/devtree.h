#ifndef HV_DEVTREE_H
#define HV_DEVTREE_H

#include <libos/libos.h>
#include <libfdt.h>

// Error values that will not conflict with FDT errors
#define BADTREE -256
#define NOMEM -257
#define NOTRANS -258

#define MAX_ADDR_CELLS 4
#define MAX_SIZE_CELLS 2

int get_addr_format(const void *tree, int node,
                    uint32_t *naddr, uint32_t *nsize);

void *ptr_from_node(const void *devtree, int offset, const char *type);
int lookup_alias(const void *tree, const char *path);

physaddr_t find_end_of_mem(void);

int xlate_one(uint32_t *addr, const uint32_t *ranges,
              int rangelen, uint32_t naddr, uint32_t nsize,
              uint32_t prev_naddr, uint32_t prev_nsize,
              physaddr_t *rangesize);

int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                  uint32_t *addrbuf, uint32_t *rootnaddr,
                  physaddr_t *size);

static inline void val_from_int(uint32_t *dest, physaddr_t src)
{
	dest[0] = 0;
	dest[1] = 0;
	dest[2] = src >> 32;
	dest[3] = (uint32_t)src;
}

void create_ns16550(void);
void open_stdout(void);

extern void *fdt;

#endif
