
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HV_DEVTREE_H
#define HV_DEVTREE_H

#include <libos/libos.h>
#include <libfdt.h>

#define MAX_ADDR_CELLS 4
#define MAX_SIZE_CELLS 2
#define MAX_INT_CELLS 4

#define CELL_SIZE 4

int get_addr_format(const void *tree, int node,
                    uint32_t *naddr, uint32_t *nsize);
int get_addr_format_nozero(const void *tree, int node,
                           uint32_t *naddr, uint32_t *nsize);

void *ptr_from_node(const void *devtree, int offset, const char *type);
int lookup_alias(const void *tree, const char *path);
int get_interrupt_domain(const void *tree, int node);
int get_interrupt(const void *tree, int node, int intnum,
                  const uint32_t **intspec, int *ncells);
int get_num_interrupts(const void *tree, int node);

phys_addr_t find_memory(void);

int xlate_one(uint32_t *addr, const uint32_t *ranges,
              int rangelen, uint32_t naddr, uint32_t nsize,
              uint32_t prev_naddr, uint32_t prev_nsize,
              phys_addr_t *rangesize);

int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                  uint32_t *addrbuf, uint32_t *rootnaddr,
                  phys_addr_t *size);

int dt_get_reg(const void *tree, int node, int res,
               phys_addr_t *addr, phys_addr_t *size);

void copy_val(uint32_t *dest, const uint32_t *src, int naddr);

static inline void val_from_int(uint32_t *dest, phys_addr_t src)
{
	dest[0] = 0;
	dest[1] = 0;
	dest[2] = src >> 32;
	dest[3] = (uint32_t)src;
}

void create_ns16550(void);
void open_stdout(void);
void open_stdin(void);

extern void *fdt;
extern struct queue *stdin, *stdout;

int fdt_next_descendant_by_compatible(const void *fdt, int offset, int *depth, const char *compatible);
#endif
