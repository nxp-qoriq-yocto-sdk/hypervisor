/** @file
 * (Basic type definitions for GDB Stub and target description generator).
 */

/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
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

#define ECOUNT(a) sizeof((a))/sizeof((a)[0])

typedef enum reg_cat
{
	reg_cat_unk,
	reg_cat_gpr,
	reg_cat_fpr,
	reg_cat_pc,
	reg_cat_msr,
	reg_cat_cr,
	reg_cat_spr,
	reg_cat_pmr,
	reg_cat_fpscr,
	reg_cat_count /* Keep reg_cat_count last. */
} reg_cat_t;

typedef enum arch
{
	unknown_arch = -1,
	initial_arch,
	e500mc = initial_arch,
	e5500,
	e500mc64 = e5500,
	arch_count /* Keep arch_index_count last. */
} arch_t;

typedef enum arch_mask
{
	E500MC = 1 << e500mc,
	E5500  = 1 << e5500,
} arch_mask_t;

// Map from arch to canonical prefixes to be used for arch.
static const char *arch_name[arch_count] __attribute__ ((unused)) =
{
	[e500mc] = "e500mc",
	/* e500mc64 is for "legacy" reasons.
	 * e5500 is the canonical name to be used.
	 * we actually ought to be having:
	 * [e5500]  = "e5500",
	 * - here. But then, earlier GDB's will not
	 * handle arch powerpc:e5500
	 * New GDB's support the old arch name e500mc64,
	 * so we are OK.
	 */
	[e5500]  = "e500mc64",
};

/* A register_description is created for a register as long as it belongs to any
 * architecture. Within the description, the 'arches' bitvector indicates the
 * architectures this register is defined for.
 */
struct register_description
{
	int arches;
	const char *description;
	const char *name;
	const char *bitsize[arch_count];
	const char *regnum[arch_count];
	const char *save_restore;
	const char *type[arch_count];
	const char *group;
	reg_cat_t cat;
	/* inums are used to lookup the register's value via the hypervisor.
	 * the inum of an SPR is it's SPR number.
	 * In general, inums are defined per category.
	 */
	int inum;
};

typedef struct reg_info
{
        int inum;
        int bitsize;
        reg_cat_t cat;
} reg_info_t;
