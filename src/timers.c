/*
 * Timer support
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

/* Eventually, this will do virtualized timers -- but for now,
 * just let the guest run things.
 */

#include <frame.h>
#include <percpu.h>
#include <spr.h>
#include <console.h>

void decrementer(trapframe_t *regs)
{
	guest_t *guest = hcpu->gcpu->guest;

	if (!(regs->srr1 & MSR_GS)) {
		printf("decrementer exception from hypervisor\n");
		dump_regs(regs);
	}

	mtspr(SPR_GSRR0, regs->srr0);
	mtspr(SPR_GSRR1, regs->srr1);
	regs->srr0 = guest->ivpr | guest->ivor[10];
	regs->srr1 &= MSR_CE | MSR_ME | MSR_DE | MSR_GS;

//	printf("decrementer returning, srr0 %08x, srr1 %08x, gsrr0 %08x, gsrr1 %08x\n",
//	       regs->srr0, regs->srr1, mfspr(SPR_GSRR0), mfspr(SPR_GSRR1));
}
