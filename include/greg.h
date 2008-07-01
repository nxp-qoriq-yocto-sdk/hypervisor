/** @file
 * Guest register access
 */

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

#ifndef GREG_H
#define GREG_H

#include <hv.h>
#include <libos/trapframe.h>
#include <errors.h>

#define MINGPR 0
#define MAXGPR 31

#define MINFPR 0
#define MAXFPR 31

int read_gspr(trapframe_t *regs, int spr, register_t *val);
int write_gspr(trapframe_t *regs, int spr, register_t val);

static inline int read_ggpr(trapframe_t *regs, int gpr, register_t *val)
{
	if (gpr >= MINGPR && gpr <= MAXGPR) {
		*val = regs->gpregs[gpr];
		return 0;
	} else
		return ERR_INVALID;
}

static inline int write_ggpr(trapframe_t *regs, int gpr, register_t val)
{
	if (gpr >= MINGPR && gpr <= MAXGPR) {
		regs->gpregs[gpr] = val;
		return 0;
	} else
		return ERR_INVALID;
}

static inline int read_gfpr_const(trapframe_t *regs, int fpr, uint64_t *val)
{
	register_t msr;

	if (fpr < MINFPR || fpr > MAXFPR)
		return ERR_INVALID;

	msr = mfmsr();
	mtmsr(msr | MSR_FP);
	asm volatile("stfd%U0%X0 %1, %0" : "=m" (*val) : "i" (fpr));
	mtmsr(msr);
	return 0;
}

static inline int read_gfpr(trapframe_t *regs, int fpr, uint64_t *val)
{
	if (__builtin_constant_p(fpr))
		return read_gfpr_const(regs, fpr, val);

	switch (fpr) {
	case 0:
		return read_gfpr_const(regs, 0, val);
	case 1:
		return read_gfpr_const(regs, 1, val);
	case 2:
		return read_gfpr_const(regs, 2, val);
	case 3:
		return read_gfpr_const(regs, 3, val);
	case 4:
		return read_gfpr_const(regs, 4, val);
	case 5:
		return read_gfpr_const(regs, 5, val);
	case 6:
		return read_gfpr_const(regs, 6, val);
	case 7:
		return read_gfpr_const(regs, 7, val);
	case 8:
		return read_gfpr_const(regs, 8, val);
	case 9:
		return read_gfpr_const(regs, 9, val);
	case 10:
		return read_gfpr_const(regs, 10, val);
	case 11:
		return read_gfpr_const(regs, 11, val);
	case 12:
		return read_gfpr_const(regs, 12, val);
	case 13:
		return read_gfpr_const(regs, 13, val);
	case 14:
		return read_gfpr_const(regs, 14, val);
	case 15:
		return read_gfpr_const(regs, 15, val);
	case 16:
		return read_gfpr_const(regs, 16, val);
	case 17:
		return read_gfpr_const(regs, 17, val);
	case 18:
		return read_gfpr_const(regs, 18, val);
	case 19:
		return read_gfpr_const(regs, 19, val);
	case 20:
		return read_gfpr_const(regs, 20, val);
	case 21:
		return read_gfpr_const(regs, 21, val);
	case 22:
		return read_gfpr_const(regs, 22, val);
	case 23:
		return read_gfpr_const(regs, 23, val);
	case 24:
		return read_gfpr_const(regs, 24, val);
	case 25:
		return read_gfpr_const(regs, 25, val);
	case 26:
		return read_gfpr_const(regs, 26, val);
	case 27:
		return read_gfpr_const(regs, 27, val);
	case 28:
		return read_gfpr_const(regs, 28, val);
	case 29:
		return read_gfpr_const(regs, 29, val);
	case 30:
		return read_gfpr_const(regs, 30, val);
	case 31:
		return read_gfpr_const(regs, 31, val);
	default:
		return ERR_INVALID;
	};
}

static inline int write_gfpr_const(trapframe_t *regs, int fpr, uint64_t *val)
{
	register_t msr;

	if (fpr < MINFPR || fpr > MAXFPR)
		return ERR_INVALID;

	msr = mfmsr();
	mtmsr(msr | MSR_FP);
	asm volatile("lfd%U0%X0 %1, %0" : : "m" (*val), "i" (fpr));
	mtmsr(msr);
	return 0;
}

static inline int write_gfpr(trapframe_t *regs, int fpr, uint64_t *val)
{
	if (__builtin_constant_p(fpr))
		return write_gfpr_const(regs, fpr, val);

	switch (fpr) {
	case 0:
		return write_gfpr_const(regs, 0, val);
	case 1:
		return write_gfpr_const(regs, 1, val);
	case 2:
		return write_gfpr_const(regs, 2, val);
	case 3:
		return write_gfpr_const(regs, 3, val);
	case 4:
		return write_gfpr_const(regs, 4, val);
	case 5:
		return write_gfpr_const(regs, 5, val);
	case 6:
		return write_gfpr_const(regs, 6, val);
	case 7:
		return write_gfpr_const(regs, 7, val);
	case 8:
		return write_gfpr_const(regs, 8, val);
	case 9:
		return write_gfpr_const(regs, 9, val);
	case 10:
		return write_gfpr_const(regs, 10, val);
	case 11:
		return write_gfpr_const(regs, 11, val);
	case 12:
		return write_gfpr_const(regs, 12, val);
	case 13:
		return write_gfpr_const(regs, 13, val);
	case 14:
		return write_gfpr_const(regs, 14, val);
	case 15:
		return write_gfpr_const(regs, 15, val);
	case 16:
		return write_gfpr_const(regs, 16, val);
	case 17:
		return write_gfpr_const(regs, 17, val);
	case 18:
		return write_gfpr_const(regs, 18, val);
	case 19:
		return write_gfpr_const(regs, 19, val);
	case 20:
		return write_gfpr_const(regs, 20, val);
	case 21:
		return write_gfpr_const(regs, 21, val);
	case 22:
		return write_gfpr_const(regs, 22, val);
	case 23:
		return write_gfpr_const(regs, 23, val);
	case 24:
		return write_gfpr_const(regs, 24, val);
	case 25:
		return write_gfpr_const(regs, 25, val);
	case 26:
		return write_gfpr_const(regs, 26, val);
	case 27:
		return write_gfpr_const(regs, 27, val);
	case 28:
		return write_gfpr_const(regs, 28, val);
	case 29:
		return write_gfpr_const(regs, 29, val);
	case 30:
		return write_gfpr_const(regs, 30, val);
	case 31:
		return write_gfpr_const(regs, 31, val);
	default:
		return ERR_INVALID;
	};
}

static inline int read_gmsr(trapframe_t *regs, register_t *val)
{
	*val = regs->srr1;
	return 0;
}

static inline int write_gmsr(trapframe_t *regs, register_t val)
{
	regs->srr1 = val;
	return 0;
}

static inline int read_gcr(trapframe_t *regs, register_t *val)
{
	*val = regs->cr;
	return 0;
}

static inline int write_gcr(trapframe_t *regs, register_t val)
{
	regs->cr = val;
	return 0;
}

#endif
