/** @file
 * Guest SPR access
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

#include <hv.h>
#include <libos/trapframe.h>
#include <libos/core-regs.h>
#include <libos/fsl-booke-tlb.h>
#include <libos/queue.h>
#include <percpu.h>
#include <greg.h>
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
	case SPR_XER:
		*val = regs->xer;
		break;
	
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
		*val = mfspr(SPR_GSRR0);
		break;

	case SPR_SRR1:
		*val = mfspr(SPR_GSRR1);
		break;

	case SPR_PID:
		*val = mfspr(SPR_PID);
		break;

	case SPR_CSRR0:
		*val = gcpu->csrr0;
		break;

	case SPR_CSRR1:
		*val = gcpu->csrr1;
		break;

	case SPR_DEAR:
		*val = mfspr(SPR_GDEAR);
		break;

	case SPR_ESR:
		*val = mfspr(SPR_GESR);
		break;

	case SPR_IVPR:
		*val = gcpu->ivpr;
		break;

	case SPR_TBL:
		*val = mfspr(SPR_TBL);
		break;

	case SPR_TBU:
		*val = mfspr(SPR_TBU);
		break;

	case SPR_USPRG0:
		*val = mfspr(SPR_USPRG0);
		break;

	case SPR_USPRG4 ... SPR_USPRG7:
		*val = gcpu->sprg[spr - SPR_USPRG4];
		break;

	case SPR_SPRG0:
		*val = mfspr(SPR_GSPRG0);
		break;

	case SPR_SPRG1:
		*val = mfspr(SPR_GSPRG1);
		break;

	case SPR_SPRG2:
		*val = mfspr(SPR_GSPRG2);
		break;

	case SPR_USPRG3:
	case SPR_SPRG3:
		*val = mfspr(SPR_GSPRG3);
		break;

	case SPR_SPRG4:
		*val = mfspr(SPR_SPRG4);
		break;

	case SPR_SPRG5:
		*val = mfspr(SPR_SPRG5);
		break;

	case SPR_SPRG6:
		*val = mfspr(SPR_SPRG6);
		break;

	case SPR_SPRG7:
		*val = mfspr(SPR_SPRG7);
		break;

	case SPR_SPRG8:
		*val = gcpu->sprg[spr - SPR_SPRG8 + 4];
		break;

	case SPR_SPRG9:
		*val = mfspr(SPR_SPRG9);
		break;

	case SPR_PIR:
		*val = mfspr(SPR_GPIR);
		break;

	case SPR_PVR:
		*val = mfspr(SPR_PVR);
		break;

	case SPR_TSR:
		*val = get_tsr();
		break;

	case SPR_TCR:
		*val = get_tcr();
		break;

	case SPR_ATBL:
		*val = mfspr(SPR_ATBL);
		break;

	case SPR_ATBU:
		*val = mfspr(SPR_ATBU);
		break;

	case SPR_IVOR1...SPR_IVOR15:
		*val = gcpu->ivor[spr - SPR_IVOR0];
		break;

	case SPR_IVOR32...SPR_IVOR37:
		*val = gcpu->ivor[spr - SPR_IVOR32 + 32];
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
		*val = gcpu->dsrr0;
		break;

	case SPR_DSRR1:
		*val = gcpu->dsrr1;
		break;

	case SPR_MAS0:
		*val = mfspr(SPR_MAS0);
		break;

	case SPR_MAS1:
		*val = mfspr(SPR_MAS1);
		break;

	case SPR_MAS2:
		*val = mfspr(SPR_MAS2);
		break;

	case SPR_MAS3:
		*val = mfspr(SPR_MAS3);
		break;

	case SPR_MAS4:
		*val = mfspr(SPR_MAS4);
		break;

	case SPR_MAS6:
		*val = mfspr(SPR_MAS6);
		break;

	case SPR_MAS7:
		*val = mfspr(SPR_MAS7);
		break;

	case SPR_TLB0CFG:
		*val = mfspr(SPR_TLB0CFG);
		break;

	case SPR_TLB1CFG:
		*val = mfspr(SPR_TLB1CFG) & ~(TLBCFG_NENTRY_MASK | TLBCFG_ASSOC_MASK);
		*val |= TLB1_GSIZE;
		break;

	case SPR_CDCSR0:
		*val = mfspr(SPR_CDCSR0);
		break;

	case SPR_EPR:
		*val = mfspr(SPR_GEPR);
		break;

	case SPR_EPLC:
		*val = regs->eplc & ~(EPC_ELPID | EPC_EGS);
		break;

	case SPR_EPSC:
		*val = regs->epsc & ~(EPC_ELPID | EPC_EGS);
		break;

	case SPR_HID0:
		*val = mfspr(SPR_HID0);
		break;

	case SPR_L1CSR0:
		*val = mfspr(SPR_L1CSR0);
		break;

	case SPR_L1CSR1:
		*val = mfspr(SPR_L1CSR1);
		break;

	case SPR_L1CSR2:
		*val = mfspr(SPR_L1CSR2);
		break;

	case SPR_L1CSR3:
		*val = mfspr(SPR_L1CSR3);
		break;

	case SPR_L2CSR0:
		*val = mfspr(SPR_L2CSR0);
		break;

	case SPR_L2CSR1:
		*val = mfspr(SPR_L2CSR1);
		break;

	case SPR_L2ERRDIS:
		*val = mfspr(SPR_L2ERRDIS);
		break;

	case SPR_L2ERRDET:
		*val = mfspr(SPR_L2ERRDET);
		break;

	case SPR_L2ERRINTEN:
		*val = mfspr(SPR_L2ERRINTEN);
		break;

	case SPR_L2ERRCTL:
		*val = mfspr(SPR_L2ERRCTL);
		break;

	case SPR_L2ERRATTR:
		*val = mfspr(SPR_L2ERRATTR);
		break;

	case SPR_L2ERRADDR:
		*val = mfspr(SPR_L2ERRADDR);
		break;

	case SPR_L2ERREADDR:
		*val = mfspr(SPR_L2ERREADDR);
		break;

	case SPR_L2CAPTDATAHI:
		*val = mfspr(SPR_L2CAPTDATAHI);
		break;

	case SPR_L2CAPTDATALO:
		*val = mfspr(SPR_L2CAPTDATALO);
		break;

	case SPR_L2CAPTECC:
		*val = mfspr(SPR_L2CAPTECC);
		break;

	case SPR_L2ERRINJCTL:
		*val = mfspr(SPR_L2ERRINJCTL);
		break;

	case SPR_L2ERRINJLO:
		*val = mfspr(SPR_L2ERRINJLO);
		break;

	case SPR_L2ERRINJHI:
		*val = mfspr(SPR_L2ERRINJHI);
		break;

	case SPR_MMUCSR0:
		*val = 0;
		break;

	case SPR_BUCSR:
		*val = mfspr(SPR_BUCSR);
		break;

	case SPR_MMUCFG:
		*val = mfspr(SPR_MMUCFG) & ~MMUCFG_LPIDSIZE;
		break;

	case SPR_SVR:
		*val = mfspr(SPR_SVR);
		break;

	case SPR_DBCR0:
		*val = mfspr(SPR_DBCR0);
		if (!gcpu->guest->guest_debug_mode)
			*val |= DBCR0_EDM;
		break;

	case SPR_DBCR1:
		*val = mfspr(SPR_DBCR1);
		break;

	case SPR_DBCR2:
		*val = mfspr(SPR_DBCR2);
		break;

	case SPR_DBCR4:
		*val = mfspr(SPR_DBCR4);
		break;

	case SPR_DBSR:
		*val = mfspr(SPR_DBSR);
		break;

	case SPR_IAC1:
		*val = mfspr(SPR_IAC1);
		break;

	case SPR_IAC2:
		*val = mfspr(SPR_IAC2);
		break;

	case SPR_DAC1:
		*val = mfspr(SPR_DAC1);
		break;

	case SPR_DAC2:
		*val = mfspr(SPR_DAC2);
		break;

	case SPR_NPIDR:
		*val = mfspr(SPR_NPIDR);
		break;

	case SPR_NSPC:
		*val = mfspr(SPR_NSPC);
		break;

	case SPR_NSPD:
		*val = mfspr(SPR_NSPD);
		break;

	case SPR_L1CFG0:
		*val = mfspr(SPR_L1CFG0);
		break;

	case SPR_L1CFG1:
		*val = mfspr(SPR_L1CFG1);
		break;

	case SPR_L2CFG0:
		*val = mfspr(SPR_L2CFG0);
		break;

	case SPR_DEVENT:
		*val = mfspr(SPR_DEVENT);
		break;

	/* Removed Registers are not part of the vcpu and should fail. */
	case SPR_MSRP:
	case SPR_MAS5:
	case SPR_MAS8:
	case SPR_DBSRWR:
	case SPR_EPCR:
	case SPR_LPIDR:
	case SPR_IVOR0:
	case SPR_IVOR38:
	case SPR_IVOR39:
	case SPR_IVOR40:
	case SPR_IVOR41:
	case SPR_GIVPR:
	case SPR_GPIR:
	case SPR_GIVOR2:
	case SPR_GIVOR3:
	case SPR_GIVOR4:
	case SPR_GIVOR8:
	case SPR_GIVOR13:
	case SPR_GIVOR14:
	case SPR_GESR:
	case SPR_GSRR0:
	case SPR_GSRR1:
	case SPR_GSPRG0:
	case SPR_GSPRG1:
	case SPR_GSPRG2:
	case SPR_GSPRG3:
	case SPR_GDEAR:
	case SPR_GEPR:

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
	register_t mask = 0;

	switch (spr) {
	case SPR_XER:
		regs->xer = val;
		break;
	
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
		mtspr(SPR_GSRR0, val);
		break;

	case SPR_SRR1:
		mtspr(SPR_GSRR1, val);
		break;

	case SPR_PID:
		mtspr(SPR_PID, val);
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
		mtspr(SPR_GDEAR, val);
		break;

	case SPR_ESR:
		mtspr(SPR_GESR, val);
		break;

	case SPR_IVPR:
		val &= IVPR_MASK;
		mtspr(SPR_GIVPR, val);
		gcpu->ivpr = val;
		break;

	case SPR_USPRG0:
		mtspr(SPR_USPRG0, val);
		break;

	case SPR_SPRG0:
		mtspr(SPR_GSPRG0, val);
		break;

	case SPR_SPRG1:
		mtspr(SPR_GSPRG1, val);
		break;

	case SPR_SPRG2:
		mtspr(SPR_GSPRG2, val);
		break;

	case SPR_SPRG3:
		mtspr(SPR_GSPRG3, val);
		break;

	case SPR_SPRG4:
		mtspr(SPR_SPRG4, val);
		break;

	case SPR_SPRG5:
		mtspr(SPR_SPRG5, val);
		break;

	case SPR_SPRG6:
		mtspr(SPR_SPRG6, val);
		break;

	case SPR_SPRG7:
		mtspr(SPR_SPRG7, val);
		break;

	case SPR_SPRG8:
		gcpu->sprg[spr - SPR_SPRG8 + 4] = val;
		break;

	case SPR_SPRG9:
		mtspr(SPR_SPRG9, val);
		break;

	case SPR_PIR:
		printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
		         "mtspr@0x%08lx: unsupported write to PIR\n", regs->srr0);
		return 1;

	case SPR_EPCR:
		/* no-op on 32-bit */
		break;

	case SPR_TSR:
		set_tsr(val);
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

	case SPR_MCSRR0:
		gcpu->mcsrr0 = val;
		break;

	case SPR_MCSRR1:
		gcpu->mcsrr1 = val;
		break;

	case SPR_MCSR:
		atomic_and(&gcpu->mcsr, ~val);
		if ((val & MCSR_MCP) && (!queue_empty(&gcpu->guest->error_event_queue)))
			atomic_or(&gcpu->mcsr, MCSR_MCP);
		break;

	case SPR_DSRR0:
		gcpu->dsrr0 = val;
		break;

	case SPR_DSRR1:
		gcpu->dsrr1 = val;
		break;

	case SPR_DDAM:
		mtspr(SPR_DDAM, val);
		break;

	case SPR_MAS0:
		mtspr(SPR_MAS0, val);
		break;

	case SPR_MAS1:
		mtspr(SPR_MAS1, val);
		break;

	case SPR_MAS2:
		mtspr(SPR_MAS2, val);
		break;

	case SPR_MAS3:
		mtspr(SPR_MAS3, val);
		break;

	case SPR_MAS4:
		mtspr(SPR_MAS4, val);
		break;

	case SPR_MAS6:
		mtspr(SPR_MAS6, val);
		break;

	case SPR_MAS7:
		mtspr(SPR_MAS7, val);
		break;

	case SPR_CDCSR0:
		/* no-op */
		break;

	case SPR_EPR:
		mtspr(SPR_GEPR, val);
		break;

	case SPR_EPLC:
		regs->eplc = (val & ~EPC_ELPID) | EPC_EGS |
		             (gcpu->guest->lpid << EPC_ELPID_SHIFT);
		break;

	case SPR_EPSC:
		regs->epsc = (val & ~EPC_ELPID) | EPC_EGS |
		             (gcpu->guest->lpid << EPC_ELPID_SHIFT);
		break;

	case SPR_HID0:
		val &= ~(HID0_EMCP);
		val |= mfspr(SPR_HID0) & HID0_EMCP;
		
		if (!(val & HID0_ENMAS7)) {
			static int warned_enmas7 = 0;
			
			if (!warned_enmas7) {
				/* FIXME: save/restore ENMAS7 when doing hv TLB reads/searches */
				warned_enmas7 = 1;
				printlog(LOGTYPE_EMU, LOGLEVEL_NORMAL,
				         "mtspr@0x%08lx: HID0[ENMAS7] cleared, problems possible\n",
				         regs->srr0);
			}
		}
		
		mtspr(SPR_HID0, val);
		break;

	case SPR_L1CSR0:
		mask = L1CSR0_DCBZ32 | L1CSR0_DCUL;
		if (gcpu->guest->guest_cache_lock)
			mask |= L1CSR0_DCSLC | L1CSR0_DCLO | L1CSR0_DCLFC;
		set_spr_val(SPR_L1CSR0, val, mask);
		break;

	case SPR_L1CSR1:
		mask = L1CSR1_ICUL;
		if (gcpu->guest->guest_cache_lock)
			mask |= L1CSR1_ICSLC | L1CSR1_ICLO | L1CSR1_ICLFC;
		set_spr_val(SPR_L1CSR1, val, mask);
		break;

	case SPR_L2CSR0:
		if (gcpu->guest->guest_cache_lock)
			mask = L2CSR0_L2LFC | L2CSR0_L2FCID | L2CSR0_L2SLC | L2CSR0_L2LO;
		set_spr_val(SPR_L2CSR0, val, mask);
		break;

	case SPR_MMUCSR0:
		guest_inv_tlb(TLBIVAX_INV_ALL, -1, val & (INV_TLB0 | INV_TLB1));
		break;

	case SPR_BUCSR:
		/* no-op */
		break;

	case SPR_DBCR0:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
		         "mtspr@0x%08lx: unsupported write to DBCR0 (external debug on)\n", regs->srr0);
			return 1;
		}
 
		/* we don't want guest access to DBCR0[RST,RET,IRPT,FT,EDM] */
		val &= ~(DBCR0_RST | DBCR0_RET | DBCR0_IRPT | DBCR0_FT | DBCR0_EDM);
		mtspr(SPR_DBCR0, val);
		break;

	case SPR_DBCR1:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
		         "mtspr@0x%08lx: unsupported write to DBCR1 (external debug on)\n", regs->srr0);
			return 1;
		}
		
		if (((val & DBCR1_IAC1ER_EAMSK) == DBCR1_IAC1ER_RADDR) ||
		    ((val & DBCR1_IAC2ER_EAMSK) == DBCR1_IAC2ER_RADDR))
			return 1;

		val &= (DBCR1_IAC1ER_EAMSK | DBCR1_IAC2ER_EAMSK);
		mtspr(SPR_DBCR1, val);
		break;

	case SPR_DBCR2:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
		         "mtspr@0x%08lx: unsupported write to DBCR2 (external debug on)\n", regs->srr0);
			return 1;
		}
		
		if (((val & DBCR2_DAC1ER_EAMSK) == DBCR2_DAC1ER_RADDR) ||
		    ((val & DBCR2_DAC2ER_EAMSK) == DBCR2_DAC2ER_RADDR))
			return 1;

		val &= (DBCR2_DAC1ER_EAMSK | DBCR2_DAC2ER_EAMSK);
		mtspr(SPR_DBCR2, val);
		break;

	case SPR_DBCR4:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
		         "mtspr@0x%08lx: unsupported write to DBCR4 (external debug on)\n", regs->srr0);
			return 1;
		}

		if (((val & DBCR4_DAC1XM) > DBCR4_DAC1XM_RNG) ||
		    ((val & DBCR4_DAC2XM) > DBCR4_DAC2XM_RNG))
			return 1;

		val &= (DBCR4_DAC1XM | DBCR4_DAC2XM);
		mtspr(SPR_DBCR4, val);
		break;

	case SPR_DBSR:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
	         "mtspr@0x%08lx: unsupported write to DBSR (external debug on)\n", regs->srr0);
			return 1;
		}
	
		mtspr(SPR_DBSR, val);
		break;

	case SPR_IAC1:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
	         "mtspr@0x%08lx: unsupported write to IAC1 (externall debug on)\n", regs->srr0);
			return 1;
		}
		mtspr(SPR_IAC1, val);
		break;

	case SPR_IAC2:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
	         "mtspr@0x%08lx: unsupported write to IAC2 (external debug on)\n", regs->srr0);
			return 1;
		}
		mtspr(SPR_IAC2, val);
		break;

	case SPR_DAC1:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
	         "mtspr@0x%08lx: unsupported write to DAC1 (external debug on)\n", regs->srr0);
			return 1;
		}
		mtspr(SPR_DAC1, val);
		break;

	case SPR_DAC2:
		if (!gcpu->guest->guest_debug_mode) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ALWAYS,
	         "mtspr@0x%08lx: unsupported write to DAC2 (external debug on)\n", regs->srr0);
			return 1;
		}
		mtspr(SPR_DAC2, val);
		break;

	default:
		return 1;
	}

	return 0;
}

/**
 * Read guest performance monitor register
 */
int read_pmr(trapframe_t *regs, int pmr, register_t *val)
{
	switch (pmr) {
	case PMR_UPMC0:
		*val = mfpmr(PMR_UPMC0);
		break;
	case PMR_UPMC1:
		*val = mfpmr(PMR_UPMC1);
		break;
	case PMR_UPMC2:
		*val = mfpmr(PMR_UPMC2);
		break;
	case PMR_UPMC3:
		*val = mfpmr(PMR_UPMC3);
		break;
	case PMR_PMC0:
		*val = mfpmr(PMR_PMC0);
		break;
	case PMR_PMC1:
		*val = mfpmr(PMR_PMC1);
		break;
	case PMR_PMC2:
		*val = mfpmr(PMR_PMC2);
		break;
	case PMR_PMC3:
		*val = mfpmr(PMR_PMC3);
		break;
	case PMR_UPMLCA0:
		*val = mfpmr(PMR_UPMLCA0);
		break;
	case PMR_UPMLCA1:
		*val = mfpmr(PMR_UPMLCA1);
		break;
	case PMR_UPMLCA2:
		*val = mfpmr(PMR_UPMLCA2);
		break;
	case PMR_UPMLCA3:
		*val = mfpmr(PMR_UPMLCA3);
		break;
	case PMR_PMLCA0:
		*val = mfpmr(PMR_PMLCA0);
		break;
	case PMR_PMLCA1:
		*val = mfpmr(PMR_PMLCA1);
		break;
	case PMR_PMLCA2:
		*val = mfpmr(PMR_PMLCA2);
		break;
	case PMR_PMLCA3:
		*val = mfpmr(PMR_PMLCA3);
		break;
	case PMR_UPMLCB0:
		*val = mfpmr(PMR_UPMLCB0);
		break;
	case PMR_UPMLCB1:
		*val = mfpmr(PMR_UPMLCB1);
		break;
	case PMR_UPMLCB2:
		*val = mfpmr(PMR_UPMLCB2);
		break;
	case PMR_UPMLCB3:
		*val = mfpmr(PMR_UPMLCB3);
		break;
	case PMR_PMLCB0:
		*val = mfpmr(PMR_PMLCB0);
		break;
	case PMR_PMLCB1:
		*val = mfpmr(PMR_PMLCB1);
		break;
	case PMR_PMLCB2:
		*val = mfpmr(PMR_PMLCB2);
		break;
	case PMR_PMLCB3:
		*val = mfpmr(PMR_PMLCB3);
		break;
	case PMR_UPMGC0:
		*val = mfpmr(PMR_UPMGC0);
		break;
	case PMR_PMGC0:
		*val = mfpmr(PMR_PMGC0);

		if (mfspr(SPR_MSRP) & MSRP_PMMP)
			*val |= PMGC0_PMIE;

		break;
	default:
		return 1;
	}

	return 0;
}

#define NUM_PERF_CTRS 4

/** Checks whether a perfmon interrupt is pending.
 *
 * If so, returns 1.  Otherwise, disengages the PMMP/PCIE workaround and
 * returns 0.
 */
int check_perfmon(trapframe_t *regs)
{
	uint32_t msrp = mfspr(SPR_MSRP);

	if (msrp & MSRP_PMMP) {
		register_t pmr;

		for (int i = 0; i < NUM_PERF_CTRS; i++) {
			/* If not enabled, can't be the cause of
			 * pending interrupt.
			 */
			read_pmr(regs, PMR_PMLCA0 + i, &pmr);
			if (!(pmr & PMLCA_CE))
				continue;

			/* If enabled and overflowing, we still need the
			 * workaround to be engaged.
			 */
			read_pmr(regs, PMR_PMC0 + i, &pmr);
			if (pmr & 0x80000000)
				return 1;
		}

		/* No enabled, overflowing counters -- disengage
		 * the workaround.  PMMP being set implies the guest
		 * thinks PMIE is set.
		 */
		mtspr(SPR_MSRP, msrp & ~MSRP_PMMP);
		mtpmr(PMR_PMGC0, mfpmr(PMR_PMGC0) | PMGC0_PMIE);
	}

	return 0;
}

/**
 * Write guest performance monitor register
 */
int write_pmr(trapframe_t *regs, int pmr, register_t val)
{
	uint32_t reg;

	switch (pmr) {
	case PMR_PMC0:
		mtpmr(PMR_PMC0, val);
		check_perfmon(regs);
		break;
	case PMR_PMC1:
		mtpmr(PMR_PMC1, val);
		check_perfmon(regs);
		break;
	case PMR_PMC2:
		mtpmr(PMR_PMC2, val);
		check_perfmon(regs);
		break;
	case PMR_PMC3:
		mtpmr(PMR_PMC3, val);
		check_perfmon(regs);
		break;
	case PMR_PMLCA0:
		mtpmr(PMR_PMLCA0, val);
		check_perfmon(regs);
		break;
	case PMR_PMLCA1:
		mtpmr(PMR_PMLCA1, val);
		check_perfmon(regs);
		break;
	case PMR_PMLCA2:
		mtpmr(PMR_PMLCA2, val);
		check_perfmon(regs);
		break;
	case PMR_PMLCA3:
		mtpmr(PMR_PMLCA3, val);
		check_perfmon(regs);
		break;
	case PMR_PMLCB0:
		mtpmr(PMR_PMLCB0, val);
		break;
	case PMR_PMLCB1:
		mtpmr(PMR_PMLCB1, val);
		break;
	case PMR_PMLCB2:
		mtpmr(PMR_PMLCB2, val);
		break;
	case PMR_PMLCB3:
		mtpmr(PMR_PMLCB3, val);
		break;
	case PMR_PMGC0:
		if (mfspr(SPR_MSRP) & MSRP_PMMP) {
			/* The guest is masking perfmon interrupts,
			 * so we can disengage the workaround.
			 */
			if (!(val & PMGC0_PMIE))
				mtspr(SPR_MSRP, mfspr(SPR_MSRP) & ~MSRP_PMMP);

			val &= ~PMGC0_PMIE;
		}

		mtpmr(PMR_PMGC0, val);

		break;
	default: /* Includes all user PMRs, which are read-only */
		return 1;
	}

	return 0;
}

int read_fpscr(uint64_t *val)
{
	*val = mffs();
	return 0;
}

int write_fpscr(uint64_t val)
{
	mtfsf(val);
	return 0;
}
