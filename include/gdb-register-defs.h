/** @file
 * (Register definitions for GDB Stub and target description generator).
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

/* References:
 * 0. e500mc Core Reference Manual: Programming Model
 *    E500MCRM, Rev. C, 09/2008
 * 1. E500_mc Changes #2 Version 1.0 (6/04/08)
 * 2. E500_mc Changes #2 Version 3.0 (6/20/08)
 * 3. Freescale P4080 Embedded Hypervisor Software Architecture
 *    Rev. 0.85 1/10/2009
 * 4. dump_reg (01/12/2009)
 *    PowerPC Register Definition Dumper, Version v2_9_0
 *    Using Reglib-base library version: v10_3_1
 *          Reglib-core library version: v48_6_0
 *          Reglib-device library version: v16_0_0
 */

#include <libos/core-regs.h>

/* We define _ASM because we want the upper "non-C" part of
 * libos/fsl-booke-tlb.h (No assembly language connotation here unlike
 * in other parts of the hypervisor where _ASM could be defined by an
 * assembly language file to indicate that it needs to include the "non-C"
 * part of libos/fsl-booke-tlb.h).
 */
#define _ASM
#include <libos/fsl-booke-tlb.h>

/* Note: If you change reg_cat, add to reg_cat_names and make sure you also
 * make the change in tools/tdgen/emit_td.c::emit_e500mc_reg_table.
 */
typedef enum reg_cat
{
	reg_cat_unk,
#define REG_CAT_UNK "reg_cat_unk"

	reg_cat_gpr,
#define REG_CAT_GPR "reg_cat_gpr"

	reg_cat_fpr,
#define REG_CAT_FPR "reg_cat_fpr"

	reg_cat_pc,
#define REG_CAT_PC "reg_cat_pc"

	reg_cat_msr,
#define REG_CAT_MSR "reg_cat_msr"

	reg_cat_cr,
#define REG_CAT_CR "reg_cat_cr"

	reg_cat_spr,
#define REG_CAT_SPR "reg_cat_spr"

	reg_cat_pmr,
#define REG_CAT_PMR "reg_cat_pmr"

	reg_cat_fpscr,
#define REG_CAT_FPSCR "reg_cat_fpscr"

	/* Note: Always keep reg_cat_count last. (No such reg cat BTW). */
	reg_cat_count,
} reg_cat_t;

char *reg_cat_names[] =
{
	REG_CAT_UNK,
	REG_CAT_GPR,
	REG_CAT_FPR,
	REG_CAT_PC,
	REG_CAT_MSR,
	REG_CAT_CR,
	REG_CAT_SPR,
	REG_CAT_PMR,
	REG_CAT_FPSCR
};

struct register_description
{
	char *description;
	char *name;
	char *bitsize;
	char *regnum;
	char *save_restore;
	char *type;
	char *group;
	reg_cat_t cat;
	int inum; /* SPR number for SPR's. In general,
	             inums are defined per category. */
};

struct register_description e500mc_power_core_gprs[] =
{
	{ .name="r0", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=0, },
	{ .name="r1", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=1, },
	{ .name="r2", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=2, },
	{ .name="r3", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=3, },
	{ .name="r4", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=4, },
	{ .name="r5", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=5, },
	{ .name="r6", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=6, },
	{ .name="r7", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=7, },
	{ .name="r8", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=8, },
	{ .name="r9", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=9, },
	{ .name="r10", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=10, },
	{ .name="r11", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=11, },
	{ .name="r12", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=12, },
	{ .name="r13", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=13, },
	{ .name="r14", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=14, },
	{ .name="r15", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=15, },
	{ .name="r16", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=16, },
	{ .name="r17", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=17, },
	{ .name="r18", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=18, },
	{ .name="r19", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=19, },
	{ .name="r20", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=20, },
	{ .name="r21", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=21, },
	{ .name="r22", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=22, },
	{ .name="r23", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=23, },
	{ .name="r24", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=24, },
	{ .name="r25", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=25, },
	{ .name="r26", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=26, },
	{ .name="r27", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=27, },
	{ .name="r28", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=28, },
	{ .name="r29", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=29, },
	{ .name="r30", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=30, },
	{ .name="r31", .bitsize="32", .type="uint32", .cat=reg_cat_gpr,
	  .inum=31, },
};

struct register_description e500mc_power_fpu_fprs[] =
{
	{ .name="f0", .bitsize="64", .type="ieee_double",
	  .regnum="32",
	  .cat=reg_cat_fpr, .inum=0, },
	{ .name="f1", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=1, },
	{ .name="f2", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=2, },
	{ .name="f3", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=3, },
	{ .name="f4", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=4, },
	{ .name="f5", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=5, },
	{ .name="f6", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=6, },
	{ .name="f7", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=7, },
	{ .name="f8", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=8, },
	{ .name="f9", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=9, },
	{ .name="f10", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=10, },
	{ .name="f11", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=11, },
	{ .name="f12", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=12, },
	{ .name="f13", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=13, },
	{ .name="f14", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=14, },
	{ .name="f15", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=15, },
	{ .name="f16", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=16, },
	{ .name="f17", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=17, },
	{ .name="f18", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=18, },
	{ .name="f19", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=19, },
	{ .name="f20", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=20, },
	{ .name="f21", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=21, },
	{ .name="f22", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=22, },
	{ .name="f23", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=23, },
	{ .name="f24", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=24, },
	{ .name="f25", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=25, },
	{ .name="f26", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=26, },
	{ .name="f27", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=27, },
	{ .name="f28", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=28, },
	{ .name="f29", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=29, },
	{ .name="f30", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=30, },
	{ .name="f31", .bitsize="64", .type="ieee_double",
	  .cat=reg_cat_fpr, .inum=31, },
};

struct register_description e500mc_power_core_pc[] =
{
	{ .name="pc", .bitsize="32", .type="code_ptr", .regnum="64",
	  .cat=reg_cat_pc, },
};

struct register_description e500mc_power_core_msr[] =
{
	{ .name="msr", .bitsize="32", .type="uint32", .cat=reg_cat_msr, },
};

struct register_description e500mc_power_core_cr[] =
{
	{ .name="cr", .bitsize="32", .type="uint32", .cat=reg_cat_cr, },
};

struct register_description e500mc_power_core_sprs[] =
{
	{ .name="lr", .bitsize="32", .type="code_ptr", .cat=reg_cat_spr,
	  .inum=SPR_LR, },
	{ .name="ctr", .bitsize="32", .type="uint32", .cat=reg_cat_spr,
	  .inum=SPR_CTR, },
	{ .name="xer", .bitsize="32", .type="uint32", .cat=reg_cat_spr,
	  .inum=SPR_XER, },
};

struct register_description e500mc_power_fpu_fpscr[] =
{
	{ .name="fpscr", .bitsize="64", .group="float", .regnum="70",
	  .cat=reg_cat_fpscr, /* FPSCR is unique - so no .inum */ },
};

struct register_description e500mc_sprs[] =
{
	{ .description = "Alternate Time Base Register Lower",
	  .name="atbl", .bitsize="32", .regnum="71", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_ATBL, },
	{ .description = "Alternate Time Base Register Upper",
	  .name="atbu", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_ATBU, },
	{ .description = "Branch Unit Control and Status Register",
	  .name="bucsr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_BUCSR, },
	{ .description = "Core Device Control and Status Register 0",
	  .name="cdcsr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_CDCSR0, },
	{ .description = "Critical Save/Restore Register 0",
	  .name="csrr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_CSRR0, },
	{ .description = "Critical Save/Restore Register 1",
	  .name="csrr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_CSRR1, },
	{ .description = "Data Address Compare 1",
	  .name="dac1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DAC1, },
	{ .description = "Data Address Compare 2",
	  .name="dac2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DAC2, },
	{ .description = "Debug Control Register 0",
	  .name="dbcr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBCR0, },
	{ .description = "Debug Control Register 1",
	  .name="dbcr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBCR1, },
	{ .description = "Debug Control Register 2",
	  .name="dbcr2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBCR2, },
	{ .description = "Debug Control Register 4",
	  .name="dbcr4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBCR4, },
	{ .description = "Debug Status Register",
	  .name="dbsr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBSR, },
#ifndef TOPAZ
	{ .description = "Debug Status Register Write Register",
	  .name="dbsrwr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DBSRWR, },
#endif
	{ .description = "Debug Data Acquisition Message",
	  .name="ddam", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DDAM, },
	{ .description = "Data Exception Address Register",
	  .name="dear", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DEAR, },
	{ .description = "Decrementer",
	  .name="dec", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DEC, },
	{ .description = "Decrementer Auto-Reload",
	  .name="decar", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DECAR, },
	{ .description = "Debug Event Select Register",
	  .name="devent", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DEVENT, },
	{ .description = "Debug Save/Restore Register 0",
	  .name="dsrr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DSRR0, },
	{ .description = "Debug Save/Restore Register 1",
	  .name="dsrr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_DSRR1, },
#ifndef TOPAZ
	{ .description = "Embedded Processor Control Register",
	  .name="epcr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_EPCR, },
#endif
	{ .description = "External PID Load Context Register",
	  .name="eplc", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_EPLC, },
	{ .description = "External Proxy Register",
	  .name="epr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_EPR, },
	{ .description = "External PID Store Context Register",
	  .name="epsc", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_EPSC, },
	{ .description = "Exception Syndrome Register",
	  .name="esr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_ESR, },
	{ .description = "Guest Data Exception Address Register",
	  .name="gdear", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GDEAR, },
	{ .description = "Guest External Proxy Register",
	  .name="gepr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GEPR, },
#ifndef TOPAZ
	{ .description = "Guest Exception Syndrome Register",
	  .name="gesr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GESR, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Data TLB Error Register",
	  .name="givor13", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR13, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Instruction TLB Error",
	  .name="givor14", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR14, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Data Storage Interrupt",
	  .name="givor2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR2, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Instruction Storage Interrupt",
	  .name="givor3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR3, },
#endif
#ifndef TOPAZ
	{ .description = "Guest External Input",
	  .name="givor4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR4, },
#endif
#ifndef TOPAZ
	{ .description = "Guest System Call",
	  .name="givor8", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVOR8, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Interrupt Vector Prefix Register",
	  .name="givpr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GIVPR, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Processor Identification Register",
	  .name="gpir", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GPIR, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Special Purpose Register General 0",
	  .name="gsprg0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSPRG0, },
	{ .description = "Guest Special Purpose Register General 1",
	  .name="gsprg1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSPRG1, },
	{ .description = "Guest Special Purpose Register General 2",
	  .name="gsprg2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSPRG2, },
	{ .description = "Guest Special Purpose Register General 3",
	  .name="gsprg3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSPRG3, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Save/Restore Register 0",
	  .name="gsrr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSRR0, },
#endif
#ifndef TOPAZ
	{ .description = "Guest Save/Restore Register 1",
	  .name="gsrr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_GSRR1, },
#endif
#ifdef EXPOSE_HDBCRS
	/* Do not expose the hdbcr's. */
	{ .description = "Hardware Debug Control Register 0",
	  .name="hdbcr0", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR0, },
	{ .description = "Hardware Debug Control Register 1",
	  .name="hdbcr1", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR1, },
	{ .description = "Hardware Debug Control Register 2",
	  .name="hdbcr2", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR2, },
	{ .description = "Hardware Debug Control Register 3",
	  .name="hdbcr3", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR3, },
	{ .description = "Hardware Debug Control Register 4",
	  .name="hdbcr4", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR4, },
	{ .description = "Hardware Debug Control Register 5",
	  .name="hdbcr5", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR5, },
	{ .description = "Hardware Debug Control Register 6",
	  .name="hdbcr6", .bitsize="32", .save_restore="no",
	  .group="general", .inum=SPR_HDBCR6, },
#endif
	{ .description = "Hardware Implementation-Dependent Register 0",
	  .name="hid0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_HID0, },
	{ .description = "Instruction Address Compare 1",
	  .name="iac1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IAC1, },
	{ .description = "Instruction Address Compare 2",
	  .name="iac2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IAC2, },
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 0 (Critical Input)",
	  .name="ivor0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR0, },
#endif
	{ .description = "Interrupt Vector Offset Register 1 (Machine Check)",
	  .name="ivor1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR1, },
	{ .description = "Interrupt Vector Offset Register 10 (Decrementer)",
	  .name="ivor10", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR10, },
	{ .description = "Interrupt Vector Offset Register 11 "
			 "(Fixed-Interval Timer Interrupt)",
	  .name="ivor11", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR11, },
	{ .description = "Interrupt Vector Offset Register 12 "
			 "(Watchdog Timer Interrupt)",
	  .name="ivor12", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR12, },
	{ .description = "Interrupt Vector Offset Register 13 (Data TLB Error)",
	  .name="ivor13", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR13, },
	{ .description = "Interrupt Vector Offset Register 14 "
			 "(Instruction TLB Error)",
	  .name="ivor14", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR14, },
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 15 (Debug)",
	  .name="ivor15", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR15, },
#endif
	{ .description = "Interrupt Vector Offset Register 2 (Data Storage)",
	  .name="ivor2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR2, },
	{ .description = "Interrupt Vector Offset Register 3 "
			 "(Instruction Storage)",
	  .name="ivor3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR3, },
	{ .description = "Interrupt Vector Offset Register 35 "
		         "(Performance Monitor)",
	  .name="ivor35", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR35, },
	{ .description = "Interrupt Vector Offset Register 36"
	                 "(Processor Doorbell Interrupt)",
	  .name="ivor36", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR36, },
	{ .description = "Interrupt Vector Offset Register 37"
	                 "(Processor Doorbell Critical Interrupt)",
	  .name="ivor37", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR37, },
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 38"
	                 "(Guest Processor Doorbell)",
	  .name="ivor38", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR38, },
#endif
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 39"
	                 "(Guest Processor Doorbell Critical"
	                 " and "
	                 "Machine Check)",
	  .name="ivor39", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR39, },
#endif
	{ .description = "Interrupt Vector Offset Register 4 (External Input)",
	  .name="ivor4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR4, },
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 40"
	                 "(Hypervisor System Call)",
	  .name="ivor40", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR40, },
#endif
#ifndef TOPAZ
	{ .description = "Interrupt Vector Offset Register 41"
	                 "(Hypervisor Privilege)",
	  .name="ivor41", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR41, },
#endif
	{ .description = "Interrupt Vector Offset Register 5 (Alignment)",
	  .name="ivor5", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR5, },
	{ .description = "Interrupt Vector Offset Register 6 (Program)",
	  .name="ivor6", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR6, },
	{ .description = "Interrupt Vector Offset Register 7 "
			 "(Floating-Point Unavailable)",
	  .name="ivor7", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR7, },
	{ .description = "Interrupt Vector Offset Register 8 (System Call)",
	  .name="ivor8", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR8, },
	{ .description = "Interrupt Vector Offset Register 9 "
			 "(Auxillary Processor Unavailable)",
	  .name="ivor9", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVOR9, },
	{ .description = "Interrupt Vector Prefix Register",
	  .name="ivpr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_IVPR, },
	{ .description = "L1 Cache Configure Register 0",
	  .name="l1cfg0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L1CFG0, },
	{ .description = "L1 Cache Configure Register 1",
	  .name="l1cfg1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L1CFG1, },
	{ .description = "L1 Cache Control and Status Register 0",
	  .name="l1csr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L1CSR0, },
	{ .description = "L1 Cache Control and Status Register 1",
	  .name="l1csr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L1CSR1, },
	{ .description = "L1 Cache Control and Status Register 2",
	  .name="l1csr2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L1CSR2, },
	{ .description = "L2 Cache error data high capture register",
	  .name="l2captdatahi", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CAPTDATAHI, },
	{ .description = "L2 Cache error data low capture register",
	  .name="l2captdatalo", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CAPTDATALO, },
	{ .description = "L2 Cache error capture ECC syndrome register",
	  .name="l2captecc", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CAPTECC, },
	{ .description = "L2 Cache Configuration Register 0",
	  .name="l2cfg0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CFG0, },
	{ .description = "L2 Cache Control and Status Register 0",
	  .name="l2csr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CSR0, },
	{ .description = "L2 Cache Control and Status Register 1",
	  .name="l2csr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2CSR1, },
	{ .description = "L2 Cache error address capture register",
	  .name="l2erraddr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRADDR, },
	{ .description = "L2 Cache error attributes register",
	  .name="l2errattr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRATTR, },
	{ .description = "L2 Cache error control register",
	  .name="l2errctl", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRCTL, },
	{ .description = "L2 Cache Error Detect Register",
	  .name="l2errdet", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRDET, },
	{ .description = "L2 Cache Error Disable Register",
	  .name="l2errdis", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRDIS, },
	{ .description = "L2 Cache Error extended address capture register",
	  .name="l2erreaddr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERREADDR, },
	{ .description = "L2 Cache error injection control register",
	  .name="l2errinjctl", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRINJCTL, },
	{ .description = "L2 Cache error injection mask high register",
	  .name="l2errinjhi", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRINJHI, },
	{ .description = "L2 Cache error injection mask low register",
	  .name="l2errinjlo", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRINJLO, },
	{ .description = "L2 Cache error interrupt enable register",
	  .name="l2errinten", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_L2ERRINTEN, },
#ifndef TOPAZ
	{ .description = "Logical Partition ID Format Register",
	  .name="lpidr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_LPIDR, },
#endif
	{ .description = "MMU Assist Register 0",
	  .name="mas0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS0, },
	{ .description = "MMU Assist Register 1",
	  .name="mas1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS1, },
	{ .description = "MMU Assist Register 2",
	  .name="mas2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS2, },
	{ .description = "MMU Assist Register 3",
	  .name="mas3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS3, },
	{ .description = "MMU Assist Register 4",
	  .name="mas4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS4, },
#ifndef TOPAZ
	{ .description = "MMU Assist Register 5",
	  .name="mas5", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS5, },
#endif
	{ .description = "MMU Assist Register 6",
	  .name="mas6", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS6, },
	{ .description = "MMU Assist Register 7",
	  .name="mas7", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS7, },
#ifndef TOPAZ
	{ .description = "MMU Assist Register 8",
	  .name="mas8", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MAS8, },
#endif
	{ .description = "Machine Check Address Register",
	  .name="mcar", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MCAR, },
	{ .description = "Machine Check Address Register Upper",
	  .name="mcaru", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MCARU, },
	{ .description = "Machine Check Syndrome Register",
	  .name="mcsr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MCSR, },
	{ .description = "Machine Check Save/Restore Register 0",
	  .name="mcsrr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MCSRR0, },
	{ .description = "Machine Check Save/Restore Register 1",
	  .name="mcsrr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MCSRR1, },
	{ .description = "MMU Configuration Register",
	  .name="mmucfg", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MMUCFG, },
	{ .description = "MMU Control and Status Register 0",
	  .name="mmucsr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MMUCSR0, },
#ifndef TOPAZ
	{ .description = "MSR Protect Register",
	  .name="msrp", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_MSRP, },
#endif
	{ .description = "Nexus Processor ID Register",
	  .name="npidr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_NPIDR, },
	{ .description = "Nexus SPR Access Configuration Register",
	  .name="nspc", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_NSPC, },
	{ .description = "Nexus SPR Access Data Register",
	  .name="nspd", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_NSPD, },
	{ .description = "Process ID Register",
	  .name="pid", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_PID, },
	{ .description = "Processor ID Register",
	  .name="pir", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_PIR, },
	{ .description = "Processor Version Register",
	  .name="pvr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_PVR, },
	{ .description = "SPR General 0",
	  .name="sprg0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG0, },
	{ .description = "SPR General 1",
	  .name="sprg1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG1, },
	{ .description = "SPR General 2",
	  .name="sprg2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG2, },
	{ .description = "SPR General 3",
	  .name="sprg3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG3, },
	{ .description = "SPR General 4",
	  .name="sprg4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG4, },
	{ .description = "SPR General 5",
	  .name="sprg5", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG5, },
	{ .description = "SPR General 6",
	  .name="sprg6", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG6, },
	{ .description = "SPR General 7",
	  .name="sprg7", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG7, },
	{ .description = "SPR General Register 8",
	  .name="sprg8", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG8, },
	{ .description = "SPR General Register 9",
	  .name="sprg9", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SPRG9, },
	{ .description = "Save/Restore Register 0",
	  .name="srr0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SRR0, },
	{ .description = "Save/Restore Register 1",
	  .name="srr1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SRR1, },
	{ .description = "System Version Register",
	  .name="svr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_SVR, },
	{ .description = "Supervisor Time Base Lower",
	  .name="tbl", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TBL, },
	{ .description = "Supervisor Time Base Upper",
	  .name="tbu", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TBU, },
	{ .description = "Timer Control Register",
	  .name="tcr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TCR, },
	{ .description = "TLB Configuration Register 0",
	  .name="tlb0cfg", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TLB0CFG, },
	{ .description = "TLB Configuration Register 1",
	  .name="tlb1cfg", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TLB1CFG, },
	{ .description = "Timer Status Register",
	  .name="tsr", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_TSR, },
	{ .description = "User SPR General 0",
	  .name="usprg0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG0, },
	{ .description = "User SPR General 3",
	  .name="usprg3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG3, },
	{ .description = "User SPR General 4",
	  .name="usprg4", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG4, },
	{ .description = "User SPR General 5",
	  .name="usprg5", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG5, },
	{ .description = "User SPR General 6",
	  .name="usprg6", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG6, },
	{ .description = "User SPR General 7",
	  .name="usprg7", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_USPRG7, },
	{ .description = "User Time Base Lower",
	  .name="utbl", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_UTBL, },
	{ .description = "User Time Base Upper",
	  .name="utbu", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_spr, .inum=SPR_UTBU, },
};

struct register_description e500mc_pmrs[] =
{
	{ .description = "Performance Monitor Counter Register 0",
	  .name="pmc0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMC0, },
	{ .description = "Performance Monitor Counter Register 1",
	  .name="pmc1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMC1, },
	{ .description = "Performance Monitor Counter Register 2",
	  .name="pmc2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMC2, },
	{ .description = "Performance Monitor Counter Register 3",
	  .name="pmc3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMC3, },
	{ .description = "Performance Monitor Global Control Register 0",
	  .name="pmgc0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMGC0, },
	{ .description = "Performance Monitor Local Control A Register 0",
	  .name="pmlca0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCA0, },
	{ .description = "Performance Monitor Local Control A Register 1",
	  .name="pmlca1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCA1, },
	{ .description = "Performance Monitor Local Control A Register 2",
	  .name="pmlca2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCA2, },
	{ .description = "Performance Monitor Local Control A Register 3",
	  .name="pmlca3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCA3, },
	{ .description = "Performance Monitor Local Control B Register 0",
	  .name="pmlcb0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCB0, },
	{ .description = "Performance Monitor Local Control B Register 1",
	  .name="pmlcb1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCB1, },
	{ .description = "Performance Monitor Local Control B Register 2",
	  .name="pmlcb2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCB2, },
	{ .description = "Performance Monitor Local Control B Register 3",
	  .name="pmlcb3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_PMLCB3, },
	{ .description = "User Performance Monitor Counter Register 0",
	  .name="upmc0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMC0, },
	{ .description = "User Performance Monitor Counter Register 1",
	  .name="upmc1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMC1, },
	{ .description = "User Performance Monitor Counter Register 2",
	  .name="upmc2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMC2, },
	{ .description = "User Performance Monitor Counter Register 3",
	  .name="upmc3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMC3, },
	{ .description = "User Performance Monitor Global Control Register 0",
	  .name="upmgc0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMGC0, },
	{ .description = "User Performance Monitor Local Control A Register 0",
	  .name="upmlca0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCA0, },
	{ .description = "User Performance Monitor Local Control A Register 1",
	  .name="upmlca1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCA1, },
	{ .description = "User Performance Monitor Local Control A Register 2",
	  .name="upmlca2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCA2, },
	{ .description = "User Performance Monitor Local Control A Register 3",
	  .name="upmlca3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCA3, },
	{ .description = "User Performance Monitor Local Control B Register 0",
	  .name="upmlcb0", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCB0, },
	{ .description = "User Performance Monitor Local Control B Register 1",
	  .name="upmlcb1", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCB1, },
	{ .description = "User Performance Monitor Local Control B Register 2",
	  .name="upmlcb2", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCB2, },
	{ .description = "User Performance Monitor Local Control B Register 3",
	  .name="upmlcb3", .bitsize="32", .save_restore="no",
	  .group="general", .cat=reg_cat_pmr, .inum=PMR_UPMLCB3, },
};

#define ECOUNT(a) sizeof((a))/sizeof((a)[0])
#define E500MC_POWER_CORE_GPRS_COUNT ECOUNT(e500mc_power_core_gprs)
#define E500MC_POWER_FPU_FPRS_COUNT ECOUNT(e500mc_power_fpu_fprs)
#define E500MC_POWER_CORE_PC_COUNT ECOUNT(e500mc_power_core_pc)
#define E500MC_POWER_CORE_MSR_COUNT ECOUNT(e500mc_power_core_msr)
#define E500MC_POWER_CORE_CR_COUNT ECOUNT(e500mc_power_core_cr)
#define E500MC_POWER_CORE_SPRS_COUNT ECOUNT(e500mc_power_core_sprs)
#define E500MC_POWER_FPU_FPSCR_COUNT ECOUNT(e500mc_power_fpu_fpscr)
#define E500MC_SPRS_COUNT ECOUNT(e500mc_sprs)
#define E500MC_PMRS_COUNT ECOUNT(e500mc_pmrs)
#define E500MC_REG_COUNT E500MC_POWER_CORE_GPRS_COUNT + \
                         E500MC_POWER_FPU_FPRS_COUNT + \
                         E500MC_POWER_CORE_PC_COUNT + \
                         E500MC_POWER_CORE_MSR_COUNT + \
                         E500MC_POWER_CORE_CR_COUNT + \
                         E500MC_POWER_CORE_SPRS_COUNT + \
                         E500MC_POWER_FPU_FPSCR_COUNT + \
                         E500MC_SPRS_COUNT + \
                         E500MC_PMRS_COUNT
