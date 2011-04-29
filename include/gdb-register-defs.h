/** @file
 * (Register definitions for GDB Stub and target description generator).
 */

/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* References:
 * 0. e500mc Core Reference Manual: Programming Model
 *    E500MCRM, Rev. C, 09/2008
 * 1. E500_mc Changes #2 Version 1.0 (6/04/08)
 * 2. E500_mc Changes #2 Version 3.0 (6/20/08)
 * 3. Freescale P4080 Embedded Hypervisor Software Architecture
 *    Rev. 0.85 1/10/2009
 * 4. PowerPC Register Definition Dumper for P4080 REV 2, Version 4_2_6
 * 5. Changes from e500v4 (e500mc) to e500v5 (v1.1)
 */

#include <libos/core-regs.h>

/* We define _ASM because we want the upper "non-C" part of
 * libos/fsl-booke-tlb.h (No assembly language connotation here unlike
 * in other parts of the hypervisor where _ASM could be defined by an
 * assembly language file to indicate that it needs to include the "non-C"
 * part of libos/fsl-booke-tlb.h).
 */
#define _ASM
#include <libos/fsl-booke-tlb.h>

#include "gdb-td-defs.h"

static arch_mask_t arch_mask_table[arch_count] =
{
	[e500mc] = E500MC,
	[e5500]  = E5500,
};

struct register_description power_core_gprs[] =
{
	{
		.arches = E500MC | E5500,
		.name = "r0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 0,
	},
	{
		.arches = E500MC | E5500,
		.name = "r1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 1,
	},
	{
		.arches = E500MC | E5500,
		.name = "r2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 2,
	},
	{
		.arches = E500MC | E5500,
		.name = "r3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 3,
	},
	{
		.arches = E500MC | E5500,
		.name = "r4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 4,
	},
	{
		.arches = E500MC | E5500,
		.name = "r5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 5,
	},
	{
		.arches = E500MC | E5500,
		.name = "r6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 6,
	},
	{
		.arches = E500MC | E5500,
		.name = "r7",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 7,
	},
	{
		.arches = E500MC | E5500,
		.name = "r8",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 8,
	},
	{
		.arches = E500MC | E5500,
		.name = "r9",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 9,
	},
	{
		.arches = E500MC | E5500,
		.name = "r10",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 10,
	},
	{
		.arches = E500MC | E5500,
		.name = "r11",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 11,
	},
	{
		.arches = E500MC | E5500,
		.name = "r12",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 12,
	},
	{
		.arches = E500MC | E5500,
		.name = "r13",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 13,
	},
	{
		.arches = E500MC | E5500,
		.name = "r14",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 14,
	},
	{
		.arches = E500MC | E5500,
		.name = "r15",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 15,
	},
	{
		.arches = E500MC | E5500,
		.name = "r16",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 16,
	},
	{
		.arches = E500MC | E5500,
		.name = "r17",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 17,
	},
	{
		.arches = E500MC | E5500,
		.name = "r18",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 18,
	},
	{
		.arches = E500MC | E5500,
		.name = "r19",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 19,
	},
	{
		.arches = E500MC | E5500,
		.name = "r20",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 20,
	},
	{
		.arches = E500MC | E5500,
		.name = "r21",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 21,
	},
	{
		.arches = E500MC | E5500,
		.name = "r22",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 22,
	},
	{
		.arches = E500MC | E5500,
		.name = "r23",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 23,
	},
	{
		.arches = E500MC | E5500,
		.name = "r24",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 24,
	},
	{
		.arches = E500MC | E5500,
		.name = "r25",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 25,
	},
	{
		.arches = E500MC | E5500,
		.name = "r26",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 26,
	},
	{
		.arches = E500MC | E5500,
		.name = "r27",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 27,
	},
	{
		.arches = E500MC | E5500,
		.name = "r28",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 28,
	},
	{
		.arches = E500MC | E5500,
		.name = "r29",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 29,
	},
	{
		.arches = E500MC | E5500,
		.name = "r30",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 30,
	},
	{
		.arches = E500MC | E5500,
		.name = "r31",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_gpr,
		.inum = 31,
	},
};

struct register_description power_fpu_fprs[] =
{
	{
		.arches = E500MC | E5500,
		.name = "f0",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.regnum = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.cat = reg_cat_fpr,
		.inum = 0,
	},
	{
		.arches = E500MC | E5500,
		.name = "f1",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 1,
	},
	{
		.arches = E500MC | E5500,
		.name = "f2",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 2,
	},
	{
		.arches = E500MC | E5500,
		.name = "f3",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 3,
	},
	{
		.arches = E500MC | E5500,
		.name = "f4",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 4,
	},
	{
		.arches = E500MC | E5500,
		.name = "f5",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 5,
	},
	{
		.arches = E500MC | E5500,
		.name = "f6",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 6,
	},
	{
		.arches = E500MC | E5500,
		.name = "f7",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 7,
	},
	{
		.arches = E500MC | E5500,
		.name = "f8",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 8,
	},
	{
		.arches = E500MC | E5500,
		.name = "f9",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 9,
	},
	{
		.arches = E500MC | E5500,
		.name = "f10",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 10,
	},
	{
		.arches = E500MC | E5500,
		.name = "f11",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 11,
	},
	{
		.arches = E500MC | E5500,
		.name = "f12",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 12,
	},
	{
		.arches = E500MC | E5500,
		.name = "f13",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 13,
	},
	{
		.arches = E500MC | E5500,
		.name = "f14",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 14,
	},
	{
		.arches = E500MC | E5500,
		.name = "f15",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 15,
	},
	{
		.arches = E500MC | E5500,
		.name = "f16",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 16,
	},
	{
		.arches = E500MC | E5500,
		.name = "f17",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 17,
	},
	{
		.arches = E500MC | E5500,
		.name = "f18",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 18,
	},
	{
		.arches = E500MC | E5500,
		.name = "f19",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 19,
	},
	{
		.arches = E500MC | E5500,
		.name = "f20",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 20,
	},
	{
		.arches = E500MC | E5500,
		.name = "f21",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 21,
	},
	{
		.arches = E500MC | E5500,
		.name = "f22",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 22,
	},
	{
		.arches = E500MC | E5500,
		.name = "f23",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 23,
	},
	{
		.arches = E500MC | E5500,
		.name = "f24",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 24,
	},
	{
		.arches = E500MC | E5500,
		.name = "f25",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 25,
	},
	{
		.arches = E500MC | E5500,
		.name = "f26",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 26,
	},
	{
		.arches = E500MC | E5500,
		.name = "f27",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 27,
	},
	{
		.arches = E500MC | E5500,
		.name = "f28",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 28,
	},
	{
		.arches = E500MC | E5500,
		.name = "f29",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 29,
	},
	{
		.arches = E500MC | E5500,
		.name = "f30",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 30,
	},
	{
		.arches = E500MC | E5500,
		.name = "f31",
		.bitsize = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "ieee_double",
			[e5500]  = "ieee_double",
		},
		.cat = reg_cat_fpr,
		.inum = 31,
	},
};

struct register_description power_core_pc[] =
{
	{
		.arches = E500MC | E5500,
		.name = "pc",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "code_ptr",
			[e5500]  = "code_ptr",
		},
		.regnum = {
			[e500mc] = "64",
			[e5500]  = "64",
		},
		.cat = reg_cat_pc,
	},
};

struct register_description power_core_msr[] =
{
{
		.arches = E500MC | E5500,
		.name = "msr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint32",
		},
		.cat = reg_cat_msr,
	},
};

struct register_description power_core_cr[] =
{
{
		.arches = E500MC | E5500,
		.name = "cr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint32",
		},
		.cat = reg_cat_cr,
	},
};

struct register_description power_core_sprs[] =
{
	{
		.arches = E500MC | E5500,
		.name = "lr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "code_ptr",
			[e5500]  = "code_ptr",
		},
		.cat = reg_cat_spr,
		.inum = SPR_LR,
	},
{
		.arches = E500MC | E5500,
		.name = "ctr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint64",
		},
		.cat = reg_cat_spr,
		.inum = SPR_CTR,
	},
	{
		.arches = E500MC | E5500,
		.name = "xer",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint32",
		},
		.cat = reg_cat_spr,
		.inum = SPR_XER,
	},
};

struct register_description power_fpu_fpscr[] =
{
{
		.arches = E500MC | E5500,
		.name = "fpscr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.type = {
			[e500mc] = "uint32",
			[e5500]  = "uint32",
		},
		.group = "float",
		.regnum = {
			[e500mc] = "70",
			[e5500]  = "70",
		},
		.cat = reg_cat_fpscr,
		/* FPSCR is unique - so no .inum */
	},
};

struct register_description sprs[] =
{
	{
		.arches = E500MC | E5500,
		.description = "Alternate Time Base Register Lower",
		.name = "atbl",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.regnum = {
			[e500mc] = "71",
			[e5500]  = "71",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_ATBL,
	},
	{
		.arches = E500MC | E5500,
		.description = "Alternate Time Base Register Upper",
		.name = "atbu",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_ATBU,
	},
	{
		.arches = E500MC | E5500,
		.description = "Branch Unit Control and Status Register",
		.name = "bucsr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_BUCSR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Core Device Control and Status Register 0",
		.name = "cdcsr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_CDCSR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Critical Save/Restore Register 0",
		.name = "csrr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_CSRR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Critical Save/Restore Register 1",
		.name = "csrr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_CSRR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Data Address Compare 1",
		.name = "dac1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DAC1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Data Address Compare 2",
		.name = "dac2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DAC2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Control Register 0",
		.name = "dbcr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBCR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Control Register 1",
		.name = "dbcr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBCR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Control Register 2",
		.name = "dbcr2",
		.bitsize = {
			[e500mc] ="32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBCR2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Control Register 4",
		.name = "dbcr4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBCR4,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Status Register",
		.name = "dbsr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBSR,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Debug Status Register Write Register",
		.name = "dbsrwr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DBSRWR,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Debug Data Acquisition Message",
		.name = "ddam",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DDAM,
	},
	{
		.arches = E500MC | E5500,
		.description = "Data Exception Address Register",
		.name = "dear",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DEAR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Decrementer",
		.name = "dec",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DEC,
	},
	{
		.arches = E500MC | E5500,
		.description = "Decrementer Auto-Reload",
		.name = "decar",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DECAR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Event Select Register",
		.name = "devent",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DEVENT,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Save/Restore Register 0",
		.name = "dsrr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DSRR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Debug Save/Restore Register 1",
		.name = "dsrr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_DSRR1,
	},
	{
		/* epcr is a special case - it is guest-visible in 64-bit,
		 * but not in 32-bit. We treat it as "guest-visible" here
		 * in 32-bit, but it is accessible by GDB only.
		 */
		.arches = E500MC | E5500,
		.description = "Embedded Processor Control Register",
		.name = "epcr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_EPCR,
	},
	{
		.arches = E500MC | E5500,
		.description = "External PID Load Context Register",
		.name = "eplc",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_EPLC,
	},
	{
		.arches = E500MC | E5500,
		.description = "External Proxy Register",
		.name = "epr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_EPR,
	},
	{
		.arches = E500MC | E5500,
		.description = "External PID Store Context Register",
		.name = "epsc",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_EPSC,
	},
	{
		.arches = E500MC | E5500,
		.description = "Exception Syndrome Register",
		.name = "esr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_ESR,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Data Exception Address Register",
		.name = "gdear",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GDEAR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Guest External Proxy Register",
		.name = "gepr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GEPR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Guest Exception Syndrome Register",
		.name = "gesr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GESR,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Data TLB Error Register",
		.name = "givor13",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR13,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Instruction TLB Error",
		.name = "givor14",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR14,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Data Storage Interrupt",
		.name = "givor2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR2,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Instruction Storage Interrupt",
		.name = "givor3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR3,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest External Input",
		.name = "givor4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR4,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest System Call",
		.name = "givor8",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVOR8,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Interrupt Vector Prefix Register",
		.name = "givpr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GIVPR,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Processor Identification Register",
		.name = "gpir",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GPIR,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Special Purpose Register General 0",
		.name = "gsprg0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSPRG0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Guest Special Purpose Register General 1",
		.name = "gsprg1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSPRG1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Guest Special Purpose Register General 2",
		.name = "gsprg2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSPRG2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Guest Special Purpose Register General 3",
		.name = "gsprg3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSPRG3,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Save/Restore Register 0",
		.name = "gsrr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSRR0,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Guest Save/Restore Register 1",
		.name = "gsrr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_GSRR1,
	},
#endif
#ifdef EXPOSE_HDBCRS
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 0",
		.name = "hdbcr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 1",
		.name = "hdbcr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 2",
		.name = "hdbcr2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 3",
		.name = "hdbcr3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR3,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 4",
		.name = "hdbcr4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR4,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 5",
		.name = "hdbcr5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR5,
	},
	{
		.arches = E500MC | E5500,
		.description = "Hardware Debug Control Register 6",
		.name = "hdbcr6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.inum = SPR_HDBCR6,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Hardware Implementation-Dependent Register 0",
		.name = "hid0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_HID0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Instruction Address Compare 1",
		.name = "iac1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IAC1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Instruction Address Compare 2",
		.name = "iac2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IAC2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 0 (Critical Input)",
		.name = "ivor0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 1 (Machine Check)",
		.name = "ivor1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 10 (Decrementer)",
		.name = "ivor10",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR10,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 11 (Fixed-Interval Timer Interrupt)",
		.name = "ivor11",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR11,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 12 "
			 "(Watchdog Timer Interrupt)",
		.name = "ivor12",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR12,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 13 (Data TLB Error)",
		.name = "ivor13",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR13,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 14 "
			 "(Instruction TLB Error)",
		.name = "ivor14",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR14,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 15 (Debug)",
		.name = "ivor15",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR15,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 2 (Data Storage)",
		.name = "ivor2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 3 "
			 "(Instruction Storage)",
		.name = "ivor3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR3,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 35 "
		         "(Performance Monitor)",
		.name = "ivor35",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR35,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 36"
	                 "(Processor Doorbell Interrupt)",
		.name = "ivor36",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR36,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 37"
	                 "(Processor Doorbell Critical Interrupt)",
		.name = "ivor37",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR37,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 38"
	                 "(Guest Processor Doorbell)",
		.name = "ivor38",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR38,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 39"
	                 "(Guest Processor Doorbell Critical"
	                 " and "
	                 "Machine Check)",
		.name = "ivor39",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR39,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 4 (External Input)",
		.name = "ivor4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR4,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 40"
	                 "(Hypervisor System Call)",
		.name = "ivor40",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR40,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 41"
	                 "(Hypervisor Privilege)",
		.name = "ivor41",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR41,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 5 (Alignment)",
		.name = "ivor5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR5,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 6 (Program)",
		.name = "ivor6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR6,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 7 "
			 "(Floating-Point Unavailable)",
		.name = "ivor7",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR7,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 8 (System Call)",
		.name = "ivor8",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR8,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Offset Register 9 "
			 "(Auxillary Processor Unavailable)",
		.name = "ivor9",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVOR9,
	},
	{
		.arches = E500MC | E5500,
		.description = "Interrupt Vector Prefix Register",
		.name = "ivpr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_IVPR,
	},
	{
		.arches = E500MC | E5500,
		.description = "L1 Cache Configure Register 0",
		.name = "l1cfg0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L1CFG0,
	},
	{
		.arches = E500MC | E5500,
		.description = "L1 Cache Configure Register 1",
		.name = "l1cfg1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L1CFG1,
	},
	{
		.arches = E500MC | E5500,
		.description = "L1 Cache Control and Status Register 0",
		.name = "l1csr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L1CSR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "L1 Cache Control and Status Register 1",
		.name = "l1csr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L1CSR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "L1 Cache Control and Status Register 2",
		.name = "l1csr2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L1CSR2,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error data high capture register",
		.name = "l2captdatahi",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CAPTDATAHI,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error data low capture register",
		.name = "l2captdatalo",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CAPTDATALO,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error capture ECC syndrome register",
		.name = "l2captecc",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CAPTECC,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Configuration Register 0",
		.name = "l2cfg0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CFG0,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Control and Status Register 0",
		.name = "l2csr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CSR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Control and Status Register 1",
		.name = "l2csr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2CSR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error address capture register",
		.name = "l2erraddr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRADDR,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error attributes register",
		.name = "l2errattr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRATTR,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error control register",
		.name = "l2errctl",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRCTL,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Error Detect Register",
		.name = "l2errdet",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRDET,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Error Disable Register",
		.name = "l2errdis",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRDIS,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache Error extended address capture register",
		.name = "l2erreaddr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERREADDR,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error injection control register",
		.name = "l2errinjctl",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRINJCTL,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error injection mask high register",
		.name = "l2errinjhi",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRINJHI,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error injection mask low register",
		.name = "l2errinjlo",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRINJLO,
	},
	{
		.arches = E500MC | E5500,
		.description = "L2 Cache error interrupt enable register",
		.name = "l2errinten",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_L2ERRINTEN,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Logical Partition ID Format Register",
		.name = "lpidr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_LPIDR,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 0",
		.name = "mas0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS0,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 1",
		.name = "mas1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS1,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 2",
		.name = "mas2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS2,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 3",
		.name = "mas3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS3,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 4",
		.name = "mas4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS4,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 5",
		.name = "mas5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS5,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 6",
		.name = "mas6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS6,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 7",
		.name = "mas7",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS7,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "MMU Assist Register 8",
		.name = "mas8",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MAS8,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Machine Check Address Register",
		.name = "mcar",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MCAR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Machine Check Address Register Upper",
		.name = "mcaru",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MCARU,
	},
	{
		.arches = E500MC | E5500,
		.description = "Machine Check Syndrome Register",
		.name = "mcsr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MCSR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Machine Check Save/Restore Register 0",
		.name = "mcsrr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MCSRR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Machine Check Save/Restore Register 1",
		.name = "mcsrr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MCSRR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Configuration Register",
		.name = "mmucfg",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MMUCFG,
	},
	{
		.arches = E500MC | E5500,
		.description = "MMU Control and Status Register 0",
		.name = "mmucsr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MMUCSR0,
	},
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "MSR Protect Register",
		.name = "msrp",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_MSRP,
	},
#endif
#ifdef DEBUG_EHV
	{
		.arches = E500MC | E5500,
		.description = "Nexus Processor ID Register",
		.name = "npidr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_NPIDR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Nexus SPR Access Configuration Register",
		.name = "nspc",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_NSPC,
	},
	{
		.arches = E500MC | E5500,
		.description = "Nexus SPR Access Data Register",
		.name = "nspd",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_NSPD,
	},
#endif
	{
		.arches = E500MC | E5500,
		.description = "Process ID Register",
		.name = "pid0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_PID,
	},
	{
		.arches = E500MC | E5500,
		.description = "Processor ID Register",
		.name = "pir",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_PIR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Processor Version Register",
		.name = "pvr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_PVR,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 0",
		.name = "sprg0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG0,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 1",
		.name = "sprg1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG1,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 2",
		.name = "sprg2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG2,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 3",
		.name = "sprg3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG3,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 4",
		.name = "sprg4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG4,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 5",
		.name = "sprg5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG5,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 6",
		.name = "sprg6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG6,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 7",
		.name = "sprg7",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG7,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 8",
		.name = "sprg8",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG8,
	},
	{
		.arches = E500MC | E5500,
		.description = "SPR General Register 9",
		.name = "sprg9",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SPRG9,
	},
	{
		.arches = E500MC | E5500,
		.description = "Save/Restore Register 0",
		.name = "srr0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SRR0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Save/Restore Register 1",
		.name = "srr1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SRR1,
	},
	{
		.arches = E500MC | E5500,
		.description = "System Version Register",
		.name = "svr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_SVR,
	},
	{
		.arches = E500MC | E5500,
		.description = "Supervisor Time Base Lower",
		.name = "tbl",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TBL,
	},
	{
		.arches = E500MC | E5500,
		.description = "Supervisor Time Base Upper",
		.name = "tbu",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TBU,
	},
	{
		.arches = E500MC | E5500,
		.description = "Timer Control Register",
		.name = "tcr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TCR,
	},
	{
		.arches = E500MC | E5500,
		.description = "TLB Configuration Register 0",
		.name = "tlb0cfg",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TLB0CFG,
	},
	{
		.arches = E500MC | E5500,
		.description = "TLB Configuration Register 1",
		.name = "tlb1cfg",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TLB1CFG,
	},
	{
		.arches = E500MC | E5500,
		.description = "Timer Status Register",
		.name = "tsr",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_TSR,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 0",
		.name = "usprg0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG0,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 3",
		.name = "usprg3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG3,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 4",
		.name = "usprg4",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG4,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 5",
		.name = "usprg5",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG5,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 6",
		.name = "usprg6",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG6,
	},
	{
		.arches = E500MC | E5500,
		.description = "User SPR General 7",
		.name = "usprg7",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "64",
		},
		.type = {
			[e500mc] = "int32",
			[e5500]  = "int64",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_USPRG7,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Time Base Lower",
		.name = "utbl",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_UTBL,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Time Base Upper",
		.name = "utbu",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_spr,
		.inum = SPR_UTBU,
	},
};

struct register_description pmrs[] =
{
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Counter Register 0",
		.name = "pmc0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMC0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Counter Register 1",
		.name = "pmc1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMC1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Counter Register 2",
		.name = "pmc2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMC2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Counter Register 3",
		.name = "pmc3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMC3,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Global Control Register 0",
		.name = "pmgc0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMGC0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control A Register 0",
		.name = "pmlca0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCA0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control A Register 1",
		.name = "pmlca1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCA1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control A Register 2",
		.name = "pmlca2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCA2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control A Register 3",
		.name = "pmlca3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCA3,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control B Register 0",
		.name = "pmlcb0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCB0,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control B Register 1",
		.name = "pmlcb1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCB1,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control B Register 2",
		.name = "pmlcb2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCB2,
	},
	{
		.arches = E500MC | E5500,
		.description = "Performance Monitor Local Control B Register 3",
		.name = "pmlcb3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_PMLCB3,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Counter Register 0",
		.name = "upmc0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMC0,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Counter Register 1",
		.name = "upmc1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMC1,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Counter Register 2",
		.name = "upmc2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMC2,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Counter Register 3",
		.name = "upmc3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMC3,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Global Control Register 0",
		.name = "upmgc0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMGC0,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control A Register 0",
		.name = "upmlca0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCA0,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control A Register 1",
		.name = "upmlca1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCA1,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control A Register 2",
		.name = "upmlca2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCA2,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control A Register 3",
		.name = "upmlca3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCA3,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control B Register 0",
		.name = "upmlcb0",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCB0,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control B Register 1",
		.name = "upmlcb1",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCB1,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control B Register 2",
		.name = "upmlcb2",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCB2,
	},
	{
		.arches = E500MC | E5500,
		.description = "User Performance Monitor Local Control B Register 3",
		.name = "upmlcb3",
		.bitsize = {
			[e500mc] = "32",
			[e5500]  = "32",
		},
		.save_restore = "no",
		.group = "general",
		.cat = reg_cat_pmr,
		.inum = PMR_UPMLCB3,
	},
};

/* struct rdnb is used to create a Register_Description - Name Binding. */
struct rdnb
{
	struct register_description *rd;
	const char *name;
};

#define REG_DESC_NAME_BIND(rt) { rt, #rt }
static const struct rdnb reg_desc_name[] =
{
	REG_DESC_NAME_BIND(power_core_gprs),
	REG_DESC_NAME_BIND(power_fpu_fprs),
	REG_DESC_NAME_BIND(power_core_pc),
	REG_DESC_NAME_BIND(power_core_msr),
	REG_DESC_NAME_BIND(power_core_cr),
	REG_DESC_NAME_BIND(power_core_sprs),
	REG_DESC_NAME_BIND(power_fpu_fpscr),
	REG_DESC_NAME_BIND(sprs),
	REG_DESC_NAME_BIND(pmrs),
};

static const char *lookup_reg_desc_name(struct register_description *rd);
static const char *lookup_reg_desc_name(struct register_description *rd)
{
	const struct rdnb *p = NULL;

	for (p = &reg_desc_name[0];
	     p - &reg_desc_name[0] < sizeof(reg_desc_name)/sizeof(reg_desc_name[0]);
	     p++)
		if (p->rd == rd)
			return p->name;

	return NULL;
}

static inline bool valid_register_table(struct register_description *rtab, unsigned int length, bool warn);
static inline bool valid_register_table(struct register_description *rtab, unsigned int length, bool warn)
{
	if (rtab == NULL || length == 0) {
		fprintf(stderr, "error: empty register table\n");
		return false;
	}

	for (unsigned int i = 0; i < length; i++) {

		if (rtab[i].name == NULL || strcmp(rtab[i].name, "") == 0) {
			fprintf(stderr, "error: define %s[%d].name\n",
			        lookup_reg_desc_name(rtab), i);
			return false;
		}

		if (rtab[i].arches == 0) {
			fprintf(stderr, "error: define %s[%s].arches\n",
			        lookup_reg_desc_name(rtab), rtab[i].name);
			return false;
		}

		for (arch_t arch = initial_arch; arch < arch_count; arch++) {
			if (arch_mask_table[arch] & rtab[i].arches) {
				if (rtab[i].bitsize[arch] == NULL ||
				    strcmp(rtab[i].bitsize[arch], "") == 0) {
					fprintf(stderr, "error: define %s[%s].bitsize[%s]\n",
					        lookup_reg_desc_name(rtab), rtab[i].name,
						arch_name[arch]);
					return false;
				}
			} else if (rtab[i].bitsize[arch] != NULL) {
				fprintf(stderr,
				        "error: %s[%s].bitsize[%s] defined, "
				        "but register %s not defined for arch %s\n",
					lookup_reg_desc_name(rtab),
				        rtab[i].name, arch_name[arch], rtab[i].name,
					arch_name[arch]);
				return false;
			}
		}

		for (arch_t arch = initial_arch; arch < arch_count; arch++) {
			if (arch_mask_table[arch] & rtab[i].arches) {
				if (rtab[i].regnum[arch] == NULL ||
				    strcmp(rtab[i].regnum[arch], "") == 0) {
					if (warn)
						fprintf(stderr, "warning: %s[%s].regnum[%s] not defined\n",
							lookup_reg_desc_name(rtab),
						        rtab[i].name, arch_name[arch]);
				}
			} else if (rtab[i].regnum[arch] != NULL) {
				fprintf(stderr,
				        "error: %s[%s].regnum[%s] defined, "
				        "but register %s not defined for arch %s\n",
					lookup_reg_desc_name(rtab),
				        rtab[i].name, arch_name[arch],
				        rtab[i].name, arch_name[arch]);
				return false;
			}
		}

		for (arch_t arch = initial_arch; arch < arch_count; arch++) {
			if (arch_mask_table[arch] & rtab[i].arches) {
				if (rtab[i].type[arch] == NULL ||
				    strcmp(rtab[i].type[arch], "") == 0) {
					if (warn)
						fprintf(stderr,
						        "warning: %s[%s].type[%s] defaulting to int\n",
						        lookup_reg_desc_name(rtab), rtab[i].name, arch_name[arch]);
				}
			} else if (rtab[i].type[arch] != NULL) {
				fprintf(stderr,
				        "error: %s[%s].type[%s] defined, "
				        "but register %s not defined for arch %s\n",
				        lookup_reg_desc_name(rtab),
					rtab[i].name, arch_name[arch], rtab[i].name, arch_name[arch]);
				return false;
			}
		}

		if (rtab[i].cat == reg_cat_unk) {
			fprintf(stderr,
			        "error: define %s[%s].cat\n",
			        lookup_reg_desc_name(rtab), rtab[i].name);
			return false;
		}

		if (i != 0 && rtab[i].inum == 0) {
			/* The reason this is not a hard error is that PMR_UPMC0 (== 0)
			 * is not at index 0 in the pmrs reg table. */
			if (warn)
				fprintf(stderr,
				        "warning: %s[%s].inum @ index %d equals 0 (fyi)\n",
					lookup_reg_desc_name(rtab), rtab[i].name, i);
		}
	}

	return true;
}

static inline bool valid_register_tables(void);
static inline bool valid_register_tables(void)
{
	/* Set warn = true when developing a new target description.
	 * Set warn = false for production runs (post development of target description).
	 */
	bool warn = false;
	bool rv = true;

	rv &= valid_register_table(power_core_gprs, ECOUNT(power_core_gprs), warn);
	rv &= valid_register_table(power_fpu_fprs, ECOUNT(power_fpu_fprs), warn);
	rv &= valid_register_table(power_core_pc, ECOUNT(power_core_pc), warn);
	rv &= valid_register_table(power_core_msr, ECOUNT(power_core_msr), warn);
	rv &= valid_register_table(power_core_cr, ECOUNT(power_core_cr), warn);
	rv &= valid_register_table(power_core_sprs, ECOUNT(power_core_sprs), warn);
	rv &= valid_register_table(power_fpu_fpscr, ECOUNT(power_fpu_fpscr), warn);
	rv &= valid_register_table(sprs, ECOUNT(sprs), warn);
	rv &= valid_register_table(pmrs, ECOUNT(pmrs), warn);

	return rv;
}


static inline unsigned int ecount(arch_t arch, struct register_description *rd, unsigned int len);
static inline unsigned int ecount(arch_t arch, struct register_description *rd, unsigned int len)
{
	unsigned int i, count;

	for (i = 0, count = 0; i < len; i++)
		if (rd[i].arches & arch_mask_table[arch])
			count++;

	return count;
}

#define DEFINE_FUNCTION(rd_vec)                         \
static inline unsigned int rd_vec##_count(arch_t arch); \
static inline unsigned int rd_vec##_count(arch_t arch)  \
{                                                       \
	return ecount(arch, rd_vec, ECOUNT(rd_vec));    \
}

DEFINE_FUNCTION(power_core_gprs)
DEFINE_FUNCTION(power_fpu_fprs)
DEFINE_FUNCTION(power_core_pc)
DEFINE_FUNCTION(power_core_msr)
DEFINE_FUNCTION(power_core_cr)
DEFINE_FUNCTION(power_core_sprs)
DEFINE_FUNCTION(power_fpu_fpscr)
DEFINE_FUNCTION(sprs)
DEFINE_FUNCTION(pmrs)

static inline unsigned int num_regs(arch_t arch);
static inline unsigned int num_regs(arch_t arch)
{
	unsigned int count;

	count = power_core_gprs_count(arch) +
	        power_fpu_fprs_count(arch)  +
	        power_core_pc_count(arch)   +
	        power_core_msr_count(arch)  +
	        power_core_cr_count(arch)   +
	        power_core_sprs_count(arch) +
	        power_fpu_fpscr_count(arch) +
		sprs_count(arch)            +
	        pmrs_count(arch);

	return count;
}

static inline int reg_desc_byte_count(arch_t arch, struct register_description *registers, unsigned int count);
static inline int reg_desc_byte_count(arch_t arch, struct register_description *registers, unsigned int count)
{
	unsigned int nbytes = 0, i;

	for (i = 0; i < count; i++) {
		nbytes += atoi(registers[i].bitsize[arch])/8;
	}

	return nbytes;
}

static inline unsigned int num_reg_bytes(arch_t arch);
static inline unsigned int num_reg_bytes(arch_t arch)
{
	unsigned int nbytes = 0;

	nbytes += reg_desc_byte_count(arch, power_core_gprs, power_core_gprs_count(arch));
	nbytes += reg_desc_byte_count(arch, power_fpu_fprs, power_fpu_fprs_count(arch));
	nbytes += reg_desc_byte_count(arch, power_core_pc, power_core_pc_count(arch));
	nbytes += reg_desc_byte_count(arch, power_core_msr, power_core_msr_count(arch));
	nbytes += reg_desc_byte_count(arch, power_core_cr, power_core_cr_count(arch));
	nbytes += reg_desc_byte_count(arch, power_core_sprs, power_core_sprs_count(arch));
	nbytes += reg_desc_byte_count(arch, power_fpu_fpscr, power_fpu_fpscr_count(arch));
	nbytes += reg_desc_byte_count(arch, sprs, sprs_count(arch));
	nbytes += reg_desc_byte_count(arch, pmrs, pmrs_count(arch));

	return nbytes;
}

static inline unsigned int buf_max(arch_t arch);
static inline unsigned int buf_max(arch_t arch)
{
	/* BUFMAX defines the maximum number of characters in inbound/outbound
	 * buffers; at least NUMREGBYTES*2 are needed for register packets.
	 *
	 * buffer_ammount: Add a little extra padding, as the G packet utilizes
	 * 2 * num_reg_bytes bytes and the G prefixed to that means that we drop
	 * a byte if we define BUFMAX to be exactly 2 * num_reg_bytes.
	 * In general, we need:
	 * BUFMAX >= max({length(name(<command>))+length(payload(<command>))
	 *                for <command> in RSP-commands})
	 * For now, 8 seems like a good upper bound on the maximum length an
	 * alternative name can go upto even if G were to be renamed.
	 * PS: We do not have an instance of a larger packet that is
	 * transmitted over the serial line using the RSP.
	 */

	const int buffer_ammount = 8;

	return 2 * num_reg_bytes(arch) + buffer_ammount;
}
