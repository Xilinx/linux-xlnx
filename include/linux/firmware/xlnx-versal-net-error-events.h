/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Versal NET Error Event Node IDs and Error Event Mask.
 * Use with Xilinx Event Management Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Jay Buddhabhatti <jay.buddhabhatti@amd.com>
 */

#ifndef _FIRMWARE_XLNX_VERSAL_NET_ERROR_EVENTS_H_
#define _FIRMWARE_XLNX_VERSAL_NET_ERROR_EVENTS_H_

/*
 * Error Event Node Ids
 */
#define VERSAL_NET_EVENT_ERROR_PMC_ERR1	(0x28100000U)
#define VERSAL_NET_EVENT_ERROR_PMC_ERR2	(0x28104000U)
#define VERSAL_NET_EVENT_ERROR_PMC_ERR3	(0x28108000U)
#define VERSAL_NET_EVENT_ERROR_PSM_ERR1	(0x2810C000U)
#define VERSAL_NET_EVENT_ERROR_PSM_ERR2	(0x28110000U)
#define VERSAL_NET_EVENT_ERROR_PSM_ERR3	(0x28114000U)
#define VERSAL_NET_EVENT_ERROR_PSM_ERR4	(0x28118000U)
#define VERSAL_NET_EVENT_ERROR_SW_ERR	(0x2811C000U)

/*
 * Error Event Mask belongs to PMC ERR1 node.
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PMC_ERR1
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_BOOT_CR: Error event mask for PMC Boot Correctable Error.
 * Set by ROM code during ROM execution during Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_BOOT_CR		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_BOOT_NCR: Error event mask for PMC Boot Non-Correctable Error.
 * Set by ROM code during ROM execution during Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_BOOT_NCR		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FW_CR: Error event mask for PMC Firmware Boot Correctable Error.
 * Set by PLM during firmware execution during Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FW_CR		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FW_NCR: Error event mask for PMC Firmware Boot Non-Correctable Error.
 * Set by PLM during firmware execution during Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FW_NCR		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_GSW_CR: Error event mask for General Software Correctable Error.
 * Set by any processors after Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_GSW_CR		BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_GSW_NCR: Error event mask for General Software Non-Correctable Error.
 * Set by any processors after Boot.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_GSW_NCR		BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CFU: Error event mask for CFU Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CFU		BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CFRAME: Error event mask for CFRAME Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CFRAME		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_PSM_CR: Error event mask for PSM Correctable Error,
 * Summary from PSM Error Management.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_PSM_CR		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_PSM_NCR: Error event mask for PSM Non-Correctable Error,
 * Summary from PSM Error Management.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_PSM_NCR	BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMB_CR: Error event mask for DDRMC MB Correctable ECC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMB_CR		BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMB_NCR: Error event mask for DDRMC MB Non-Correctable ECC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMB_NCR		BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCTYPE1_CR: Error event mask for NoC Type1 Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCTYPE1_CR	BIT(12)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCTYPE1_NCR: Error event mask for NoC Type1 Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCTYPE1_NCR	BIT(13)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCUSER: Error event mask for NoC User Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NOCUSER		BIT(14)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MMCM: Error event mask for MMCM Lock Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MMCM		BIT(15)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AIE_CR: Error event mask for ME Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AIE_CR		BIT(16)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AIE_NCR: Error event mask for ME Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AIE_NCR		BIT(17)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMC_CR: Error event mask for DDRMC MC Correctable ECC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMC_CR		BIT(18)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMC_NCR: Error event mask for DDRMC MC Non-Correctable ECC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DDRMC_NCR		BIT(19)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_GT_CR: Error event mask for GT Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_GT_CR		BIT(20)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_GT_NCR: Error event mask for GT Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_GT_NCR		BIT(21)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PLSMON_CR: Error event mask for PL Sysmon Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PLSMON_CR		BIT(22)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PLSMON_NCR: Error event mask for PL Sysmon Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PLSMON_NCR		BIT(23)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PL0: Error event mask for User defined PL generic error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PL0		BIT(24)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PL1: Error event mask for User defined PL generic error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PL1		BIT(25)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PL2: Error event mask for User defined PL generic error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PL2		BIT(26)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PL3: Error event mask for User defined PL generic error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PL3		BIT(27)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NPIROOT: Error event mask for NPI Root Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NPIROOT		BIT(28)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT3: Error event mask for SSIT Error from Slave SLR1,
 * Only used in Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT3		BIT(29)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT4: Error event mask for SSIT Error from Slave SLR2,
 * Only used in Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT4		BIT(30)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT5: Error event mask for SSIT Error from Slave SLR3,
 * Only used in Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT5		BIT(31)

/*
 * Error Event Mask belongs to PMC ERR2 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PMC_ERR2
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCAPB: Error event mask for General purpose PMC error,
 * can be triggered by any of the following peripherals:,
 * - PMC Global Regsiters,- PMC Clock & Reset (CRP),- PMC IOU Secure SLCR,
 * - PMC IOU SLCR,- BBRAM Controller,- PMC Analog Control Registers,
 * - RTC Control Registers.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCAPB		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCROM: Error event mask for PMC ROM Validation Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCROM		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL0: Error event mask for PMC PPU0 MB TMR Fatal Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL0		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL1: Error event mask for PMC PPU1 MB TMR Fatal Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL1		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_CR: Error event mask for PMC Correctable Errors,
 * PPU0 RAM correctable error.,PPU1 instruction RAM correctable error.,
 * PPU1 data RAM correctable error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_CR		BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_NCR: Error event mask for PMC Non-Correctable Errors,
 * PPU0 RAM non-correctable error.,PPU1 instruction RAM non-correctable error.,
 * PPU1 data RAM non-correctable error.,PRAM non-correctable error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMC_NCR		BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON0: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[0].
 * Indicates an alarm condition on any of SUPPLY0 to SUPPLY31.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON0		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON1: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[1].
 * Indicates an alarm condition on any of SUPPLY32 to SUPPLY63.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON1		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON2: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[2].
 * Indicates an alarm condition on any of SUPPLY64 to SUPPLY95.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON2		BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON3: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[3].
 * Indicates an alarm condition on any of SUPPLY96 to SUPPLY127.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON3		BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON4: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[4].
 * Indicates an alarm condition on any of SUPPLY128 to SUPPLY159.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON4		BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON8: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[8].
 * Indicates an over-temperature alarm.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON8		BIT(15)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON9: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[9].
 * Indicates a device temperature alarm.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMCSMON9		BIT(16)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CFI: Error event mask for CFI Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CFI		BIT(17)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SEUCRC: Error event mask for CFRAME SEU CRC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SEUCRC		BIT(18)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SEUECC: Error event mask for CFRAME SEU ECC Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SEUECC		BIT(19)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PMX_WWDT: Error event mask for PMC WWDT Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PMX_WWDT		BIT(20)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RTCALARM: Error event mask for RTC Alarm Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RTCALARM		BIT(22)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NPLL: Error event mask for PMC NPLL Lock Error,
 * This error can be unmasked after the NPLL is locked to alert when the
 * NPLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NPLL		BIT(23)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PPLL: Error event mask for PMC PPLL Lock Error,
 * This error can be unmasked after the PPLL is locked to alert when the
 * PPLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PPLL		BIT(24)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CLKMON: Error event mask for Clock Monitor Errors.,
 * Collected from CRP's CLKMON_STATUS register.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CLKMON		BIT(25)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PMX_CORR_ERR: Error event mask for PMC interconnect
 * correctable error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PMX_CORR_ERR	BIT(27)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PMX_UNCORR_ERR: Error event mask for PMC interconnect
 * uncorrectable error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PMX_UNCORR_ERR	BIT(28)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT0: Error event mask for Master SLR:
 * SSIT Error from Slave SLR1.,
 * For Slave SLRs: SSIT Error0 from Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT0		BIT(29)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT1: Error event mask for Master SLR:
 * SSIT Error from Slave SLR2.,
 * For Slave SLRs: SSIT Error1 from Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT1		BIT(30)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT2: Error event mask for Master SLR:
 * SSIT Error from Slave SLR3.,
 * For Slave SLRs: SSIT Error2 from Master SLR.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_SSIT2		BIT(31)

/*
 * Error Event Mask belongs to PMC ERR3 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PMC_ERR3
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_IOU_CR: Error event mask for PMC IOU correctable error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_IOU_CR		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_IOU_NCR: Error event mask for PMC IOU uncorrectable
 * error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_IOU_NCR		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DFX_UXPT_ACT: Error event mask for DFX unexpected
 * activation
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DFX_UXPT_ACT	BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DICE_CDI_PAR: Error event mask for DICE CDI SEED parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DICE_CDI_PAR	BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PRIV: Error event mask for Device identity private
 * key parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PRIV		BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NXTSW_CDI_PAR: Error event mask for Next SW CDI SEED
 * parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NXTSW_CDI_PAR	BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PRIV: Error event mask for Device attestation
 * private key parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PRIV		BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DME_PUB_X: Error event mask for DME public key X
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DME_PUB_X		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DME_PUB_Y: Error event mask for DME public key Y
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DME_PUB_Y		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PUB_X: Error event mask for DEVAK public key X
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PUB_X	BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PUB_Y: Error event mask for DEVAK public key Y
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVAK_PUB_Y	BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PUB_X: Error event mask for DEVIK public key X
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PUB_X	BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PUB_Y: Error event mask for DEVIK public key Y
 * component's parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_DEVIK_PUB_Y	BIT(12)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PCR_PAR: Error event mask for PCR parity
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PCR_PAR		BIT(13)

/*
 * Error Event Mask belongs to PSM ERR1 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PSM_ERR1
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PS_SW_CR: Error event mask for PS Software can write to
 * trigger register to generate this Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PS_SW_CR		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PS_SW_NCR: Error event mask for PS Software can write to
 * trigger register to generate this Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PS_SW_NCR		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_B_CR: Error event mask for PSM Firmware can write to
 * trigger register to generate this Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_B_CR		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_B_NCR: Error event mask for PSM Firmware can write to
 * trigger register to generate this Non-Correctable Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_B_NCR		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL: Error event mask for Or of MB Fatal1, Fatal2, Fatal3 Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MB_FATAL		BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_CR: Error event mask for PSM Correctable.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_CR		BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_NCR: Error event mask for PSM Non-Correctable.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSM_NCR		BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSMX_CHK: Error event mask for PSMX CHK error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSMX_CHK		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APLL1_LOCK: Error event mask for APLL1 lock error. The error
 * can be unmasked after the PLL is locked to alert when the PLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APLL1_LOCK		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APLL2_LOCK: Error event mask for APLL2 lock error. The error
 * can be unmasked after the PLL is locked to alert when the PLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APLL2_LOCK		BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RPLL_LOCK: Error event mask for RPLL Lock Errors. The error
 * can be unmasked after the PLL is locked to alert when the PLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RPLL_LOCK		BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FLXPLL_LOCK: Error event mask for FLXPLL Lock Errors. The
 * error can be unmasked after the PLL is locked to alert when the PLL loses lock.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FLXPLL_LOCK	BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PSM_CR: Error event mask for INT_PSM correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PSM_CR		BIT(12)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PSM_NCR: Error event mask for INT_PSM non-correctable
 * error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_PSM_NCR	BIT(13)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_USB2: Error event mask for Consolidated Error from the two
 * USB2 blocks.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_USB2		BIT(14)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_UXPT_ACT: Error event mask for LPX unexpected dfx
 * activation error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_UXPT_ACT	BIT(15)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_LPD_CR: Error event mask for INT_LPD correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_LPD_CR		BIT(17)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_LPD_NCR: Error event mask for INT_LPD non-correctable
 * error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_LPD_NCR	BIT(18)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_OCM_CR: Error event mask for INT_OCM correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_OCM_CR		BIT(19)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_OCM_NCR: Error event mask for INT_OCM non-correctable
 * error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_OCM_NCR	BIT(20)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_FPD_CR: Error event mask for INT_FPD correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_FPD_CR		BIT(21)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_FPD_NCR: Error event mask for INT_FPD non-correctable
 * error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_FPD_NCR	BIT(22)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_IOU_CR: Error event mask for INT_IOU correctable Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_IOU_CR		BIT(23)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_IOU_NCR: Error event mask for INT_IOU non-correctable
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_INT_IOU_NCR	BIT(24)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RPUA_LOCKSTEP: Error event mask for RPU lockstep error for
 * ClusterA
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RPUA_LOCKSTEP	BIT(25)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RPUB_LOCKSTEP: Error event mask for RPU lockstep error for
 * ClusterB
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RPUB_LOCKSTEP	BIT(26)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_GIC_AXI: Error event mask for err_int_irq from APU
 * GIC Distributer
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_GIC_AXI	BIT(27)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_GIC_ECC: Error event mask for fault_int_irq from APU
 * GIC Distributer
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_GIC_ECC	BIT(28)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CPM_CR: Error event mask for CPM correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CPM_CR		BIT(29)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CPM_NCR: Error event mask for CPM non-correctable error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CPM_NCR		BIT(30)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_CPI: Error event mask for CPI error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_CPI		BIT(31)

/*
 * Error Event Mask belongs to PSM ERR2 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PSM_ERR2
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT0: Error event mask for FPD WDT0 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT0		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT1: Error event mask for FPD WDT1 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT1		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT2: Error event mask for FPD WDT2 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT2		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT3: Error event mask for FPD WDT3 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_WDT3		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER0: Error event mask for Memory Errors for
 * Splitter0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER0	BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER0: Error event mask for Consolidated
 * Error indicating AXI parity Error for Splitter0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER0	BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER1: Error event mask for Memory Errors for
 * Splitter1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER1	BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER1: Error event mask for Consolidated
 * Error indicating AXI parity Error for Splitter1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER1	BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER2: Error event mask for Memory Errors for
 * Splitter2
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER2	BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER2: Error event mask for Consolidated
 * Error indicating AXI parity Error for Splitter2
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER2	BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER3: Error event mask for Memory Errors for
 * Splitter3
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_MEM_SPLITTER3	BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER3: Error event mask for Consolidated
 * Error indicating AXI parity Error for Splitter3
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_AXI_PAR_SPLITTER3	BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER0: Error event mask for APU Cluster 0 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER0	BIT(12)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER1: Error event mask for APU Cluster 1 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER1	BIT(13)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER2: Error event mask for APU Cluster 2 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER2	BIT(14)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER3: Error event mask for APU Cluster 3 error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_APU_CLUSTER3	BIT(15)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_WWDT0: Error event mask for WWDT0 LPX Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_WWDT0		BIT(16)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_WWDT1: Error event mask for WWDT0 LPX Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_WWDT1		BIT(17)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_ADMA_LOCKSTEP: Error event mask for ADMA Lockstep Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_ADMA_LOCKSTEP	BIT(18)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_IPI: Error event mask for IPI Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_IPI		BIT(19)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK0_CR: Error event mask for OCM Bank0 Corr Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK0_CR	BIT(20)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK1_CR: Error event mask for OCM Bank1 Corr Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK1_CR	BIT(21)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK0_NCR: Error event mask for OCM Bank0 UnCorr
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK0_NCR	BIT(22)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK1_NCR: Error event mask for OCM Bank1 UnCorr
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_OCM_BANK1_NCR	BIT(23)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPXAFIFS_CR: Error event mask for LPXAFIFS Corr Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPXAFIFS_CR	BIT(24)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPXAFIFS_NCR: Error event mask for LPXAFIFS UnCorr
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPXAFIFS_NCR	BIT(25)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_GLITCH_DETECT0: Error event mask for LPX Glitch
 * Detector0 glitch detected.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_GLITCH_DETECT0	BIT(26)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_GLITCH_DETECT1: Error event mask for LPX Glitch
 * Detector1 glitch detected.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPX_GLITCH_DETECT1	BIT(27)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_WR_NOC_NMU: Error event mask for Firewall write
 * errors from NOC NMUs
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_WR_NOC_NMU	BIT(28)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_RD_NOC_NMU: Error event mask for Firewall read
 * error from NOC NMU
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_RD_NOC_NMU	BIT(29)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_NOC_NSU: Error event mask for Firewall error from
 * NOC NSU
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FWALL_NOC_NSU	BIT(30)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_A0: Error event mask for Bit[18] from R52 Core
 * A0, Err event
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_A0		BIT(31)

/*
 * Error Event Mask belongs to PSM ERR3 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PSM_ERR3
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_A1: Error event mask for Bit[18] from R52 Core
 * A1, Err event
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_A1		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_B0: Error event mask for Bit[18] from R52 Core
 * B0, Err event
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_B0		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_B1: Error event mask for Bit[18] from R52 Core
 * B1, Err event
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B18_R52_B1		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_CR: Error event mask for R52 A0 Core Correctable
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_CR		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_TFATAL: Error event mask for R52 A0 Core TFatal
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_TFATAL	BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_TIMEOUT: Error event mask for R52 A0 Core Timeout
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A0_TIMEOUT	BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUA0: Error event mask for Bit[24:20] pf
 * ERREVNT for RPUA0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUA0	BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUA0: Error event mask for Bit[25] of ERREVNT for
 * RPUA0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUA0		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_CR: Error event mask for R52 A1 Core Correctable
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_CR		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_TFATAL: Error event mask for R52 A1 Core TFatal
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_TFATAL	BIT(9)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_TIMEOUT: Error event mask for R52 A1 Core Timeout
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_A1_TIMEOUT	BIT(10)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUA1: Error event mask for Bit[24:20] pf
 * ERREVNT for RPUA1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUA1	BIT(11)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUA1: Error event mask for Bit[25] of ERREVNT for
 * RPUA1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUA1		BIT(12)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_CR: Error event mask for R52 A1 Core Correctable
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_CR		BIT(13)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_TFATAL: Error event mask for R52 A1 Core TFatal
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_TFATAL	BIT(14)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_TIMEOUT: Error event mask for R52 A1 Core Timeout
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B0_TIMEOUT	BIT(15)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUB0: Error event mask for Bit[24:20] pf
 * ERREVNT for RPUB0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUB0	BIT(16)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUB0: Error event mask for Bit[25] of ERREVNT for
 * RPUB0
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUB0		BIT(17)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_CR: Error event mask for R52 A1 Core Correctable
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_CR		BIT(18)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_TFATAL: Error event mask for R52 A1 Core TFatal
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_TFATAL	BIT(19)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_TIMEOUT: Error event mask for R52 A1 Core Timeout
 * Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_R52_B1_TIMEOUT	BIT(20)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUB1: Error event mask for Bit[24:20] pf
 * ERREVNT for RPUB1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B24_B20_RPUB1	BIT(21)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUB1: Error event mask for Bit[25] of ERREVNT for
 * RPUB1
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_B25_RPUB1		BIT(22)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PCIL_RPU: Error event mask for PCIL ERR FOR RPU Clusters
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PCIL_RPU		BIT(24)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPXAFIFS_CR: Error event mask for FPXAFIFS Corr Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPXAFIFS_CR	BIT(25)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPXAFIFS_NCR: Error event mask for FPXAFIFS UnCorr Error
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPXAFIFS_NCR	BIT(26)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_1: Reserved
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_1		BIT(27)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_2: Reserved
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_2		BIT(28)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_3: Error event mask for PSX_CMN_3 PD block
 * consolidated ERROR
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CMN_3		BIT(29)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CML: Reserved
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_PSX_CML		BIT(30)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_INT_WRAP: Error event mask FPD_INT_WRAP PD block
 * consolidated ERROR
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_INT_WRAP	BIT(31)

/*
 * Error Event Mask belongs to PSM ERR4 node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_PSM_ERR4
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_RST_MON: Error event mask for FPD Reset Monitor ERROR
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FPD_RST_MON	BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_RST_CLK_MON: Error event mask for LPD reset and Clock
 * Monitor Error.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_LPD_RST_CLK_MON	BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_FATAL_AFI_FM: Error event mask for Fatal Error from all
 * AFI FM
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_FATAL_AFI_FM	BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM_LPX: Error event mask for Non Fatal Error
 * from AFI FM in LPX
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM_LPX	BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM0_FPX: Error event mask for Non Fatal Error
 * from AFI FM0 in FPX
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM0_FPX	BIT(4)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM1_FPX: Error event mask for Non Fatal Error
 * from AFI FM1 in FPX
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM1_FPX	BIT(5)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM2_FPX: Error event mask for Non Fatal Error
 * from AFI FM2 in FPX
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM2_FPX	BIT(6)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM3_FPX: Error event mask for Non Fatal Error
 * from AFI FM3 in FPX
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_NFATAL_AFI_FM3_FPX	BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RPU_CLUSTERA: Error event mask for Errors from RPU
 * cluster A
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RPU_CLUSTERA	BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_RPU_CLUSTERB: Error event mask for Errors from RPU
 * cluster B
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_RPU_CLUSTERB	BIT(9)

/*
 * Error Event Mask belongs to SW ERR node,
 * For which Node_Id = VERSAL_NET_EVENT_ERROR_SW_ERR
 */

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_0: Health Boot Monitoring errors.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_0		BIT(0)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_1: Health Boot Monitoring errors.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_1		BIT(1)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_2: Health Boot Monitoring errors.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_2		BIT(2)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_3: Health Boot Monitoring errors.
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_HB_MON_3		BIT(3)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_CE: Error event mask for handling
 * correctable error in Versal Configuration RAM which is reported by
 * Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_CE		BIT(7)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_UE: Error event mask for handling
 * uncorrectable error in Versal Configuration RAM which is reported by
 * Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_UE		BIT(8)

/**
 * XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_NPI_UE: Error event mask for handling
 * uncorrectable error in Versal NoC programming interface (NPI)
 * register which is reported by Soft Error Mitigation (XilSEM).
 */
#define XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_NPI_UE		BIT(9)

#endif /* _FIRMWARE_XLNX_VERSAL_NET_ERROR_EVENTS_H_ */
