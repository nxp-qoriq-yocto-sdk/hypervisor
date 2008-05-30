/** @file
 * Guest SPR access
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

#include <hv.h>
#include <libos/trapframe.h>
#include <libos/spr.h>
#include <percpu.h>
#include <gspr.h>
#include <timers.h>
#include <paging.h>

/**
 * Read special purpose register
 *
 */
int read_gspr(trapframe_t *regs, int spr, register_t *val)
{
	gcpu_t *gcpu = get_gcpu();

	switch (spr) {
	case SPR_LR:
		*val = regs->lr;
		break;

	case SPR_CTR:
		*val = regs->ctr;
		break;

	case SPR_DEC:
		*val = mfspr(SPR_DEC);
		break;

	case SPR_SRR0:
		break;

	case SPR_SRR1:
		break;

	case SPR_PID:
		break;

	case SPR_DECAR:
		break;

	case SPR_CSRR0:
		*val = gcpu->csrr0;
		break;

	case SPR_CSRR1:
		*val = gcpu->csrr1;
		break;

	case SPR_DEAR:
		break;

	case SPR_ESR:
		break;

	case SPR_IVPR:
		*val = gcpu->ivpr;
		break;

	case SPR_TBL:
		break;

	case SPR_TBU:
		break;

	case SPR_SPRG0:
		break;

	case SPR_SPRG1:
		break;

	case SPR_SPRG2:
		break;

	case SPR_SPRG3:
		break;

	case SPR_SPRG4:
		break;

	case SPR_SPRG5:
		break;

	case SPR_SPRG6:
		break;

	case SPR_SPRG7:
		break;

	case SPR_TBWL:
		break;

	case SPR_TBWU:
		break;

	case SPR_PIR:
		break;

	case SPR_DBSRWR:
		break;

	case SPR_EHCSR:
		break;

	case SPR_MSRP:
		break;

	case SPR_TSR:
		*val = get_tsr();
		break;

	case SPR_LPIDR:
		break;

	case SPR_TCR:
		*val = get_tcr();
		break;

	case SPR_IVOR0...SPR_IVOR15:
		*val = gcpu->ivor[spr - SPR_IVOR0];
		break;

	case SPR_IVOR32...SPR_IVOR37:
		*val = gcpu->ivor[spr - SPR_IVOR32 + 32];
		break;

	case SPR_IVOR38:
		break;

	case SPR_IVOR39:
		break;

	case SPR_IVOR40:
		break;

	case SPR_IVOR41:
		break;

	case SPR_MCARU:
		*val = gcpu->mcar >> 32;
		break;

	case SPR_MCSRR0:
		*val = gcpu->mcsrr0;
		break;

	case SPR_MCSRR1:
		*val = gcpu->mcsrr1;
		break;

	case SPR_MCSR:
		*val = gcpu->mcsr;
		break;

	case SPR_MCAR:
		*val = gcpu->mcar;
		break;

	case SPR_DSRR0:
		break;

	case SPR_DSRR1:
		break;

	case SPR_DDAM:
		break;

	case SPR_SPRG8:
		break;

	case SPR_SPRG9:
		break;

	case SPR_TLB0CFG:
		break;

	case SPR_TLB1CFG:
		*val = mfspr(SPR_TLB1CFG) & ~TLBCFG_NENTRY_MASK;
		*val |= TLB1_GSIZE;
		break;

	case SPR_CDCSR0:
		*val = mfspr(SPR_CDCSR0);
		break;

	case SPR_EPLC:
		break;

	case SPR_EPSC:
		break;

	case SPR_HID0:
		break;

	case SPR_L1CSR0:
		break;

	case SPR_L1CSR1:
		break;

	case SPR_MMUCSR0:
		*val = 0;
		break;

	case SPR_BUCSR:
		*val = mfspr(SPR_BUCSR);
		break;

	case SPR_MMUCFG:
		*val = mfspr(SPR_MMUCFG);
		break;

	case SPR_SVR:
		break;

	default:
		return 1;
	}

	return 0;
}

/**
 * Write special purpose register
 *
 */
int write_gspr(trapframe_t *regs, int spr, register_t val)
{
	gcpu_t *gcpu = get_gcpu();

	switch (spr) {
	case SPR_LR:
		regs->lr = val;
		break;

	case SPR_CTR:
		regs->ctr = val;
		break;

	case SPR_DEC:
		mtspr(SPR_DEC, val);
		break;

	case SPR_SRR0:
		break;

	case SPR_SRR1:
		break;

	case SPR_PID:
		break;

	case SPR_DECAR:
		mtspr(SPR_DECAR, val);
		break;

	case SPR_CSRR0:
		gcpu->csrr0 = val;
		break;

	case SPR_CSRR1:
		gcpu->csrr1 = val;
		break;

	case SPR_DEAR:
		break;

	case SPR_ESR:
		break;

	case SPR_IVPR:
		val &= IVPR_MASK;
		mtspr(SPR_GIVPR, val);
		gcpu->ivpr = val;
		break;

	case SPR_TBL:
		break;

	case SPR_TBU:
		break;

	case SPR_SPRG0:
		break;

	case SPR_SPRG1:
		break;

	case SPR_SPRG2:
		break;

	case SPR_SPRG3:
		break;

	case SPR_SPRG4:
		break;

	case SPR_SPRG5:
		break;

	case SPR_SPRG6:
		break;

	case SPR_SPRG7:
		break;

	case SPR_TBWL:
		break;

	case SPR_TBWU:
		break;

	case SPR_PIR:
		break;

	case SPR_DBSRWR:
		break;

	case SPR_EHCSR:
		break;

	case SPR_MSRP:
		break;

	case SPR_TSR:
		set_tsr(val);
		break;

	case SPR_LPIDR:
		break;

	case SPR_TCR:
		set_tcr(val);
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
		break;

	case SPR_IVOR13:
		mtspr(SPR_GIVOR13, val);
		goto low_ivor;
		
	case SPR_IVOR14:
		mtspr(SPR_GIVOR14, val);
		goto low_ivor;

	case SPR_IVOR0...SPR_IVOR1:
	case SPR_IVOR5...SPR_IVOR7:
	case SPR_IVOR9...SPR_IVOR12:
	case SPR_IVOR15:
	low_ivor:
		gcpu->ivor[spr - SPR_IVOR0] = val & IVOR_MASK;
		break;

	case SPR_IVOR32...SPR_IVOR37:
		gcpu->ivor[spr - SPR_IVOR32 + 32] = val & IVOR_MASK;
		break;

	case SPR_IVOR40:
		break;

	case SPR_IVOR41:
		break;

	case SPR_MCARU:
		break;

	case SPR_MCSRR0:
		gcpu->mcsrr0 = val;
		break;

	case SPR_MCSRR1:
		gcpu->mcsrr1 = val;
		break;

	case SPR_MCSR:
		gcpu->mcsr &= ~val;
		break;

	case SPR_MCAR:
		break;

	case SPR_DSRR0:
		break;

	case SPR_DSRR1:
		break;

	case SPR_DDAM:
		break;

	case SPR_SPRG8:
		break;

	case SPR_SPRG9:
		break;

	case SPR_PID1:
		break;

	case SPR_PID2:
		break;

	case SPR_TLB0CFG:
		break;

	case SPR_TLB1CFG:
		break;

	case SPR_CDCSR0:
		break;

	case SPR_EPLC:
		break;

	case SPR_EPSC:
		break;

	case SPR_HID0:
		break;

	case SPR_L1CSR0:
		break;

	case SPR_L1CSR1:
		break;

	case SPR_MMUCSR0:
		if (val & MMUCSR_L2TLB0_FI)
			mtspr(SPR_MMUCSR0, MMUCSR_L2TLB0_FI);
		if (val & MMUCSR_L2TLB1_FI)
			guest_inv_tlb1(0);
		break;

	case SPR_BUCSR:
		break;

	case SPR_MMUCFG:
		break;

	case SPR_SVR:
		break;

	default:
		return 1;
	}

	return 0;
}

