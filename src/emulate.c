/*
 * Instruction emulation
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
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

#include <uv.h>
#include <frame.h>
#include <console.h>
#include <guestmemio.h>
#include <percpu.h>

#define TLB1_NENTRIES 4

static int get_ea_indexed(trapframe_t *regs, uint32_t insn)
{
	int ra = (insn >> 16) & 31;
	int rb = (insn >> 11) & 31;
	
	return regs->gpregs[rb] + (ra ? regs->gpregs[ra] : 0);
}

static int emu_tlbivax(trapframe_t *regs, uint32_t insn)
{
	uint32_t va = get_ea_indexed(regs, insn);
	
	if (va & 0xff3) {
		printf("tlbivax@0x%08x: reserved bits in EA: 0x%08x\n", regs->srr0, va);
		return 1;
	}

	asm volatile("tlbivax 0, %0" : : "r" (va) : "memory");
	return 0;
}

static int emu_tlbsync(trapframe_t *regs, uint32_t insn)
{
	// FIXME: add lock so only one can be executing at a time
	asm volatile("tlbsync" : : : "memory");
	return 0;
}

static int emu_tlbsx(trapframe_t *regs, uint32_t insn)
{
	uint32_t va = get_ea_indexed(regs, insn);
	guest_t *guest = hcpu->gcpu->guest;

	mtspr(SPR_MAS5, MAS5_SGS | mfspr(SPR_LPID));
	asm volatile("tlbsx 0, %0" : : "r" (va) : "memory");

	if (mfspr(SPR_MAS1) & MAS1_VALID) {
		uint32_t mas3 = mfspr(SPR_MAS3);
		uint64_t rpn = ((uint64_t)mfspr(SPR_MAS7) << 32) |
		               (mas3 & MAS3_RPN);

		uint64_t gphys = rpn - guest->mem_real + guest->mem_start;

//		printf("tlbsx va 0x%08x found 0x%09llx gphys 0x%09llx\n", va, rpn, gphys);

		mtspr(SPR_MAS7, gphys >> 32);
		mtspr(SPR_MAS3, (uint32_t)gphys | (mas3 & ~MAS3_RPN));
	}
	
	return 0;
}

static int emu_tlbre(trapframe_t *regs, uint32_t insn)
{
	guest_t *guest = hcpu->gcpu->guest;;
	uint32_t mas0 = mfspr(SPR_MAS0);
	int entry;
	
	if (mas0 & MAS0_RESERVED) {
		printf("tlbre@0x%08x: reserved bits in MAS0: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (!(mas0 & MAS0_TLBSEL1)) {
		printf("tlbre@0x%08x: attempt to read from TLB0\n", regs->srr0);
		return 1;
	}

	entry = (mas0 & MAS0_ESEL_TLB1MASK) >> MAS0_ESEL_SHIFT;
	if (entry > TLB1_NENTRIES) {
		printf("tlbre@0x%08x: attempt to read TLB1 entry %d (max %d)\n",
		       regs->srr0, entry, TLB1_NENTRIES);
		return 1;
	}

	asm volatile("tlbre" : :  : "memory");
	
	if (mfspr(SPR_MAS1) & MAS1_VALID) {
		uint32_t mas3 = mfspr(SPR_MAS3);
		uint64_t rpn = ((uint64_t)mfspr(SPR_MAS7) << 32) |
		               (mas3 & MAS3_RPN);

		uint64_t gphys = rpn - guest->mem_real + guest->mem_start;

//		printf("tlbre mas0 0x%08x found 0x%09llx gphys 0x%09llx\n",
//		       mas0, rpn, gphys);

		mtspr(SPR_MAS7, gphys >> 32);
		mtspr(SPR_MAS3, (uint32_t)gphys | (mas3 & ~MAS3_RPN));
	}
	
	return 0;
}

static int emu_tlbwe(trapframe_t *regs, uint32_t insn)
{
	guest_t *guest = hcpu->gcpu->guest;
	uint32_t mas0 = mfspr(SPR_MAS0);
	uint32_t mas1 = mfspr(SPR_MAS1);
	uint32_t mas2 = mfspr(SPR_MAS2);
	uint32_t mas3 = mfspr(SPR_MAS3);
	uint32_t mas7 = mfspr(SPR_MAS7);
	uint64_t gphys = ((uint64_t)mas7 << 32) | (mas3 & MAS3_RPN);
	uint64_t rpn = gphys - guest->mem_start + guest->mem_real;

	if (mas0 & MAS0_RESERVED) {
		printf("tlbwe@0x%08x: reserved bits in MAS0: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (mas1 & MAS1_RESERVED) {
		printf("tlbwe@0x%08x: reserved bits in MAS1: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (mas2 & MAS2_RESERVED) {
		printf("tlbwe@0x%08x: reserved bits in MAS2: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (mas3 & MAS3_RESERVED) {
		printf("tlbwe@0x%08x: reserved bits in MAS3: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (mas7 & MAS7_RESERVED) {
		printf("tlbwe@0x%08x: reserved bits in MAS7: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	if (mas0 & MAS0_TLBSEL1) {
		int entry = (mas0 & MAS0_ESEL_TLB1MASK) >> MAS0_ESEL_SHIFT;
		if (entry >= TLB1_NENTRIES) {
			printf("tlbwe@0x%08x: attempt to write TLB1 entry %d (max %d)\n",
			       regs->srr0, entry, TLB1_NENTRIES);
			return 1;
		}
		
		if (!(mas1 & MAS1_VALID))
			goto ok; 
		
		uint64_t size =
			1024 << (((mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT) * 2);

		if (size < 4096 || size > 4096ULL * 1024 * 1024) {
			printf("tlbwe@0x%08x: invalid size, MAS1=0x%08x\n", regs->srr0, mas1);
			return 1;
		}

		if (rpn & (size - 1)) {	
			printf("tlbwe@0x%08x: misaligned TLB1 entry, gphys 0x%09llx, "
			       "real 0x%09llx, size 0x%09llx\n", regs->srr0, gphys, rpn, size);
			return 1;
		}

		if (gphys + size > guest->mem_end) {
#if 0
			// FIXME: lookup allowed I/O
			printf("tlbwe@0x%08x: guest phys 0x%09llx, size 0x%09llx out of range\n",
			       regs->srr0, gphys, size);
			return 1;
#else
			goto ok;
#endif
		}
	} else if (!(mas1 & MAS1_VALID)) {
		goto ok; 
	}

	if (gphys < guest->mem_start || gphys >= guest->mem_end) {
#if 0
		// FIXME: lookup allowed I/O
		printf("tlbwe@0x%08x: guest phys 0x%09llx out of range\n",
		       regs->srr0, gphys);
		return 1;
#else
		goto ok;
#endif
	}

	mtspr(SPR_MAS7, rpn >> 32);
	mtspr(SPR_MAS3, (uint32_t)rpn | (mas3 & ~MAS3_RPN));

ok:
	mtspr(SPR_MAS8, MAS8_GTS_MASK | mfspr(SPR_LPID));

//	printf("tlbwe@0x%08x: mas0 0x%08x mas1 0x%08x mas2 0x%08x mas3 0x%08x"
//	       " mas7 0x%08x mas8 0x%08x\n", regs->srr0, mfspr(SPR_MAS0), mfspr(SPR_MAS1),
//	       mfspr(SPR_MAS2), mfspr(SPR_MAS3), mfspr(SPR_MAS7), mfspr(SPR_MAS8));

	asm volatile("tlbwe" : : : "memory");
	return 0;
}

static int get_spr(uint32_t insn)
{
	return ((insn >> 6) & 0x3e0) |
	       ((insn >> 16) & 0x1f);
}

static int emu_mfspr(trapframe_t *regs, uint32_t insn)
{
	int spr = get_spr(insn);
	int reg = (insn >> 21) & 31;
	uint32_t ret;
	
	switch (spr) {
	case SPR_TLB1CFG:
		ret = mfspr(SPR_TLB1CFG) & ~TLBCFG_NENTRY_MASK;
		ret |= TLB1_NENTRIES;
		break;

	case SPR_IVPR:
		ret = hcpu->gcpu->guest->ivpr;
		break;

	case SPR_IVOR0...SPR_IVOR15:
		ret = hcpu->gcpu->guest->ivor[spr - SPR_IVOR0];
		break;

	case SPR_IVOR32...SPR_IVOR37:
		ret = hcpu->gcpu->guest->ivor[spr - SPR_IVOR32 + 32];
		break;

	default:
		printf("mfspr@0x%08x: unknown reg %d\n", regs->srr0, spr);
		ret = 0;
//		return 1;
	}
	
	regs->gpregs[reg] = ret;
	return 0;
}

static int emu_mtspr(trapframe_t *regs, uint32_t insn)
{
	int spr = get_spr(insn);
	int reg = (insn >> 21) & 31;
	register_t val = regs->gpregs[reg];

	switch (spr) {
	case SPR_IVPR:
		mtspr(SPR_GIVPR, val);
		hcpu->gcpu->guest->ivpr = val;
		break;
	
	case SPR_IVOR2:
		mtspr(SPR_GIVOR2, val);
		goto low_ivor;
		
	case SPR_IVOR3:
		mtspr(SPR_GIVOR3, val);
		goto low_ivor;
		
	case SPR_IVOR4:
		mtspr(SPR_GIVOR4, val);
		goto low_ivor;
		
	case SPR_IVOR8:
		mtspr(SPR_GIVOR8, val);
		goto low_ivor;
		
	case SPR_IVOR13:
		mtspr(SPR_GIVOR13, val);
		goto low_ivor;
		
	case SPR_IVOR14:
		mtspr(SPR_GIVOR14, val);
		goto low_ivor;
		
	case SPR_IVOR0...SPR_IVOR1:
	case SPR_IVOR5...SPR_IVOR6:
	case SPR_IVOR9...SPR_IVOR12:
	case SPR_IVOR15:
	low_ivor:
		hcpu->gcpu->guest->ivor[spr - SPR_IVOR0] = val;
		break;

	case SPR_IVOR32...SPR_IVOR37:
		hcpu->gcpu->guest->ivor[spr - SPR_IVOR32 + 32] = val;
		break;

	default:
		printf("mtspr@0x%08x: unknown reg %d, val 0x%08x\n",
		       regs->srr0, spr, val);
//		return 1;
	}
	
	return 0;
}

void hvpriv(trapframe_t *regs)
{
	uint32_t insn, op;
	int fault = 1;

	guestmem_set_insn(regs);
//	printf("hvpriv trap from 0x%08x, srr1 0x%08x, eplc 0x%08x\n", regs->srr0,
//	       regs->srr1, mfspr(SPR_EPLC));
	insn = guestmem_in32((uint32_t *)regs->srr0);
	
	if (((insn >> 26) & 0x3f) != 0x1f)
		goto bad;
	
	op = (insn >> 1) & 0x3ff;

	switch (op) {
	case 0x312:
		fault = emu_tlbivax(regs, insn);
		break;
	
	case 0x3b2:
		fault = emu_tlbre(regs, insn);
		break;

	case 0x392:
		fault = emu_tlbsx(regs, insn);
		break;

	case 0x236:
		fault = emu_tlbsync(regs, insn);
		break;

	case 0x3d2:
		fault = emu_tlbwe(regs, insn);
		break;

	case 0x153:
		fault = emu_mfspr(regs, insn);
		break;

	case 0x1d3:
		fault = emu_mtspr(regs, insn);
		break;
	}

	if (__builtin_expect(fault == 0, 1)) {
		regs->srr0 += 4;
		return;
	}

bad:
	printf("unhandled hvpriv trap from 0x%08x, insn 0x%08x\n", regs->srr0, insn);
	stopsim();
}
