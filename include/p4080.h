
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

#ifndef HV_P4080_H
#define HV_P4080_H

#define LAW_ENABLE 0x80000000
#define LAWAR_TARGETID_SHIFT 20
#define LAWAR_CSDID_SHIFT 12
#define LAWAR_CSDID_MASK  0x000ff000
#define LAW_TARGETID_MASK 0x0ff00000
#define LAW_SIZE_MASK 0x3f

#define LAW_SIZE_4KB 11
#define LAW_SIZE_64GB 35

/* Define LAW targets here */
#define LAW_TRGT_DDR1	0x10
#define LAW_TRGT_DDR2	0x11
#define LAW_TRGT_INTLV	0x14

typedef struct {
	uint32_t high, low, attr, reserved;
} law_t;

#define CPCCSR0 0
#define CPCCSR0_CPCE 0x80000000
#define CPCCSR0_CPCFL 0x800
#define CPCCSR0_CPCLFC 0x400

#define CPCPIR0 0x200
#define CPCPIR0_RESET_MASK 0xFFFFFFFF
#define CPCPAR0 0x208
#define CPCPWR0 0x20C

/* this mask is specified as the reset value 0.902 block guide */
#define CPCPAR0_RESET_MASK 0xFFFFFBFF
/* DRDPEALLOC, DRDFEALLOC, IRDALLOC, DRDALLOC, WCOALLOC,
 * READUALLOC, WRITEUALLOC, ADSTASHEN, CNSTASHEN
 */
#define CPCPAR_MASK 0x3FB

#define NUM_PART_REGS 16

#define NUM_SNOOP_IDS 32

/* CCF error registers */
#define CCF_CEDR         0xA00 // CCF error detect register
#define   CCF_CEDR_MINT       0x2 // Multiple intervention response
#define   CCF_CEDR_LAE        0x1 // Local access error
#define   CCF_ERR_MASK        (CCF_CEDR_MINT | CCF_CEDR_LAE)

#define CCF_CEER         0xA04 // CCF error enable register
#define   CCF_CEER_MINT       0x00000002 // Enable MINT error reporting
#define   CCF_CEER_LAE        0x00000001 // Enable LAE error reporting

#define CCF_CECAR        0xA0C // CCF error capture attribute register
#define   CCF_CECAR_SRC       0xff000000 // Transaction source
#define   CCF_CECAR_UVT       0x00008000 // Unavailable target
#define   CCF_CECAR_VAL       0x00000001 // Error capture attribute register is valid

#define CCF_CECADRH      0xA10 // CCF error capture high address register
#define CCF_CECADRL      0xA14 // CCF error capture low address register

#define CCF_CMECAR       0xA18 // CCF error capture attribute register
#define   CCF_CMECAR_SYND     0xff000000 // Intervention response per port
#define   CCF_CMECAR_MINT     0x00000200 // MINT detected
#define   CCF_CMECAR_SOLE     0x00000100 // SOLE data and intervention detected

/* CPC error registers */
#define CPC_CAPTDATAHI     0xE20 //CPC Error capture data high register
#define CPC_CAPTDATALO     0xE24 //CPC Error capture data low register

#define CPC_CAPTECC        0xE28 //CPC Error capture ECC syndrome register

#define CPC_ERRDET         0xE40 //CPC error detect register
#define   CPC_ERRDET_MULLERR    0x80000000 //Multiple CPC errors
#define   CPC_ERRDET_TMHITERR   0x00000080 //Tag multi way hit error
#define   CPC_ERRDET_TMBECCERR  0x00000040 //Tag or status multi bit ecc error
#define   CPC_ERRDET_TSBECCERR  0x00000020 //Tag or status single bit ecc error
#define   CPC_ERRDET_DMBECCERR  0x00000008 //Data multi bit ECC error
#define   CPC_ERRDET_DSBECCERR  0x00000004 //Data single bit ECC error
#define   CPC_ERR_MASK         (CPC_ERRDET_TMHITERR | CPC_ERRDET_TMBECCERR \
	 | CPC_ERRDET_TSBECCERR | CPC_ERRDET_DMBECCERR | CPC_ERRDET_DSBECCERR)

#define CPC_ERRDIS         0xE44 //CPC error disable register
#define   CPC_ERRDIS_TMHITDIS   0x00000080 //Tag multi way hit error disable
#define   CPC_ERRDIS_TMBECCDIS  0x00000040 //Tag or status multi bit ecc error disable
#define   CPC_ERRDIS_TSBECCDIS  0x00000020 //Tag or status single bit ecc error disable
#define   CPC_ERRDIS_DMBECCDIS  0x00000008 //Data multi bit ECC error disable
#define   CPC_ERRDIS_DSBECCDIS  0x00000004 //Data single bit ECC error disable

#define CPC_ERRINTEN       0xE48 //CPC error interrupt enable register
#define   CPC_ERRINTEN_TMHITINTEN   0x00000080 //Tag multi way hit error interrupt enable
#define   CPC_ERRINTEN_TMBECCINTEN  0x00000040 //Tag or status multi bit ecc error interrupt enable
#define   CPC_ERRINTEN_TSBECCINTEN  0x00000020 //Tag or status single bit ecc error interrupt enable
#define   CPC_ERRINTEN_DMBECCINTEN  0x00000008 //Data multi bit ECC error interrupt enable
#define   CPC_ERRINTEN_DSBECCINTEN  0x00000004 //Data single bit ECC error interrupt enable

#define CPC_ERRATTR       0xE4C //CPC error attribute register
#define CPC_ERREADDR      0xE50 //CPC error extended address register
#define CPC_ERRADDR       0xE54 //CPC error address register

#define CPC_ERRCTL        0xE58 //CPC error control register
#define   CPC_ERRCTL_DATA_CNT_MASK  0x000000ff //Mask for Data ECC count
#define   CPC_ERRCTL_TAG_CNT_MASK   0x0000ff00 //Mask for Tag ECC count

#define CPC_ERRCTL_THRESH_SHIFT 16

/* DDR error registers */
#define DDR_ERR_CAPT_DATA_HI     0xE20 //Error capture data high
#define DDR_ERR_CAPT_DATA_LO     0xE24 //Error capture data low

#define DDR_ERR_CAPT_ECC         0xE28 //Data path read capture ECC

#define DDR_ERR_DET              0xE40 //Memory error detect
#define  DDR_ERR_DET_MME             0x80000000 //Multiple memory errors
#define  DDR_ERR_DET_APE             0x00000100 //Address parity error
#define  DDR_ERR_DET_ACE             0x00000080 //Automatic calibration error
#define  DDR_ERR_DET_CDE             0x00000010 //Corrupted data error
#define  DDR_ERR_DET_MBE             0x00000008 //Multi bit error
#define  DDR_ERR_DET_SBE             0x00000004 //Single bit error
#define  DDR_ERR_DET_MSE             0x00000001 //Memory select error
#define  DDR_ERR_MASK                (DDR_ERR_DET_APE | DDR_ERR_DET_ACE \
	 | DDR_ERR_DET_CDE | DDR_ERR_DET_MBE | DDR_ERR_DET_SBE | DDR_ERR_DET_MSE)

#define DDR_ERR_DIS              0xE44 //Memory error dsable
#define  DDR_ERR_DIS_APED             0x00000100 //Address parity error disable
#define  DDR_ERR_DIS_ACED             0x00000080 //Automatic calibration error disable
#define  DDR_ERR_DIS_CDED             0x00000010 //Corrupted data error disable
#define  DDR_ERR_DIS_MBED             0x00000008 //Multi bit error disable
#define  DDR_ERR_DIS_SBED             0x00000004 //Single bit error disable
#define  DDR_ERR_DIS_MSED             0x00000001 //Memory select error disable

#define DDR_ERR_INT_EN           0xE48 //Memory error int enable
#define  DDR_ERR_INT_EN_APE             0x00000100 //Address parity error int enable
#define  DDR_ERR_INT_EN_ACE             0x00000080 //Automatic calibration error int enable
#define  DDR_ERR_INT_EN_CDE             0x00000010 //Corrupted data error int enable
#define  DDR_ERR_INT_EN_MBE             0x00000008 //Multi bit error int enable
#define  DDR_ERR_INT_EN_SBE             0x00000004 //Single bit error int enable
#define  DDR_ERR_INT_EN_MSE             0x00000001 //Memory select error int enable

#define DDR_ERR_ATTR            0xE4C //Error attributes register
#define DDR_ERR_ATTR_VALID      0x1

#define DDR_ERR_ADDR            0xE50 //Error address register
#define DDR_ERR_EXT_ADDR        0xE54 //Extended error address register
#define DDR_ERR_SB_MGMT         0xE58 //Single bit ECC error management
#define DDR_SB_THRESH_SHIFT 16

#endif
