
/*
 * Copyright (C) 2008-2010 Freescale Semiconductor, Inc.
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

#ifndef __PAMU_H_
#define __PAMU_H_

#include <percpu.h>
#include <devtree.h>
#include <handle.h>

typedef struct pamu_handle {
	unsigned long assigned_liodn;
	handle_t user;
	int no_dma_disable; /* on partition stop or cold boot */

#ifdef CONFIG_CLAIMABLE_DEVICES
	claim_action_t claim_action;
#endif
} pamu_handle_t;

/* define indexes for each operation mapping scenario */
#define OMI_QMAN        0x00
#define OMI_FMAN        0x01
#define OMI_QMAN_PRIV   0x02
#define OMI_CAAM        0x03
#define OMI_MAX         0x03  /* define the max index defined */

void hv_pamu_global_init(void);
void hv_pamu_partition_init(guest_t *guest);

int hv_pamu_enable_liodn(unsigned int liodn);
int hv_pamu_disable_liodn(unsigned int liodn);
int hv_pamu_config_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode, dt_node_t *cfgnode, dt_node_t *gnode);
int hv_pamu_reconfig_liodn(guest_t *guest, uint32_t liodn, dt_node_t *hwnode);

void hcall_dma_attr_set(trapframe_t *regs);
void hcall_dma_attr_get(trapframe_t *regs);

#endif

