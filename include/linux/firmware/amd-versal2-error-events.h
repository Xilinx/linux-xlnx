/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Versal2 Error Event Node IDs and Error Event Mask.
 * Use with AMD Event Management Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * Naman Trivedi <naman.trivedimanojbhai@amd.com>
 */

#ifndef _FIRMWARE_AMD_VERSAL2_ERROR_EVENTS_H_
#define _FIRMWARE_AMD_VERSAL2_ERROR_EVENTS_H_

/*
 * Error Event Node Ids
 */
#define VERSAL2_EVENT_ERROR_PMC_ERR1		(0x28100000U)
#define VERSAL2_EVENT_ERROR_PMC_ERR2		(0x28104000U)
#define VERSAL2_EVENT_ERROR_PMC_ERR3		(0x28108000U)
#define VERSAL2_EVENT_ERROR_LPDSLCR_ERR1	(0x2810C000U)
#define VERSAL2_EVENT_ERROR_LPDSLCR_ERR2	(0x28110000U)
#define VERSAL2_EVENT_ERROR_LPDSLCR_ERR3	(0x28114000U)
#define VERSAL2_EVENT_ERROR_LPDSLCR_ERR4	(0x28118000U)
#define VERSAL2_EVENT_ERROR_SW_ERR		(0x2811C000U)

/*
 * Error Event Mask for register: PMC_ERR1_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_PMC_ERR1
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_BOOT_CR: Error event mask for PMC Boot
 * Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_BOOT_CR				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_BOOT_NCR: Error event mask for PMC Boot
 * Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_BOOT_NCR				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FW_CR: Error event mask for PMC Firmware
 * Boot Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FW_CR				BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FW_NCR: Error event mask for PMC Firmware
 * Boot Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FW_NCR				BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GSW_CR: Error event mask for General
 * Software Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GSW_CR				BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GSW_NCR: Error event mask for General
 * Software Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GSW_NCR				BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CFU: Error event mask for CFU Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CFU				BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CFRAME: Error event mask for CFRAME Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CFRAME				BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_0: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_0				BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_1: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_1				BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DDRMB_CR: Error event mask for DDRMC MB
 * Correctable ECC Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DDRMB_CR				BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DDRMB_NCR: Error event mask for DDRMC MB
 * Non-Correctable ECC Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DDRMB_NCR				BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOCTYPE1_CR: Error event mask for NoC Type1
 * Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOCTYPE1_CR			BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOCTYPE1_NCR: Error event mask for NoC
 * Type1 Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOCTYPE1_NCR			BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOCUSER: Error event mask for NoC User Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOCUSER				BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMCM: Error event mask for MMCM Lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMCM				BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_AIE_CR: Error event mask for ME
 * Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_AIE_CR				BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_AIE_NCR: Error event mask for ME
 * Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_AIE_NCR				BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DDRMC_CR: Error event mask for DDRMC MC
 * Correctable ECC Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DDRMC_CR				BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DDRMC_NCR: Error event mask for DDRMC MC
 * Non-Correctable ECC Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DDRMC_NCR				BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GT_CR: Error event mask for GT
 * Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GT_CR				BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GT_NCR: Error event mask for GT
 * Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GT_NCR				BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL_SMON_CR: Error event mask for PL
 * Sysmon Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL_SMON_CR				BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL_SMON_NCR: Error event mask for PL
 * Sysmon Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL_SMON_NCR			BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL0: Error event mask for User defined PL
 * generic Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL0				BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL1: Error event mask for User defined PL
 * generic Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL1				BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL2: Error event mask for User defined PL
 * generic Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL2				BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PL3: Error event mask for User defined PL
 * generic Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PL3				BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NPI_ROOT: Error event mask for NPI Root Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NPI_ROOT				BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT3: Error event mask for SSIT Error from
 * Slave SLR1.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT3				BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT4: Error event mask for SSIT Error
 * from Slave SLR2.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT4				BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT5: Error event mask for SSIT Error from
 * Slave SLR3.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT5				BIT(31)

/*
 * Error Event Mask for register: PMC_ERR2_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_PMC_ERR2
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_APB: Error event mask for General purpose
 * PMC Error, can be triggered by any of the following peripherals:
 * - PMC Global Regsiters,- PMC Clock & Reset (CRP),- PMC IOU Secure SLCR,
 * - PMC IOU SLCR,- BBRAM Controller,- PMC Analog Control Registers,
 * - RTC Control Registers.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_APB				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_ROM: Error event mask for PMC ROM
 * Validation Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_ROM				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MB_FATAL0: Error event mask for PMC PPU0 MB
 * TMR Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MB_FATAL0				BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MB_FATAL1: Error event mask for PMC PPU1 MB
 * TMR Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MB_FATAL1				BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_2: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_2				BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_CR: Error event mask for PMC
 * Correctable Errors,
 * - PPU0 RAM correctable Error.
 * - PPU1 instruction RAM correctable Error.
 * - PPU1 data RAM correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_CR				BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_NCR: Error event mask for PMC
 * Non-Correctable Errors,
 * - PPU0 RAM non-correctable Error.,PPU1 instruction RAM non-correctable Error.
 * - PPU1 data RAM non-correctable Error.,PRAM non-correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_NCR				BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON0: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[0].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON0				BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON1: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[1].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON1				BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON2: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[2].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON2				BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON3: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[3].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON3				BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON4: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[4].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON4				BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON5: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[5].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON5				BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON6: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[6].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON6				BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON7: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[7].
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON7				BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON8: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[8].
 * Indicates an over-temperature alarm.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON8				BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON9: Error event mask for PMC
 * Temperature Shutdown Alert and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[9].
 * Indicates a device temperature alarm.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMC_SMON9				BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CFI: Error event mask for CFI Non-Correctable
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CFI				BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SEUCRC: Error event mask for CFRAME SEU CRC
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SEUCRC				BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SEUECC: Error event mask for CFRAME SEU ECC
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SEUECC				BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PMX_WWDT: Error event mask for PMC WWDT Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PMX_WWDT				BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_3: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_3				BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RTC_ALARM: Error event mask for RTC Alarm Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RTC_ALARM				BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NPLL: Error event mask for PMC NPLL Lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NPLL				BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PPLL: Error event mask for PMC PPLL Lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PPLL				BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CLK_MON: Error event mask for Clock Monitor
 * Errors, collected from CRP's CLKMON_STATUS register.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CLK_MON				BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_4: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_4				BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_PMX_CORR_ERR: rror event mask for PMC
 * interconnect correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_PMX_CORR_ERR			BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_PMX_UNCORR_ERR: Error event mask for PMC
 * interconnect uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_PMX_UNCORR_ERR			BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT0: Error event mask for Master SLR:
 * - SSIT Error from Slave SLR1.
 * - For Slave SLRs: SSIT Error0 from Master SLR.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT0				BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT1: Error event mask for Master SLR:
 * - SSIT Error from Slave SLR2.
 * - For Slave SLRs: SSIT Error1 from Master SLR.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT1				BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SSIT2: Error event mask for Master SLR:
 * - SSIT Error from Slave SLR3.
 * - For Slave SLRs: SSIT Error2 from Master SLR.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SSIT2				BIT(31)

/*
 * Error Event Mask for register: PMC_ERR3_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_PMC_ERR3
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_IOU_CR: Error event mask for PMC IOU
 * correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_IOU_CR				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_IOU_NCR: Error event mask for PMC IOU
 * uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_IOU_NCR				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DFX_UXPT_ACT: Error event mask for
 * DFX unexpected activation.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DFX_UXPT_ACT			BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DICE_CDI_PAR: Error event mask for DICE
 * CDI SEED parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DICE_CDI_PAR			BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PRIV: Error event mask for Device
 * identity private key parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PRIV				BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NXTSW_CDI_PAR: Error event mask for
 * Next SW CDI SEED parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NXTSW_CDI_PAR			BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PRIV: Error event mask for
 * Device attestation private key parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PRIV				BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DME_PUB_X: Error event mask for DME
 * public key X component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DME_PUB_X				BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DME_PUB_Y: Error event mask for DME
 * public key Y component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DME_PUB_Y				BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PUB_X: Error event mask for
 * DEVAK public key X component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PUB_X			BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PUB_Y: Error event mask for
 * DEVAK public key Y component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVAK_PUB_Y			BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PUB_X: Error event mask for
 * DEVIK public key X component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PUB_X			BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PUB_Y: Error event mask for
 * DEVIK public key Y component's parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEVIK_PUB_Y			BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PCR_PAR: Error event mask for PCR parity.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PCR_PAR				BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E0: Error event mask for
 * LPX detected EAM Err group 0. Sent to PL via signal
 * pmc_error_to_pl[55], readable from jtag register JTAG_ERROR_STATUS[55].
 */
#define XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E0				BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E1: Error event mask for LPX
 * detected EAM Err group 1. ent to PL via signal pmc_error_to_pl[54],
 * readable from jtag register JTAG_ERROR_STATUS[54].
 */
#define XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E1				BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E2: Error event mask for LPX
 * detected EAM Err group 2. Sent to PL via signal pmc_error_to_pl[20],
 * readable from jtag register JTAG_ERROR_STATUS[20].
 */
#define XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E2				BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E3: Error event mask for LPX
 * detected EAM Err group 3. Sent to PL via signal pmc_error_to_pl[19],
 * readable from jtag register JTAG_ERROR_STATUS[19].
 */
#define XPM_VERSAL2_EVENT_ERROR_PSX_EAM_E3				BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_ASU_EAM_GD: Error event mask ASU Glitch
 * Detect Valid. Sent to PL via signal pmc_error_to_pl[18],
 * readable from jtag register JTAG_ERROR_STATUS[18].
 */
#define XPM_VERSAL2_EVENT_ERROR_ASU_EAM_GD				BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_GD: Error event mask PMC Glitch
 * Detect Valid. Sent to PL via signal pmc_error_to_pl[64],
 * readable from jtag register JTAG_ERROR_STATUS[155].
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_GD				BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_SMIRQ0: Error event mask SYSMON
 * IRQ[0]. Sent to PL via signal pmc_error_to_pl[65], readable from
 * jtag register JTAG_ERROR_STATUS[156].
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_SMIRQ0				BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_SMIRQ1: Error event mask SYSMON
 * IRQ[1]. Sent to PL via signal pmc_error_to_pl[66], readable from
 * jtag register JTAG_ERROR_STATUS[157].
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_SMIRQ1				BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_PRAM: Error event mask PRAM IRQ.
 * Sent to PL via signal pmc_error_to_pl[67],
 * readable from jtag register JTAG_ERROR_STATUS[158].
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_PRAM				BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_AGERR: Error event mask AES GO SW
 * Programming Error. Sent to PL via signal pmc_error_to_pl[68],
 * readable from jtag register JTAG_ERROR_STATUS[159].
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_AGERR				BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_PMC_EAM_UFSFE: Error event mask UFSHC Fatal
 * Error. Sent to PL via signal pmc_error_to_pl[69], readable from
 * jtag register JTAG_ERROR_STATUS.
 */
#define XPM_VERSAL2_EVENT_ERROR_PMC_EAM_UFSFE				BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_5: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_5				BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_6: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_6				BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_7: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_7				BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_8: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_8				BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_9: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_9				BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_10: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_10				BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_11: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_11				BIT(31)

/*
 * Error Event Mask for register: EAM_ERR0_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_LPDSLCR_ERR1
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PS_SW_CR: Error event mask for PS
 * Software Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PS_SW_CR				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PS_SW_NCR: Error event mask for PS
 * Software Non-Correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PS_SW_NCR				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_USB_ERR: Error event mask for
 * aggregated LPX USB Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_USB_ERR				BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_DFX: Error event mask for
 * aggregated LPX DFX controllers * unexpected activation Error
 * (from slcr reg LPX_DFX_ERR_ISR).
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_DFX				BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_UFSHC_FE_IRQ: Error event mask for
 * UFSHC FE IRQ Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_UFSHC_FE_IRQ			BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_APLL1_LOCK: Error event mask
 * for APLL1 lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_APLL1_LOCK				BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_APLL2_LOCK: Error event mask for
 * APLL2 lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_APLL2_LOCK				BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPLL_LOCK: Error event mask for RPLL
 * Lock Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPLL_LOCK				BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FLXPLL_LOCK: Error event mask for
 * FLXPLL Lock Errors. The Error can be unmasked after the PLL is
 * locked to alert when the PLL loses lock.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FLXPLL_LOCK			BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILB_CR: Error event mask for
 * aggregated LPX ASIL B correctable errors from OCMASILB, LPXASILB, IOU.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILB_CR			BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILB_NCR: Error event mask for
 * aggregated LPX ASIL B uncorrectable errors from OCMASILB, LPXASILB, IOU.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILB_NCR			BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILD_CR: Error event mask for
 * aggregated LPX ASIL D correctable errors from OCMASILD, LPXASILD.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILD_CR			BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILD_NCR: Error event mask for
 * aggregated LPX ASIL D uncorrectable errors from OCMASILD, LPXASILD.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_LPXASILD_NCR			BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILD_CR: Error event mask for
 * aggregated FPX ASIL D correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILD_CR			BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILD_NCR: Error event mask for
 * aggregated FPX ASIL D uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILD_NCR			BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILB_CR: Error event mask for
 * aggregated FPX ASIL B correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILB_CR			BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILB_NCR: Error event mask for
 * aggregated FPX ASIL B uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_FPXASILB_NCR			BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_SPLIT_CR: Error event mask for
 * splitter interconnect correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_SPLIT_CR			BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_INT_SPLIT_NCR: Error event mask for
 * splitter interconnect uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_INT_SPLIT_NCR			BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_12: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_12				BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_13: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_13				BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_14: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_14				BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_15: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_15				BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NMU_FIREWALL_WR_ERR: Error event mask
 * for Firewall write Errors from NOC NMUs.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NMU_FIREWALL_WR_ERR		BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NMU_FIREWALL_RD_ERR: Error event
 * mask for Firewall read Error from NOC NMU.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NMU_FIREWALL_RD_ERR		BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NSU_FIREWALL_ERR: Error event mask
 * for Firewall Error from NOC NSU.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_NOC_NSU_FIREWALL_ERR		BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FMU_ERR: Error event mask for GIC_FMU_ERR.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FMU_ERR			BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FMU_FAULT: Error event mask for
 * GIC_FMU_FAULT.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FMU_FAULT			BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_16: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_16				BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_17: Error event mask for reserved Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RSVD_17				BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_IPI_ERR: Error event mask for
 * aggregated IPI Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_IPI_ERR				BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_CPI: Error event mask for
 * aggregated CPI Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_CPI				BIT(31)

/*
 * Event Mask for register: EAM_ERR1_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_LPDSLCR_ERR2
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT0: Error event mask for FPD WDT0 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT0				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT1: Error event mask for FPD WDT1 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT1				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT2: Error event mask for FPD WDT2 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT2				BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT3: Error event mask for FPD WDT3 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_WDT3				BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER0_NON_FATAL_ERR: Error
 * event mask for Non Fatal Error for Splitter 0.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER0_NON_FATAL_ERR	BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER1_NON_FATAL_ERR: Error
 * event mask for Non Fatal Error for Splitter 1.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER1_NON_FATAL_ERR	BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER2_NON_FATAL_ERR: Error
 * event mask for Non Fatal Error for Splitter 2.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER2_NON_FATAL_ERR	BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER3_NON_FATAL_ERR: Error
 * event mask for Non Fatal Error for Splitter 3.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER3_NON_FATAL_ERR	BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER_FATAL_ERR: Error event
 * mask for aggregated Fatal Error For Splitter0-3.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PSXC_SPLITTER_FATAL_ERR		BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GIC_ERR: Error event mask for
 * aggregated GIC Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GIC_ERR				BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FAULT: Error event mask for
 * aggregated GIC fault.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_GIC_FAULT				BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CMN_FAULT: Error event mask for
 * aggregated CMN faults from all PD domains and FMU
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CMN_FAULT				BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_CMN_ERR: Error event mask for
 * aggregated CMN Errors from all PD domains and FMU.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_CMN_ERR				BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ACP_ERR: Error event mask for
 * aggregated Errors from ACP0 + ACP1.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ACP_ERR				BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU0_ERI: Error event mask for
 * APU Cluster 0 fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU0_ERI			BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU0_FHI: Error event mask for
 * APU Cluster 0 non-fatal/fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU0_FHI			BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU1_ERI: Error event mask for
 * APU Cluster 1 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU1_ERI			BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU1_FHI: Error event mask for
 * APU Cluster 1 non-fatal/fatal.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU1_FHI			BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU2_ERI: Error event mask for
 * APU Cluster 2 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU2_ERI			BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU2_FHI: Error event mask for APU
 * Cluster 2 non-fatal/fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU2_FHI			BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU3_ERI: Error event mask for
 * APU Cluster 3 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU3_ERI			BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU3_FHI: Error event mask for APU
 * Cluster 3 non-fatal/fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_APU3_FHI			BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_MMU_ERR: Error event mask for
 * aggregated MMU Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_MMU_ERR			BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_MMU_FAULT: Error event mask for
 * aggregated MMU fault.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_MMU_FAULT			BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_SLCR_ERR: Error event mask for
 * SLCR Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_SLCR_ERR			BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_SLCR_SECURE_ERR: Error event mask for
 * SLCR SECURE Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_SLCR_SECURE_ERR		BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM0_NONFATAL_ERR: Error event mask for
 * Non Fatal Error from AFI FM0 in FPX.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM0_NONFATAL_ERR		BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM1_NONFATAL_ERR: Error event mask for
 * Non Fatal Error from AFI FM1 in FPX.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM1_NONFATAL_ERR		BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM2_NONFATAL_ERR: Error event mask for
 * Non Fatal Error from AFI FM2 in FPx.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM2_NONFATAL_ERR		BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM3_NONFATAL_ERR: Error event mask for
 * Non Fatal Error from AFI FM3 in FPX.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFM3_NONFATAL_ERR		BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_BFPX_AFIFS_CORR_ERR: Error event mask for
 * FPXAFIFS Corr Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_BFPX_AFIFS_CORR_ERR		BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFS_UNCORR_ERR: Error event mask for
 * FPXAFIFS UnCorr Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPX_AFIFS_UNCORR_ERR		BIT(31)

/*
 * Error Event Mask for register: EAM_ERR2_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_LPDSLCR_ERR3
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE_CLUSTER_FATAL: Error event mask for
 * aggregated RPU Cluster A cluster + core Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE_CLUSTER_FATAL		BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE0_NON_FATAL: Error event mask for
 * RPUA Core0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE0_NON_FATAL		BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE1_NON_FATAL: Error event mask for
 * RPUA Core1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUA_CORE1_NON_FATAL		BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE_CLUSTER_FATAL: Error event mask for
 * aggregated RPU Cluster B cluster + core Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE_CLUSTER_FATAL		BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE0_NON_FATAL: Error event mask for
 * RPUB Core0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE0_NON_FATAL		BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE1_NON_FATAL: Error event mask for
 * RPUB Core1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUB_CORE1_NON_FATAL		BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE_CLUSTER_FATAL: Error event mask for
 * aggregated RPU Cluster C cluster + core Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE_CLUSTER_FATAL		BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE0_NON_FATAL: Error event mask for
 * RPUC Core0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE0_NON_FATAL		BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE1_NON_FATAL: Error event mask for
 * RPUC Core1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUC_CORE1_NON_FATAL		BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE_CLUSTER_FATAL: Error event mask for
 * aggregated RPU Cluster D cluster + core Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE_CLUSTER_FATAL		BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE0_NON_FATAL: Error event mask for
 * RPUD Core0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE0_NON_FATAL		BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE1_NON_FATAL: Error event mask for
 * RPUD Core1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUD_CORE1_NON_FATAL		BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE_CLUSTER_FATAL: Error event mask for
 * aggregated RPU Cluster E cluster + core Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE_CLUSTER_FATAL		BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE0_NON_FATAL: Error event mask
 * for RPUE Core0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE0_NON_FATAL		BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE1_NON_FATAL: Error event mask
 * for RPUE Core1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPUE_CORE1_NON_FATAL		BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_RPU_PCIL_ERR: Error event mask for PCIL ERR
 * FOR RPU Clusters.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_RPU_PCIL_ERR			BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM0_NONFATAL_ERR: Error event mask for OCM
 * Bank0 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM0_NONFATAL_ERR			BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM0_FATAL_ERR: Error event mask for
 * OCM Bank0 Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM0_FATAL_ERR			BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM1_NONFATAL_ERR: Error event mask
 * for OCM Bank1 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM1_NONFATAL_ERR			BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM1_FATAL_ERR: Error event mask for
 * OCM Bank1 Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM1_FATAL_ERR			BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM2_NONFATAL_ERR: Error event mask
 * for OCM Bank2 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM2_NONFATAL_ERR			BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM2_FATAL_ERR: Error event mask for OCM
 * Bank2 Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM2_FATAL_ERR			BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM3_NONFATAL_ERR: Error event mask for OCM
 * Bank3 NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM3_NONFATAL_ERR			BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_OCM3_FATAL_ERR: Error event mask for OCM Bank3
 * Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_OCM3_FATAL_ERR			BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT0: Error event mask for LPX WDT0 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT0				BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT1: Error event mask for LPX WDT1 Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT1				BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT2: Error event mask for LPX WDT2
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT2				BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT3: Error event mask for LPX WDT3
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT3				BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT4: Error event mask for LPX WDT4
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_WWDT4				BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_LS_ERR: Error event mask for ADMA LS
 * Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_LS_ERR			BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_GLITCH_DET0: Error event mask for
 * LPX Glitch Detector0 glitch detected.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_GLITCH_DET0			BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_GLITCH_DET1: Error event mask for
 * LPX Glitch Detector1 glitch detected.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_GLITCH_DET1			BIT(31)

/*
 * Event Mask for register: EAM_ERR3_STATUS
 * For Node_Id: VERSAL2_EVENT_ERROR_LPDSLCR_ERR4
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_FPD_CRF: Error event mask for FPD
 * Reset Monitor ERROR.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_FPD_CRF				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPD_MON_ERR: Error event mask for LPD
 * reset and Clock Monitor Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPD_MON_ERR			BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_AFIFM_FATAL_ERR: Error event mask for
 * Fatal Error from all AFI FM.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_AFIFM_FATAL_ERR			BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFM_NONFATAL_ERR: Error event
 * mask for LPX AFI FM Non Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFM_NONFATAL_ERR		BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPD_ASU_FATAL: Error event mask for
 * aggregated ASU + ASU_PL Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPD_ASU_FATAL			BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPD_ASU_NON_FATAL: Error event mask for
 * aggregated ASU + ASU_PL NonFatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPD_ASU_NON_FATAL			BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFS_CORR_ERR: Error event mask for
 * LPX AFI FS Non Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFS_CORR_ERR			BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFS_UNCORR_ERR: Error event mask for
 * LPX AFI FS Fatal Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_LPX_AFIFS_UNCORR_ERR		BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_CORR_EVENT: Error event mask for MMI
 * top level correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_CORR_EVENT			BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_UNCORR_EVENT: Error event mask for
 * MMI top level uncorrectable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_UNCORR_EVENT			BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_GPU_COR_EVENT: Error event mask for
 * MMI gpu correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_GPU_COR_EVENT			BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_PCIE0_COR_EVENT: Error event mask for
 * MMI pcie0 correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_PCIE0_COR_EVENT		BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_PCIE1_COR_EVENT: Error event mask
 * for MMI pcie1 correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_PCIE1_COR_EVENT		BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_GEM_COR_EVENT: Error event mask
 * for MMI gem correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_GEM_COR_EVENT			BIT(13)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_DC_COR_EVENT: Error event mask for MMI
 * dc correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_DC_COR_EVENT			BIT(14)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_MMI_UDH_COR_EVENT: Error event mask for MMI
 * udh correctable Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_MMI_UDH_COR_EVENT			BIT(15)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR1: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR1				BIT(16)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR2: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR2				BIT(17)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR3: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR3				BIT(18)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR4: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR4				BIT(19)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR5: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR5				BIT(20)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR6: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR6				BIT(21)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR7: Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR7				BIT(22)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR8:Error event mask for ADMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_ADMA_ERR8				BIT(23)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR1: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR1				BIT(24)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR2: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR2				BIT(25)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR3: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR3				BIT(26)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR4: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR4				BIT(27)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR5: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR5				BIT(28)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR6: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR6				BIT(29)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR7: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR7				BIT(30)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR8: Error event mask for SDMA per
 * channel Error.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_SDMA_ERR8				BIT(31)

/*
 * Error Event Mask belongs to SW ERR node,
 * For Node_Id: VERSAL2_EVENT_ERROR_SW_ERR
 */

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_0: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_0				BIT(0)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_1: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_1				BIT(1)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_2: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_2				BIT(2)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_3: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_3				BIT(3)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PLM_EXCEPTION: Error event mask for PLM
 * Exception.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PLM_EXCEPTION			BIT(4)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_DEV_STATE_CHANGE: Error event mask for
 * Dev state change.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_DEV_STATE_CHANGE			BIT(5)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_PCR_LOG_UPDATE: Error event mask for PCR
 * Log update.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_PCR_LOG_UPDATE			BIT(6)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_CRAM_CE: Error event mask for handling
 * correctable Error in Versal Configuration RAM which is reported by
 * Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_CRAM_CE			BIT(7)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_CRAM_UE: Error event mask for handling
 * uncorrectable Error in Versal Configuration RAM which is reported by
 * Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_CRAM_UE			BIT(8)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_NPI_UE: Error event mask for handling
 * uncorrectable Error in Versal NoC programming interface (NPI).
 * register which is reported by Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_XSEM_NPI_UE			BIT(9)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_4: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_4				BIT(10)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_5: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_5				BIT(11)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_6: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_6				BIT(12)

/**
 * XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_7: Health Boot Monitoring Errors.
 */
#define XPM_VERSAL2_EVENT_ERROR_MASK_HB_MON_7				BIT(13)

#endif /* _FIRMWARE_AMD_VERSAL2_ERROR_EVENTS_H_ */
