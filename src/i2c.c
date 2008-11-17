/** @file
 * I2C device emulation
 */
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
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

#include <libos/trap_booke.h>
#include <percpu.h>
#include <guestmemio.h>

/**
 * i2c_callback - I2C virtualization trap callback function
 *
 * This function is registered via register_vf_handler()
 */
void i2c_callback(trapframe_t *regs, phys_addr_t paddr)
{
	uint32_t insn;
	void *vaddr;
	int ret;

	// Create a TLB mapping for the real physical address.  We assume that
	// for CCSR memory space, guest physical equals real physical.
	tlb1_set_entry(TEMPTLB1, (unsigned long) temp_mapping[0], paddr,
                     TLB_TSIZE_4K, TLB_MAS2_IO, TLB_MAS3_KERN, 0, 0, TLB_MAS8_HV);

	vaddr = temp_mapping[0] + (paddr & (PAGE_SIZE - 1));

	// Get the actual instruction that caused the trap
	// This uses the external pid load instruction, which needs the EPLC
	// SPR set up first.
	guestmem_set_insn(regs);
	ret = guestmem_in32((uint32_t *)regs->srr0, &insn);
	if (ret != GUESTMEM_OK) {
		if (ret == GUESTMEM_TLBMISS)
			regs->exc = EXC_ITLB;
		else {
			printlog(LOGTYPE_EMU, LOGLEVEL_ERROR,
				 "%s: guestmem_in32() returned %d\n", __func__, ret);
			regs->exc = EXC_ISI;
		}
		reflect_trap(regs);
		return;
	}

	if (unlikely(emu_load_store(regs, insn, vaddr))) {
		regs->exc = EXC_PROGRAM;
		mtspr(SPR_ESR, ESR_PIL);
		reflect_trap(regs);
		return;
	}

	regs->srr0 += 4;
}
