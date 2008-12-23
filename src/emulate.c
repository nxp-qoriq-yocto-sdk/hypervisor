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
#include <libos/console.h>
#include <guestmemio.h>
#include <percpu.h>
#include <libos/trap_booke.h>
#include <paging.h>
#include <tlbcache.h>
#include <timers.h>
#include <greg.h>
#include <events.h>

#ifdef CONFIG_STATISTICS
static inline int get_tlb_ivax_stat(unsigned long va)
{
	if (va & TLBIVAX_TLB1) {
		if (va & TLBIVAX_INV_ALL)
			return stat_emu_tlbivax_tlb1_all;
		else
			return stat_emu_tlbivax_tlb1;
	} else {
		if (va & TLBIVAX_INV_ALL)
			return stat_emu_tlbivax_tlb0_all;
		else
			return stat_emu_tlbivax_tlb0;
	}

}
#else
static inline int get_tlb_ivax_stat(unsigned long va)
{
	return 0;
}
#endif

static int get_ea_indexed(trapframe_t *regs, uint32_t insn)
{
	int ra = (insn >> 16) & 31;
	int rb = (insn >> 11) & 31;
	
	return regs->gpregs[rb] + (ra ? regs->gpregs[ra] : 0);
}

/**
 * Emulate the msgsnd instruction
 *
 * This function is called when the guest OS tries to execute a msgsnd
 * instruction.  This instruction must be emulated to ensure that one
 * partition cannot send a message to another partition.
 */
static int emu_msgsnd(trapframe_t *regs, uint32_t insn)
{
	unsigned int rb = (insn >> 11) & 31;
	uint32_t msg = regs->gpregs[rb];
	guest_t *guest = get_gcpu()->guest;
	unsigned long lpid = guest->lpid;
	unsigned int type = msg >> 27;

	inc_stat(stat_emu_msgsnd);

	/*
	 * Validate the message type, and convert it to the corresponding
	 * hypervisor message type.  Supported types are: DBELL (0) and
	 * DBELL_CRIT (1)
	 */
	if (type > 1) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "msgsnd@0x%08lx: invalid message type %u\n", regs->srr0, type);
		return 1;
	}

	/*
	 * DBELL (0) becomes G_DBELL (2), and DBELL_CRIT (1) becomes
	 * G_DBELL_CRIT (2).  The quickest way to make that change is to set bit
	 * 1 of the type field (aka bit 35 of the register).
	 */
	msg |= 0x10000000;

	/* Check for broadcast */
	if (msg & 0x04000000) {
		/* Tell each core that it needs to reflect a msgsnd doorbell
		   exception. */
		if (type)
			for (unsigned int i = 0; i < guest->cpucnt; i++)
				atomic_or(&guest->gcpus[i]->crit_gdbell_pending, GCPU_PEND_MSGSNDC);
		else
			for (unsigned int i = 0; i < guest->cpucnt; i++)
				atomic_or(&guest->gcpus[i]->gdbell_pending, GCPU_PEND_MSGSND);
	} else {
		unsigned int pir = msg & 0x3fff;

		if (pir >= guest->cpucnt) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "msgsnd@0x%08lx: invalid pir %u\n", regs->srr0, pir);
			return 1;
		}

		gcpu_t *gcpu = guest->gcpus[pir];
		if (type)
			atomic_or(&gcpu->crit_gdbell_pending, GCPU_PEND_MSGSNDC);
		else
			atomic_or(&gcpu->gdbell_pending, GCPU_PEND_MSGSND);
	}

	/* Clear the reserved bits and oerwrite the LPIDTAG field with
	   the current lpid */
	msg = (msg & 0xfc003fff) | (lpid << 14);

	asm volatile("msgsnd %0" : : "r" (msg) : "memory");

	return 0;
}

/**
 * Emulate the msgclr instruction
 *
 * This function is called when the guest OS tries to execute a msgclr
 * instruction.  This instruction must be emulated to ensure that one
 * partition cannot send a message to another partition.
 *
 * Unlike emu_msgsnd, we don't re-issue the msgclr instruction.  Instead, we
 * just clear the GCPU_PEND_MSGSND[C} bit.  The hypervisor trap handler will
 * still get called, but it won't reflect the interrupt to the guest.
 */
static int emu_msgclr(trapframe_t *regs, uint32_t insn)
{
	unsigned int rb = (insn >> 11) & 31;
	uint32_t msg = regs->gpregs[rb];
	guest_t *guest = get_gcpu()->guest;
	unsigned int type = msg >> 27;

	inc_stat(stat_emu_msgclr);

	/*
	 * Validate the message type, and convert it to the corresponding
	 * hypervisor message type.  Supported types are: DBELL (0) and
	 * DBELL_CRIT (1)
	 */
	if (type > 1) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "msgclr@0x%08lx: invalid message type %u\n", regs->srr0, type);
		return 1;
	}

	/* Check for broadcast */
	if (msg & 0x04000000) {
		if (type)
			for (unsigned int i = 0; i < guest->cpucnt; i++)
				atomic_and(&guest->gcpus[i]->crit_gdbell_pending, ~GCPU_PEND_MSGSNDC);
		else
			for (unsigned int i = 0; i < guest->cpucnt; i++)
				atomic_and(&guest->gcpus[i]->gdbell_pending, ~GCPU_PEND_MSGSND);
	} else {
		unsigned int pir = msg & 0x3fff;

		if (pir >= guest->cpucnt) {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "msgclr@0x%08lx: invalid pir %u\n", regs->srr0, pir);
			return 1;
		}

		gcpu_t *gcpu = guest->gcpus[pir];
		if (type)
			atomic_and(&gcpu->crit_gdbell_pending, ~GCPU_PEND_MSGSNDC);
		else
			atomic_and(&gcpu->gdbell_pending, ~GCPU_PEND_MSGSND);
	}

	return 0;
}

void save_mas(gcpu_t *gcpu)
{
	gcpu->mas0 = mfspr(SPR_MAS0);
	gcpu->mas1 = mfspr(SPR_MAS1);
	gcpu->mas2 = mfspr(SPR_MAS2);
	gcpu->mas3 = mfspr(SPR_MAS3);
	gcpu->mas6 = mfspr(SPR_MAS6);
	gcpu->mas7 = mfspr(SPR_MAS7);
}

void restore_mas(gcpu_t *gcpu)
{
	mtspr(SPR_MAS0, gcpu->mas0);
	mtspr(SPR_MAS1, gcpu->mas1);
	mtspr(SPR_MAS2, gcpu->mas2);
	mtspr(SPR_MAS3, gcpu->mas3);
	mtspr(SPR_MAS6, gcpu->mas6);
	mtspr(SPR_MAS7, gcpu->mas7);
}

void tlbivax_ipi(trapframe_t *regs)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	int tlb = (guest->tlbivax_addr & TLBIVAX_TLB1) ? INV_TLB1 : INV_TLB0;

	save_mas(gcpu);
	guest_inv_tlb(guest->tlbivax_addr, -1, tlb);
	restore_mas(gcpu);
	
	atomic_add(&guest->tlbivax_count, -1);
}

static int emu_tlbivax(trapframe_t *regs, uint32_t insn)
{
	unsigned long va = get_ea_indexed(regs, insn);
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	int i;

	inc_stat(stat_emu_tlbivax);

	inc_stat(get_tlb_ivax_stat(va));
	
	if (va & TLBIVAX_RESERVED) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbivax@0x%08lx: reserved bits in EA: 0x%08lx\n",
		         regs->srr0, va);
		return 1;
	}
 
	spin_lock(&guest->inv_lock);
	guest->tlbivax_addr = va;
	guest->tlbivax_count = guest->cpucnt;

	for (i = 0; i < guest->cpucnt; i++)
		if (i != gcpu->gcpu_num)
			setevent(guest->gcpus[i], EV_TLBIVAX);

	tlbivax_ipi(regs);

	while (guest->tlbivax_count != 0)
		barrier();

	spin_unlock(&guest->inv_lock);
	return 0;
}

static int emu_tlbilx(trapframe_t *regs, uint32_t insn)
{
	unsigned long va = get_ea_indexed(regs, insn);
	gcpu_t *gcpu = get_gcpu();
	unsigned int pid;
	int type = (insn >> 21) & 3;
	int ret = 0;
	register_t saved;

	saved = disable_critint_save(); 
	save_mas(gcpu);

	inc_stat(stat_emu_tlbilx);

	pid = (gcpu->mas6 & MAS6_SPID_MASK) >> MAS6_SPID_SHIFT;

	switch (type) {
	case 0: /* Invalidate LPID */
		guest_inv_tlb(TLBIVAX_INV_ALL, -1, INV_TLB0 | INV_TLB1);
		break;

	case 1: /* Invalidate PID */
		guest_inv_tlb(TLBIVAX_INV_ALL, pid, INV_TLB0 | INV_TLB1);
		break;

	case 2: /* Invalid */
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbilx@0x%08lx: reserved instruction type 2\n",
		         regs->srr0);

		ret = 1;
		break;

	case 3: /* Invalidate address */
		guest_inv_tlb(va, pid, INV_TLB0 | INV_TLB1);
		break;
	}

	restore_mas(gcpu);
	restore_critint(saved);

	return ret;
}

/* No-op, as we never actually execute a tlbivax */
static int emu_tlbsync(trapframe_t *regs, uint32_t insn)
{
	inc_stat(stat_emu_tlbsync);
	return 0;
}

static void fixup_tlb_sx_re(void)
{
	if (!(mfspr(SPR_MAS1) & MAS1_VALID))
		return;

#ifdef CONFIG_TLB_CACHE
	BUG();
#else
	gcpu_t *gcpu = get_gcpu();
	unsigned long mas3 = mfspr(SPR_MAS3);
	unsigned long mas7 = mfspr(SPR_MAS7);

	unsigned long grpn = (mas7 << (32 - PAGE_SHIFT)) |
	                     (mas3 >> MAS3_RPN_SHIFT);

	printlog(LOGTYPE_GUEST_MMU, LOGLEVEL_VERBOSE + 1,
	         "sx_re: mas0 %lx mas1 %lx mas3 %lx mas7 %lx grpn %lx\n",
	         mfspr(SPR_MAS0), mfspr(SPR_MAS1), mas3, mas7, grpn);

	/* Currently, we only use virtualization faults for bad mappings. */
	if (likely(!(mfspr(SPR_MAS8) & MAS8_VF))) {
		unsigned long attr;
		unsigned long rpn = vptbl_xlate(gcpu->guest->gphys_rev,
		                                grpn, &attr, PTE_PHYS_LEVELS);

		assert(attr & PTE_VALID);

		mtspr(SPR_MAS3, (rpn << PAGE_SHIFT) |
		                (mas3 & (MAS3_FLAGS | MAS3_USER)));
		mtspr(SPR_MAS7, rpn >> (32 - PAGE_SHIFT));
	}

	assert(MAS0_GET_TLBSEL(mfspr(SPR_MAS0)) == 0);
#endif
}

static int emu_tlbsx(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();
	uint32_t va = get_ea_indexed(regs, insn);
	register_t mas1, mas6;

	inc_stat(stat_emu_tlbsx);
	disable_critint();
	
	mas6 = mfspr(SPR_MAS6);
	mas1 = (TLB_TSIZE_4K << MAS1_TSIZE_SHIFT) |
	       ((mas6 & MAS6_SAS) << MAS1_TS_SHIFT) |
	       (mas6 & MAS6_SPID_MASK);

	int tlb1 = guest_find_tlb1(-1, mas1, va >> PAGE_SHIFT);
	if (tlb1 >= 0) {
		mtspr(SPR_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(tlb1));
		mtspr(SPR_MAS1, gcpu->gtlb1[tlb1].mas1);
		mtspr(SPR_MAS2, gcpu->gtlb1[tlb1].mas2);
		mtspr(SPR_MAS3, gcpu->gtlb1[tlb1].mas3);
		mtspr(SPR_MAS7, gcpu->gtlb1[tlb1].mas7);

		enable_critint();
		return 0;
	}

#ifdef CONFIG_TLB_CACHE
	tlbctag_t tag = make_tag(va, (mas6 & MAS6_SPID_MASK) >> MAS6_SPID_SHIFT,
	                         mas6 & MAS6_SAS);
	tlbcset_t *set;
	int way;

	if (find_gtlb_entry(va, tag, &set, &way)) {
		gtlb0_to_mas(set - cpu->client.tlbcache, way);
		enable_critint();
		return 0;
	}
#endif

	asm volatile("tlbsx 0, %0" : : "r" (va) : "memory");
	fixup_tlb_sx_re();

	enable_critint();
	return 0;
}

static int emu_tlbre(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();
	uint32_t mas0 = mfspr(SPR_MAS0);
	unsigned int entry, tlb;
	
	if (mas0 & (MAS0_RESERVED | 0x20000000)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbre@0x%08lx: reserved bits in MAS0: 0x%08x\n", regs->srr0, mas0);
		return 1;
	}

	inc_stat(stat_emu_tlbre);

	tlb = MAS0_GET_TLBSEL(mas0);
	if (tlb == 0) {
		disable_critint();

#ifdef CONFIG_TLB_CACHE
		gtlb0_to_mas((mfspr(SPR_MAS2) >> MAS2_EPN_SHIFT) &
		             (cpu->client.tlbcache_bits - 1),
		             MAS0_GET_TLB0ESEL(mas0));
#else
		asm volatile("tlbre" : : : "memory");
		fixup_tlb_sx_re();
#endif
		enable_critint();
		return 0;
	}

	entry = MAS0_GET_TLB1ESEL(mas0);
	if (entry >= TLB1_GSIZE) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbre@0x%08lx: attempt to read TLB1 entry %d (max %d)\n",
		         regs->srr0, entry, TLB1_GSIZE);
		return 1;
	}

	mtspr(SPR_MAS1, gcpu->gtlb1[entry].mas1);
	mtspr(SPR_MAS2, gcpu->gtlb1[entry].mas2);
	mtspr(SPR_MAS3, gcpu->gtlb1[entry].mas3);
	mtspr(SPR_MAS7, gcpu->gtlb1[entry].mas7);
	return 0;
}

static int emu_tlbwe(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();
	guest_t *guest = gcpu->guest;
	unsigned long grpn, mas0, mas1, mas2, mas3, mas7;
	int ret;

	disable_critint();
	save_mas(gcpu);
	
	mas0 = gcpu->mas0;
	mas1 = gcpu->mas1;
	mas2 = gcpu->mas2;
	mas3 = gcpu->mas3;
	mas7 = gcpu->mas7;

	grpn = (gcpu->mas7 << (32 - PAGE_SHIFT)) |
	       (mas3 >> MAS3_RPN_SHIFT);

	inc_stat(stat_emu_tlbwe);

	if (mas0 & (MAS0_RESERVED | 0x20000000)) {
		restore_mas(gcpu);
		enable_critint();
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbwe@0x%lx: reserved bits in MAS0: 0x%lx\n",
		         regs->srr0, mas0);
		return 1;
	}

	if (gcpu->mas1 & MAS1_RESERVED) {
		restore_mas(gcpu);
		enable_critint();
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbwe@0x%lx: reserved bits in MAS1: 0x%lx\n",
		         regs->srr0, mas0);
		return 1;
	}

	if (gcpu->mas2 & MAS2_RESERVED) {
		restore_mas(gcpu);
		enable_critint();
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbwe@0x%lx: reserved bits in MAS2: 0x%lx\n",
		         regs->srr0, mas0);
		return 1;
	}

	if (gcpu->mas3 & MAS3_RESERVED) {
		restore_mas(gcpu);
		enable_critint();
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbwe@0x%lx: reserved bits in MAS3: 0x%lx\n",
		         regs->srr0, mas0);
		return 1;
	}

	if (gcpu->mas7 & MAS7_RESERVED) {
		restore_mas(gcpu);
		enable_critint();
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "tlbwe@0x%lx: reserved bits in MAS7: 0x%lx\n",
		         regs->srr0, mas0);
		return 1;
	}

	/* If an entry for this address exists elsewhere in any
	 * TLB, don't let the guest create undefined behavior by
	 * adding a duplicate entry.
	 */
	if (mas1 & MAS1_VALID) {
		unsigned long epn = mas2 >> PAGE_SHIFT;
		unsigned long pages;
		int tlb1esel, tsize;

		if (mas0 & MAS0_TLBSEL1) {
			tsize = MAS1_GETTSIZE(mas1);
			pages = tsize_to_pages(tsize);
			tlb1esel = MAS0_GET_TLB1ESEL(mas0);

			if (((mas2 >> PAGE_SHIFT) & (pages - 1)) ||
			    ((mas3 >> PAGE_SHIFT) & (pages - 1))) {
				restore_mas(gcpu);
				enable_critint();

				printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				         "tlbwe@0x%lx: misaligned: mas0 = 0x%lx, mas1 = 0x%lx,\n"
				         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
				         regs->srr0, mas0, mas1, mas2, mas3, mas7);
				return 1;
			}
		} else {
			/* The hardware ignores tsize in TLB0, but it's
			 * convenient for us for it to be set properly.
			 */
			mas1 &= ~MAS1_TSIZE_MASK;
			mas1 |= 1 << MAS1_TSIZE_SHIFT;
			tsize = TLB_TSIZE_4K;
			pages = 1;
			tlb1esel = -1;
		}

		int dup = guest_find_tlb1(tlb1esel, mas1, epn);
		if (dup >= 0) {
			restore_mas(gcpu);
			enable_critint();

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: duplicate TLB entry\n", regs->srr0);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: new: mas0 = 0x%lx, mas1 = 0x%lx,\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, mas0, mas1, mas2, mas3, mas7);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: dup: TLB1 entry = %d, mas1 = 0x%lx,\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, dup,
			         gcpu->gtlb1[dup].mas1, gcpu->gtlb1[dup].mas2,
			         gcpu->gtlb1[dup].mas3, gcpu->gtlb1[dup].mas7);

			return 1;
		}

#ifdef CONFIG_TLB_CACHE
		if ((mas0 & MAS0_TLBSEL1) &&
		    unlikely(check_tlb1_conflict(epn, tsize, MAS1_GETTID(mas1),
		                                 !!(mas1 & MAS1_TS)))) {

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: duplicate TLB entry\n", regs->srr0);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: new: mas0 = 0x%lx, mas1 = 0x%lx,\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, mas0, mas1,
			         mas2, mas3, mas7);

			return 1;
		}
#else
		mtspr(SPR_MAS6, (mas1 & MAS1_TID_MASK) |
		                ((mas1 >> MAS1_TS_SHIFT) & 1));

		for (unsigned long i = epn; i < epn + pages; i++) {
			unsigned long sx_mas0;
		
			mtspr(SPR_MAS2, i << PAGE_SHIFT);
			asm volatile("tlbsx 0, %0" : : "r" (mas2 & MAS2_EPN) : "memory");
			sx_mas0 = mfspr(SPR_MAS0);

			if (likely(!(mfspr(SPR_MAS1) & MAS1_VALID)))
				continue;

			if (!(mas0 & MAS0_TLBSEL1)) {
				assert(MAS0_GET_TLBSEL(sx_mas0) == 0);
				if ((mas0 & MAS0_ESEL_MASK) == (sx_mas0 & MAS0_ESEL_MASK))
					continue; 
			} else if (sx_mas0 & MAS0_TLBSEL1) {
				continue;
			}

			restore_mas(gcpu);
			enable_critint();

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: duplicate TLB entry\n", regs->srr0);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: new: mas0 = 0x%lx, mas1 = 0x%lx,\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, mas0, mas1, mas2, mas3, mas7);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: dup: mas0 = 0x%lx, mas1 = 0x%lx\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, mfspr(SPR_MAS0),
			         mfspr(SPR_MAS1), mfspr(SPR_MAS2),
			         mfspr(SPR_MAS3), mfspr(SPR_MAS7));

			return 1;
		}
#endif
	}

	if (mas0 & MAS0_TLBSEL1) {
		unsigned long epn = mas2 >> PAGE_SHIFT;
		int entry = MAS0_GET_TLB1ESEL(mas0);

		if (entry >= TLB1_GSIZE) {
			restore_mas(gcpu);
			enable_critint();

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: attempt to write TLB1 entry %d (max %d)\n",
			         regs->srr0, entry, TLB1_GSIZE);
			return 1;
		}

		inc_stat(stat_emu_tlbwe_tlb1);

		guest_set_tlb1(entry, mas1, epn, grpn, mas2 & MAS2_FLAGS,
		               mas3 & (MAS3_FLAGS | MAS3_USER));
	} else {
		unsigned long mas8 = guest->lpid | MAS8_GTS;
		unsigned long attr, gmas3;
		unsigned long rpn = vptbl_xlate(guest->gphys, grpn,
		                                &attr, PTE_PHYS_LEVELS);

		/* If there's no valid mapping, request a virtualization
		 * fault, so a machine check can be reflected upon use.
		 */
		if (unlikely(!(attr & PTE_VALID))) {
			mas8 |= MAS8_VF;
			rpn = grpn;
		} else {
			mas8 |= (attr << PTE_MAS8_SHIFT) & PTE_MAS8_MASK;
		}

		gmas3 = mas3 & PTE_MAS3_MASK;
		mas3 &= (attr & PTE_MAS3_MASK) | MAS3_USER;

		inc_stat(stat_emu_tlbwe_tlb0);

		ret = guest_set_tlb0(mas0, mas1, mas2, mas3, rpn, mas8, gmas3);
		if (unlikely(ret == ERR_BUSY)) {
			restore_mas(gcpu);
			enable_critint();

			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: duplicate TLB entry\n", regs->srr0);
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			         "tlbwe@0x%lx: new: mas0 = 0x%lx, mas1 = 0x%lx,\n"
			         "    mas2 = 0x%lx, mas3 = 0x%lx, mas7 = 0x%lx\n",
			         regs->srr0, mas0, mas1, mas2, mas3, mas7);

			return 1;
		}
	}

	restore_mas(gcpu);
	enable_critint();
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
	register_t ret;

	inc_stat(stat_emu_spr);

	if (read_gspr(regs, spr, &ret)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "mfspr@0x%lx: unknown reg %d\n", regs->srr0, spr);
		ret = 0;
//FIXME		return 1;
	}
	
	regs->gpregs[reg] = ret;
	return 0;
}

static int emu_mtspr(trapframe_t *regs, uint32_t insn)
{
	int spr = get_spr(insn);
	int reg = (insn >> 21) & 31;
	register_t val = regs->gpregs[reg];

	inc_stat(stat_emu_spr);

	if (write_gspr(regs, spr, val)) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "mtspr@0x%lx: unknown reg %d, val 0x%lx\n",
		         regs->srr0, spr, val);
//FIXME		return 1;
	}

	return 0;
}

static int emu_rfci(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();

	regs->srr0 = gcpu->csrr0;
	write_gmsr(regs, gcpu->csrr1);

	return 0;
}

static int emu_rfmci(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();

	regs->srr0 = gcpu->mcsrr0;
	write_gmsr(regs, gcpu->mcsrr1);

	return 0;
}

static int emu_dcbtls(trapframe_t *regs, uint32_t insn)
{
	uint32_t addr, ct;

	addr = get_ea_indexed(regs, insn);
	ct = (insn >> 21) & 0x1f;
	printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
			"emulating dcbtls ct =%x, addr = %x\n", ct, addr);

	/*FIXME : Add dcbtls emulation code here, right now we emulate failure*/
	if (ct == 0)
		mtspr(SPR_L1CSR0, mfspr(SPR_L1CSR0) |  L1CSR0_DCUL);

	return 0;
}

static int emu_icbtls(trapframe_t *regs, uint32_t insn)
{
	uint32_t addr, ct;

	addr =  get_ea_indexed(regs, insn);
	ct = (insn >> 21) & 0x1f;
	printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
			"emulating icbtls ct =%x, addr = %x\n", ct, addr);

	/*FIXME : Add icbtls emulation code here, right now we emulate failure*/
	if (ct == 0)
		mtspr(SPR_L1CSR0, mfspr(SPR_L1CSR1) | L1CSR1_ICUL);

	return 0;
}

static int emu_dcblc(trapframe_t *regs, uint32_t insn)
{
	uint32_t addr, ct;

	addr =  get_ea_indexed(regs, insn);
	ct = (insn >> 21) & 0x1f;
	printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
			"emulating dcblc ct =%x, addr = %x\n", ct, addr);

	/*FIXME : Add dcblc emulation code here*/

	return 0;
}

static int emu_icblc(trapframe_t *regs, uint32_t insn)
{
	uint32_t addr, ct;

	addr =  get_ea_indexed(regs, insn);
	ct = (insn >> 21) & 0x1f;
	printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
			"emulating icblc ct =%x, addr = %x\n", ct, addr);

	/*FIXME : Add icblc emulation code here*/

	return 0;
}

static int emu_rfdi(trapframe_t *regs, uint32_t insn)
{
	gcpu_t *gcpu = get_gcpu();

	if (!gcpu->guest->guest_debug_mode)
		return 1;

	regs->srr0 = gcpu->dsrr0;
	regs->srr1 = (regs->srr1 & MSR_HVPRIV_GDEBUG) |
	             (gcpu->dsrr1 & ~MSR_HVPRIV_GDEBUG);

	return 0;
}

#ifdef CONFIG_DEVICE_VIRT

/**
 * emu_load_store - emulate any of the load or store instructions
 * @vaddr - effective address from the DEAR register
 * @store - returns 0 if this is a load, non-zero if this is a store
 * @reg - returns the source/destination register number
 *
 * Emulate any of the load and store instructions.  Unlike the other emu_xxx
 * functions, this one is not called from hvpriv().  It's called from
 * whatever function is emulating a particular device.
 *
 * Note that the phrase "emulate a device" is not exactly accurate.  We're
 * not really emulating a device, we're just ensuring that access to the
 * device's registers is allowed for that guest.
 *
 * We currently only support 8-bit accesses.  This should be okay, since all
 * the registers are 8-bit, so it's highly unlikely any guest OS will use
 * any other instruction type. In the future, we'll add more support.
 *
 * Returns 0 on success, non-zero if this instruction is not yet supported.
 */
int emu_load_store(trapframe_t *regs, uint32_t insn, void *vaddr,
		   int *store, unsigned int *reg)
{
	unsigned int major, minor, rSD, rA;

	// Extract the major and minor opcodes from the instruction, and the
	// target register number.
	major = (insn >> 26) & 0x3f;
	minor = (insn >> 1) & 0x3ff;
	rSD = (insn >> 21) & 0x1f;	// Source or destination register
	rA = (insn >> 26) & 0x1f;	// Base register

	// Now decode the instruction and emulate it.

	switch (major) {
	case 0x1f:
		switch (minor) {
		case 0x077:	// lbzux
			if (unlikely(rA == 0))
				// We expect the core to trap on an invalid
				// instruction before it would trap on a VF,
				// but just in case it doesn't.
				return 1;
			regs->gpregs[rA] = (register_t) vaddr;
			// fall-through ...
		case 0x057:	// lbzx
			regs->gpregs[rSD] = in8(vaddr);
			*store = 0;
			break;

		case 0x0f7:	// stbux
			if (unlikely(rA == 0))
				return 1;
			regs->gpregs[rA] = (register_t) vaddr;
			// fall-through ...
		case 0x0d7:	// stbx
			out8(vaddr, regs->gpregs[rSD]);
			*store = 1;
			break;

		default:
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				 "%s: unimplemented instruction %08x (major=0x%x minor=0x%x rSD=0x%x)\n",
				 __func__, insn, major, minor, rSD);
			return 1;
		}
		break;
	case 0x23:	// lbzu
		if (unlikely(rA == 0))
			return 1;
		regs->gpregs[rA] = (register_t) vaddr;
		// fall-through ...
	case 0x22:	// lbz
		regs->gpregs[rSD] = in8(vaddr);
		*store = 0;
		break;

	case 0x27:	// stbu
		if (unlikely(rA == 0))
			return 1;
		regs->gpregs[rA] = (register_t) vaddr;
		// fall-through ...
	case 0x26:	// stb
		out8(vaddr, regs->gpregs[rSD]);
		*store = 1;
		break;

	default:
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
			 "%s: unimplemented instruction %08x (major=0x%x minor=0x%x rSD=0x%x)\n",
			 __func__, insn, major, minor, rSD);
		return 1;
	}

	*reg = rSD;

	return 0;
}

#endif

void hvpriv(trapframe_t *regs)
{
	uint32_t insn, major, minor;
	int fault = 1;
	int ret;

	inc_stat(stat_emu_total);

	guestmem_set_insn(regs);
	printlog(LOGTYPE_EMU, LOGLEVEL_VERBOSE,
	         "hvpriv trap from 0x%lx, srr1 0x%lx, eplc 0x%lx\n", regs->srr0,
	         regs->srr1, mfspr(SPR_EPLC));

	ret = guestmem_in32((uint32_t *)regs->srr0, &insn);
	if (ret != GUESTMEM_OK) {
		printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
		         "guestmem in returned %d\n", ret);
		if (ret == GUESTMEM_TLBMISS)
			regs->exc = EXC_ITLB;
		else
			regs->exc = EXC_ISI;

		reflect_trap(regs);
		return;
	}

	major = (insn >> 26) & 0x3f;
	minor = (insn >> 1) & 0x3ff;
	
	switch (major) {
	case 0x13:
		switch (minor) {
		case 0x033:
			if (unlikely(emu_rfci(regs, insn)))
				goto fault;
			
			return;

		case 0x026:
			if (unlikely(emu_rfmci(regs, insn)))
				goto fault;

			return;

		case 0x027:
			if (unlikely(emu_rfdi(regs, insn)))
				goto fault;

			return;
		}
		
		break;

	case 0x1f:
		switch (minor) {
		case 0x0ce:
			fault = emu_msgsnd(regs, insn);
			break;

		case 0x0ee:
			fault = emu_msgclr(regs, insn);
			break;

		case 0x312:
			fault = emu_tlbivax(regs, insn);
			break;

		case 0x313:
			fault = emu_tlbilx(regs, insn);
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

		case 0xa6:
			fault = emu_dcbtls(regs, insn);
			break;

		case 0x86:
			/* FIXME: would require actual dcbtstls emulation */
			fault = emu_dcbtls(regs, insn);
			break;

		case 0x1d6:
			fault = emu_icbtls(regs, insn);
			break;

		case 0x186:
			fault = emu_dcblc(regs, insn);
			break;

		case 0xd6:
			fault = emu_icblc(regs, insn);
			break;
		}
		
		break;
	} 

	if (likely(!fault)) {
		regs->srr0 += 4;
		return;
	}

fault:
	printlog(LOGTYPE_EMU, LOGLEVEL_DEBUG,
	         "unhandled hvpriv trap from 0x%lx, insn 0x%08x\n",
	         regs->srr0, insn);
	regs->exc = EXC_PROGRAM;
	mtspr(SPR_ESR, ESR_PIL);
	reflect_trap(regs);
}
