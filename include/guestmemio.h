/* @file
 * Load/store acccesors to guest virtual addresses.
 */
/* Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
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

#include <stdint.h>
#include <libos/core-regs.h>
#include <libos/trapframe.h>
#include <libos/io.h>

/* The relevent guestmem_set() call must be made prior
 * to executing any guestmem_in() calls.  It is not
 * required for guestmem_out().
 */
static inline void guestmem_set_data(trapframe_t *regs)
{
	uint32_t eplc = mfspr(SPR_EPLC);
	uint32_t new_eplc = eplc;

	new_eplc &= ~EPC_EAS;
	new_eplc |= (regs->srr1 << (MSRBIT_DS - EPCBIT_EAS)) & EPC_EAS;

	if (eplc != new_eplc) {
		mtspr(SPR_EPLC, new_eplc);
		isync();
	}
}

static inline void guestmem_set_insn(trapframe_t *regs)
{
	uint32_t eplc = mfspr(SPR_EPLC);
	uint32_t new_eplc = eplc;

	new_eplc &= ~EPC_EAS;
	new_eplc |= (regs->srr1 << (MSRBIT_IS - EPCBIT_EAS)) & EPC_EAS;

	if (eplc != new_eplc) {
		mtspr(SPR_EPLC, new_eplc);
		isync();
	}
}

#define GUESTMEM_OK 0
#define GUESTMEM_TLBMISS 1
#define GUESTMEM_TLBERR 2

static inline int guestmem_in32(uint32_t *ptr, uint32_t *val)
{
	register int stat asm("r3") = 0;

	asm("1: lwepx %0, %y2;"
	    "2:"
	    ".section .extable,\"a\";"
	    ".long 1b;"
	    ".long 2b;"
	    ".previous;" : "=r" (*val), "+r" (stat) : "Z" (*ptr));

	return stat;
}

static inline int guestmem_in8(uint8_t *ptr, uint8_t *val)
{
	register int stat asm("r3") = 0;

	asm("1: lbepx %0, %y2;"
	    "2:"
	    ".section .extable,\"a\";"
	    ".long 1b;"
	    ".long 2b;"
	    ".previous;" : "=r" (*val), "+r" (stat) : "Z" (*ptr));

	return stat;
}

static inline int guestmem_out32(uint32_t *ptr, uint32_t val)
{
	register int stat asm("r3") = 0;

	asm("1: stwepx %2, %y1;"
	    "2:"
	    ".section .extable,\"a\";"
	    ".long 1b;"
	    ".long 2b;"
	    ".previous;" : "+r" (stat), "=Z" (*ptr) : "r" (val));

	return stat;
}

static inline int guestmem_out8(uint8_t *ptr, uint8_t val)
{
	register int stat asm("r3") = 0;

	asm("1: stbepx %2, %y1;"
	    "2:"
	    ".section .extable,\"a\";"
	    ".long 1b;"
	    ".long 2b;"
	    ".previous;" : "+r" (stat), "=Z" (*ptr) : "r" (val));

	return stat;
}
