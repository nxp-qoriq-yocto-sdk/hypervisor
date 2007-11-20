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
 *
 * Some hw specific parts of this pmap were derived or influenced
 * by NetBSD's ibm4xx pmap module. More generic code is shared with
 * a few other pmap modules from the FreeBSD tree.
 */

#include "tlb.h"
#include "spr.h"
#include "percpu.h"

static void tlb1_write_entry(unsigned int idx);
static unsigned int size2tsize(uint32_t size);


/*
 *    after tlb1_init:
 *        TLB1[0] = CCSR
 *        TLB1[1] = hv image 16M
 *
 *
 */

/* hardcoded hack for now */
#define CCSRBAR_PA              0xfe000000
#define CCSRBAR_VA              0xf0000000
#define CCSRBAR_SIZE            0x01000000

void tlb1_init(void)
{
	tlb1_set_entry(14, CCSRBAR_VA, CCSRBAR_PA, CCSRBAR_SIZE, _TLB_ENTRY_IO, UV_TID, 0, 0);
}



/*
 * Setup entry in a sw tlb1 table, write entry to TLB1 hardware.
 * This routine is used for low level operations on the TLB1,
 * for creating temporaray as well as permanent mappings (tlb_set_entry).
 *
 * We assume kernel mappings only, thus all entries created have supervisor
 * permission bits set nad user permission bits cleared.
 *
 * Provided mapping size must be a power of 4.
 * Mapping flags must be a combination of MAS2_[WIMG].
 * Entry TID is set to _tid which must not exceed 8 bit value.
 * Entry TS is set to either 0 or MAS1_TS based on provided _ts.
 */
void tlb1_set_entry(unsigned int idx, uint32_t va, uint32_t pa,
                    uint32_t size, uint32_t flags, unsigned int _tid,
                    unsigned int _ts, unsigned int _gs)
{
	int tsize;
	uint32_t ts, tid;

	//debugf("__tlb1_set_entry: s (idx = %d va = 0x%08x pa = 0x%08x "
	//              "size = 0x%08x flags = 0x%08x _tid = %d _ts = %d\n",
	//              idx, va, pa, size, flags, _tid, _ts);

	/* Convert size to TSIZE */
	tsize = size2tsize(size);
	//debugf("__tlb1_set_entry: tsize = %d\n", tsize);

	tid = (_tid <<  MAS1_TID_SHIFT) & MAS1_TID_MASK;
	ts = (_ts) ? MAS1_TS : 0;
	hcpu->tlb1[idx].mas1 = MAS1_VALID | MAS1_IPROT | ts | tid;
	hcpu->tlb1[idx].mas1 |= ((tsize << MAS1_TSIZE_SHIFT) & MAS1_TSIZE_MASK);

	hcpu->tlb1[idx].mas2 = (va & MAS2_EPN) | flags;

	/* Set supervisor rwx permission bits */
	hcpu->tlb1[idx].mas3 = (pa & MAS3_RPN) | MAS3_SR | MAS3_SW | MAS3_SX;


	/* set GS bit */
	hcpu->tlb1[idx].mas8 = 0;
	hcpu->tlb1[idx].mas8 |= ((_gs << MAS8_GTS_SHIFT) & MAS8_GTS_MASK);

	//debugf("__tlb1_set_entry: mas1 = %08x mas2 = %08x mas3 = 0x%08x\n",
	//              tlb1[idx].mas1, tlb1[idx].mas2, tlb1[idx].mas3);

	tlb1_write_entry(idx);
	//debugf("__tlb1_set_entry: e\n");
}


static void
tlb1_write_entry(unsigned int idx)
{
	uint32_t mas0, mas7;

	//debugf("tlb1_write_entry: s\n");

	/* Clear high order RPN bits */
	mas7 = 0;

	/* Select entry */
	mas0 = MAS0_TLBSEL(1) | MAS0_ESEL(idx);
	//debugf("tlb1_write_entry: mas0 = 0x%08x\n", mas0);

	mtspr(SPR_MAS0, mas0);
	__asm volatile("isync");
	mtspr(SPR_MAS1, hcpu->tlb1[idx].mas1);
	__asm volatile("isync");
	mtspr(SPR_MAS2, hcpu->tlb1[idx].mas2);
	__asm volatile("isync");
	mtspr(SPR_MAS3, hcpu->tlb1[idx].mas3);
	__asm volatile("isync");
	mtspr(SPR_MAS7, mas7);
	__asm volatile("isync");
	mtspr(SPR_MAS8, hcpu->tlb1[idx].mas8);
	__asm volatile("isync; tlbwe; isync; msync");

	//debugf("tlb1_write_entry: e\n");;
}


/*
 * Return the largest uint value log such that 2^log <= num.
 */
static unsigned int
ilog2(unsigned int num)
{
	int lz;

	__asm ("cntlzw %0, %1" : "=r" (lz) : "r" (num));
	return (31 - lz);
}


/*
 * Convert TLB TSIZE value to mapped region size.
 */
static uint32_t
tsize2size(unsigned int tsize)
{
	/*
	 * size = 4^tsize KB
	 * size = 4^tsize * 2^10 = 2^(2 * tsize - 10)
	 */

	return (1 << (2 * tsize)) * 1024;
}


/*
 * Convert region size (must be power of 4) to TLB TSIZE value.
 */
static unsigned int
size2tsize(uint32_t size)
{
	/*
	 * tsize = log2(size) / 2 - 5
	 */

	return (ilog2(size) / 2 - 5);
}

