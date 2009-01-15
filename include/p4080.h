
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

#ifndef HV_P4080_H
#define HV_P4080_H

#define NUMLAWS 32

#define LAW_ENABLE 0x80000000
#define LAWAR_TARGETID_SHIFT 20
#define LAWAR_CSDID_SHIFT 12
#define LAW_TARGETID_MASK 0x0ff00000
#define LAW_SIZE_MASK 0x3f

#define LAW_SIZE_4KB 11
#define LAW_SIZE_64GB 35

/* Define LAW targets here */
#define LAW_TRGT_DDR1	0x10
#define LAW_TRGT_DDR2	0x11
#define LAW_TRGT_INTLV	0x14

typedef struct {
	uint32_t high, low, attr, reserved;
} law_t;

#define NUMCSDS 32

#define NUMCPCS 2
#define NUMCPCWAYS 32

#define CPCADDR0 0x10000
#define CPCADDR1 0x11000

#define CPCPIR0 0x200
#define CPCPIR0_RESET_MASK 0xFFFFFFFF
#define CPCPAR0 0x208
#define CPCPWR0 0x20C

/*IRDALLOC, DRDALLOC, WCOALLOC, READUALLOC, WRITEUALLOC, ADSTASHEN, CNSTASHEN */
#define CPCPAR_MASK 0xFB

typedef struct cpc_part_reg {
	uint32_t cpcpir, reserved, cpcpar, cpcpwr;
} cpc_part_reg_t;

#define NUM_PART_REGS 16

typedef struct cpc_dev {
	uint32_t cpc_way_map, cpc_dev_lock;
	unsigned long cpc_reg_map;
	struct cpc_part_reg *cpc_part_base;
} cpc_dev_t;


#endif
