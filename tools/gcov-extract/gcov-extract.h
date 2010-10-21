/* File format for coverage information
   Copyright (C) 1996, 1997, 1998, 2000, 2002,
   2003, 2004, 2005, 2008, 2009 Free Software Foundation, Inc.
   Contributed by Bob Manson <manson@cygnus.com>.
   Completely remangled by Nathan Sidwell <nathan@codesourcery.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* This file is derived from gcc/gcov-io.h */

#ifndef GCOV_INFRA_H
#define GCOV_INFRA_H

#include <stdint.h>

typedef void(*gcov_merge_fn) (long *, unsigned);
extern gcov_merge_fn __gcov_merge_add;

extern void __gcov_init (void *);

struct gcov_fn_info
{
	unsigned ident;
	unsigned checksum;
	unsigned n_ctrs[0];
};

struct gcov_ctr_info
{
	unsigned num;
	uint64_t *values;
	gcov_merge_fn merge;
};

struct gcov_info
{
	unsigned version;
	struct gcov_info *next;
	unsigned stamp;
	char *filename;
	unsigned n_functions;
	struct gcov_fn_info *functions;
	unsigned ctr_mask;
	struct gcov_ctr_info counts[0];
};

typedef struct tgt_info {

	/* gcov_info */
	int gi_version_offst;
	int gi_next_offst;
	int gi_stamp_offst;
	int gi_filename_offst;
	int gi_n_functions_offst;
	int gi_functions_offst;
	int gi_ctr_mask_offst;
	int gi_counts_offst;

	/* gcov_ctr_info */
	int gci_num_offst;
	int gci_values_offst;
	int gci_merge_offst;

	/* gcov_fn_info */
	int gfi_ident_offst;
	int gfi_checksum_offst;
	int gfi_n_ctrs_offst;

	/* type sizes */
	int unsigned_size;
	int long_size;
	int word_size;
	int gcov_fn_info_size;
	int gcov_ctr_info_size;

} tgt_info_t;

tgt_info_t tgt_info = {

	/* gcov_info (denoted by gi_ prefixes) */
	.gi_version_offst = 0,
	.gi_next_offst = 4,
	.gi_stamp_offst = 8,
	.gi_filename_offst = 12,
	.gi_n_functions_offst = 16,
	.gi_functions_offst = 20,
	.gi_ctr_mask_offst = 24,
	.gi_counts_offst = 28,

	/* gcov_ctr_info (denoted by gci_ prefixes) */
	.gci_num_offst = 0,
	.gci_values_offst = 4,
	.gci_merge_offst = 8,

	/* gcov_fn_info (denoted by gfi_ prefixes) */
	.gfi_ident_offst = 0,
	.gfi_checksum_offst = 4,
	.gfi_n_ctrs_offst = 8,

	/* type sizes */
#define UNSIGNED_SIZE 4
	.unsigned_size = UNSIGNED_SIZE,
	.long_size = 4,
	.word_size = 4, /* We do override this based on get-data-info. */
	.gcov_fn_info_size = 8 + UNSIGNED_SIZE, /* add an unsigned's worth of storage for n_ctrs[0] */
	.gcov_ctr_info_size = 12,
};

#endif
