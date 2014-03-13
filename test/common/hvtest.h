/*
 * Copyright (C) 2009-2011 Freescale Semiconductor, Inc.
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

#ifndef HVTEST_H
#define HVTEST_H

#include <libos/types.h>

void init(unsigned long devtree_ptr);
int release_secondary_cores(void);

extern void (*secondary_startp)(void);
extern void *fdt;
extern int coreint;
extern int guest_error_queue;
extern int global_error_queue;

extern phys_addr_t uart_addr;
extern uint8_t *uart_virt;

int get_addr_format(const void *tree, int node,
                    uint32_t *naddr, uint32_t *nsize);
int get_addr_format_nozero(const void *tree, int node,
                           uint32_t *naddr, uint32_t *nsize);

void copy_val(uint32_t *dest, const uint32_t *src, int naddr);

int xlate_one(uint32_t *addr, const uint32_t *ranges,
              int rangelen, uint32_t naddr, uint32_t nsize,
              uint32_t prev_naddr, uint32_t prev_nsize,
              phys_addr_t *rangesize);

int xlate_reg_raw(const void *tree, int node, const uint32_t *reg,
                  uint32_t *addrbuf, phys_addr_t *size,
                  uint32_t naddr, uint32_t nsize);

int xlate_reg(const void *tree, int node, const uint32_t *reg,
              phys_addr_t *addr, phys_addr_t *size);

int dt_get_reg(const void *tree, int node, int res,
               phys_addr_t *addr, phys_addr_t *size);

struct chardev *test_init_uart(int node);
const char *get_bootargs(void);
int get_vmpic_irq(int node, int irq);
int set_vmpic_irq_priority(int handle, int prio);
int init_error_queues(void);
int get_number32(const char *numstr, uint32_t *num);
char *nextword(char **str);

uint32_t dt_get_timebase_freq(void);

static inline void delay_timebase(uint64_t ticks)
{
	uint64_t start = get_tb();

	while (get_tb() - start < ticks);
}

static inline void delay_ms(unsigned long ms)
{
	delay_timebase((uint64_t)ms * dt_get_timebase_freq() / 1000);
}

#endif
