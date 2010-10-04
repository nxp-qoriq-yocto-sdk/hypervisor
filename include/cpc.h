/*
 * Copyright (C) 2009,2010 Freescale Semiconductor, Inc.
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

#ifndef CPC_H
#define CPC_H

struct dt_node;
struct dt_prop;

typedef struct cpc_part_reg {
	uint32_t cpcpir, reserved, cpcpar, cpcpwr;
} cpc_part_reg_t;

typedef struct cpc_err_reg {
	uint32_t cpcerrdet;
	uint32_t cpcerrdis;
	uint32_t cpcerrinten;
	uint32_t cpcerrattr;
	uint32_t cpcerreaddr;
	uint32_t cpcerraddr;
	uint32_t cpcerrctl;
} cpc_err_reg_t;

typedef struct cpc_dev {
	unsigned long cpc_reg_map;
	uint32_t *cpccsr0;
	struct cpc_err_reg *cpc_err_base;
	struct cpc_part_reg *cpc_part_base;
	struct dt_node *cpc_node;
} cpc_dev_t;

/* Derived from available DDR targets for P4080 */
typedef enum mem_tgts {
	mem_tgt_ddr1,
	mem_tgt_ddr2,
	max_num_mem_tgts
} mem_tgts_t;

void allocate_cpc_ways(struct dt_prop *prop, uint32_t tgt, uint32_t csdid, struct dt_node *node);
int cpcs_enabled(void);

#endif
