/*-
 * Copyright (C) 2006 Semihalf, Marian Balakowicz <m8@semihalf.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

#ifndef	_MACHINE_TLB_H_
#define	_MACHINE_TLB_H_


/*  PowerPC E500 MAS registers */
#define MAS0_TLBSEL(x)		((x << 28) & 0x10000000)
#define MAS0_ESEL(x)		((x << 16) & 0x000F0000)

#define MAS0_TLBSEL1		0x10000000
#define MAS0_TLBSEL0		0x00000000
#define MAS0_ESEL_TLB1MASK	0x000F0000
#define MAS0_ESEL_TLB0MASK	0x00030000
#define MAS0_ESEL_SHIFT		16
#define MAS0_NV_MASK		0x00000003
#define MAS0_NV_SHIFT		0
#define MAS0_RESERVED           0xe000f000

#define MAS1_VALID		0x80000000
#define MAS1_IPROT		0x40000000
#define MAS1_TID_MASK		0x00FF0000
#define MAS1_TID_SHIFT		16
#define MAS1_TS			0x00001000
#define MAS1_TSIZE_MASK		0x00000F00
#define MAS1_TSIZE_SHIFT	8
#define MAS1_RESERVED           0x0000e0ff

#define	TLB_SIZE_4K		1
#define	TLB_SIZE_16K		2
#define	TLB_SIZE_64K		3
#define	TLB_SIZE_256K		4
#define	TLB_SIZE_1M		5
#define	TLB_SIZE_4M		6
#define	TLB_SIZE_16M		7
#define	TLB_SIZE_64M		8
#define	TLB_SIZE_256M		9
#define	TLB_SIZE_1G		10
#define	TLB_SIZE_4G		11

#define	MAS2_EPN		0xFFFFF000
#define	MAS2_EPN_SHIFT		12
#define	MAS2_X0			0x00000040
#define	MAS2_X1			0x00000020
#define	MAS2_W			0x00000010
#define	MAS2_I			0x00000008
#define	MAS2_M			0x00000004
#define	MAS2_G			0x00000002
#define	MAS2_E			0x00000001
#define MAS2_RESERVED           0x00000f00

#define	MAS3_RPN		0xFFFFF000
#define	MAS3_RPN_SHIFT		12
#define	MAS3_U0			0x00000200
#define	MAS3_U1			0x00000100
#define	MAS3_U2			0x00000080
#define	MAS3_U3			0x00000040
#define	MAS3_UX			0x00000020
#define	MAS3_SX			0x00000010
#define	MAS3_UW			0x00000008
#define	MAS3_SW			0x00000004
#define	MAS3_UR			0x00000002
#define	MAS3_SR			0x00000001
#define MAS3_RESERVED           0x00000c00

#define MAS4_TLBSELD1		0x10000000
#define MAS4_TLBSELD0		0x00000000
#define MAS4_TIDSELD_MASK	0x00030000
#define MAS4_TIDSELD_SHIFT	16
#define MAS4_TSIZED_MASK	0x00000F00
#define MAS4_TSIZED_SHIFT	8
#define MAS4_X0D		0x00000040
#define MAS4_X1D		0x00000020
#define MAS4_WD			0x00000010
#define MAS4_ID			0x00000008
#define MAS4_MD			0x00000004
#define MAS4_GD			0x00000002
#define MAS4_ED			0x00000001

#define MAS5_SGS                0x80000000 /* Search GS */
#define MAS5_SLPID              0x000000ff /* Search LPID */

#define MAS6_SPID0_MASK		0x00FF0000
#define MAS6_SPID0_SHIFT	16
#define MAS6_SAS		0x00000001
#define MAS6_RESERVED           0xf000fffe

#define MAS7_RPN                0x0000000f
#define MAS7_RESERVED           0xfffffff0

#define MAS8_GTS_SHIFT		31
#define MAS8_GTS_MASK		0x80000000
#define MAS8_VF                 0x40000000 /* Virtualization Fault */
#define MAS8_TLPID              0x000000ff

#define MAS1_GETTID(mas1)	(((mas1) & MAS1_TID_MASK) >> MAS1_TID_SHIFT)

#define MAS2_TLB0_ENTRY_IDX_MASK	0x0007f000
#define MAS2_TLB0_ENTRY_IDX_SHIFT	12

#define _TLB_ENTRY_IO	(MAS2_I | MAS2_G)
#define _TLB_ENTRY_MEM	(0)

#define UV_TID	0	/* TLB TID to use for UV translations */

#define TLBIVAX_RESERVED        0xfe3

#if !defined(_ASM)

#include <stdint.h>
typedef struct tlb_entry {
	uint32_t mas1;
	uint32_t mas2;
	uint32_t mas3;
	uint32_t mas8;
} tlb_entry_t;

void __tlb1_set_entry(unsigned int idx, uint32_t va, uint32_t pa, uint32_t size,
              uint32_t flags, unsigned int _tid, unsigned int _ts, unsigned int _gs);


#endif

#endif	/* _MACHINE_TLB_H_ */