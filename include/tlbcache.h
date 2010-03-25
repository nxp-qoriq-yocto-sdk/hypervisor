/* @file
 */
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

#ifndef TLBCACHE_H
#define TLBCACHE_H

#include <paging.h>

#ifdef CONFIG_TLB_CACHE
#define TLBC_WAYS 4

#define TLBC_MIN_IDX_BITS 10

/* Do not touch these structures without updating the TLB miss handler. */
typedef union tlbctag {
	uintptr_t tag;

	/* We only support 6-bit LPIDs, so that the tag
	 * fits in a pointer-size word.
	 */
	struct {

		/* This assumes 10 bits of TLB index, corresponding to a
		 * 64KiB TLB cache.  For larger caches, the high-order bits
		 * are zero.
		 */
#ifndef CONFIG_64BIT
		uintptr_t vaddr:10;
#else
		uintptr_t vaddr:42;
#endif
		uintptr_t valid:1;
		uintptr_t space:1;
		uintptr_t lpid:6;
		uintptr_t pid:14;
	};
} tlbctag_t;

typedef struct tlbcentry {
	uint32_t mas3;

	union {
		uint32_t pad;

		struct {
			uint32_t mas2:8; /* excluding EPN */
			uint32_t mas7:4;
			uint32_t tsize:4;
			uint32_t mas8:2; /* TGS/VF only */
			uint32_t gmas3:6; /* guest rwx bits */
		};
	};
} tlbcentry_t;

typedef struct __attribute__((aligned(64))) tlbcset {
	tlbctag_t tag[TLBC_WAYS];
	tlbcentry_t entry[TLBC_WAYS];
} tlbcset_t;

static inline tlbctag_t make_tag(uintptr_t vaddr, unsigned int pid, unsigned int space)
{
	tlbctag_t tag;

	tag.vaddr = vaddr >> (PAGE_SHIFT + cpu->client.tlbcache_bits);
	tag.pid = pid;
	tag.lpid = mfspr(SPR_LPIDR);
	tag.space = space;
	tag.valid = 1;

	return tag;
}

void tlbcache_init(void);
int find_gtlb_entry(uintptr_t vaddr, tlbctag_t tag, tlbcset_t **setp,
                    unsigned int *way);
void gtlb0_to_mas(unsigned int index, unsigned int way, struct gcpu *gcpu);
int check_tlb1_conflict(uintptr_t epn, unsigned int tsize,
                        unsigned int pid, unsigned int space);

#endif
#endif
