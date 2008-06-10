/** @file
 * Guest register access
 */

/*
 * Copyright (C) 2007-2008 Freescale Semiconductor, Inc.
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

#ifndef GREG_H
#define GREG_H

#include <hv.h>
#include <libos/trapframe.h>

#define MINGPR 0
#define MAXGPR 31

int read_gspr(trapframe_t *regs, int spr, register_t *val);
int write_gspr(trapframe_t *regs, int spr, register_t val);

static inline int read_ggpr(trapframe_t *regs, int gpr, register_t *val)
{
	if (gpr >= MINGPR && gpr <= MAXGPR) {
		*val = regs->gpregs[gpr];
		return 0;
	} else
		return 1;
}

static inline int write_ggpr(trapframe_t *regs, int gpr, register_t val)
{
	if (gpr >= MINGPR && gpr <= MAXGPR) {
		regs->gpregs[gpr] = val;
		return 0;
	} else
		return 1;
}

static inline int read_gfpr(trapframe_t *regs, int gpr, register_t *val)
{
	return 1;
}

static inline int write_gfpr(trapframe_t *regs, int gpr, register_t val)
{
	return 1;
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
