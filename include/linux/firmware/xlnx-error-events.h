/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Versal Error Event Node IDs and Error Event Mask.
 * Use with Xilinx Event Management Driver
 *
 * Copyright (C) 2021 Xilinx
 *
 * Abhyuday Godhasara <abhyuday.godhasara@xilinx.com>
 */

#ifndef _FIRMWARE_XLNX_ERROR_EVENTS_H_
#define _FIRMWARE_XLNX_ERROR_EVENTS_H_

/*
 * Error Event Node Ids
 */
#define XPM_NODETYPE_EVENT_ERROR_PMC_ERR1	(0x28100000U)
#define XPM_NODETYPE_EVENT_ERROR_PMC_ERR2	(0x28104000U)
#define XPM_NODETYPE_EVENT_ERROR_PSM_ERR1	(0x28108000U)
#define XPM_NODETYPE_EVENT_ERROR_PSM_ERR2	(0x2810C000U)
#define XPM_NODETYPE_EVENT_ERROR_SW_ERR		(0x28110000U)

/*
 * Error Event Mask belongs to PMC ERR2 node.
 * For which Node_Id = XPM_NODETYPE_EVENT_ERROR_PMC_ERR2
 */

/**
 * XPM_EVENT_ERROR_MASK_BOOT_CR: Error event mask for PMC Boot Correctable Error.
 * Set by ROM code during ROM execution during Boot.
 */
#define XPM_EVENT_ERROR_MASK_BOOT_CR		BIT(0)

/**
 * XPM_EVENT_ERROR_MASK_BOOT_NCR: Error event mask for PMC Boot Non-Correctable Error.
 * Set by ROM code during ROM execution during Boot.
 */
#define XPM_EVENT_ERROR_MASK_BOOT_NCR		BIT(1)

/**
 * XPM_EVENT_ERROR_MASK_FW_CR: Error event mask for PMC Firmware Boot Correctable Error.
 * Set by PLM during firmware execution during Boot.
 */
#define XPM_EVENT_ERROR_MASK_FW_CR		BIT(2)

/**
 * XPM_EVENT_ERROR_MASK_FW_NCR: Error event mask for PMC Firmware Boot Non-Correctable Error.
 * Set by PLM during firmware execution during Boot.
 */
#define XPM_EVENT_ERROR_MASK_FW_NCR		BIT(3)

/**
 * XPM_EVENT_ERROR_MASK_GSW_CR: Error event mask for General Software Correctable Error.
 * Set by any processors after Boot.
 */
#define XPM_EVENT_ERROR_MASK_GSW_CR		BIT(4)

/**
 * XPM_EVENT_ERROR_MASK_GSW_NCR: Error event mask for General Software Non-Correctable Error.
 * Set by any processors after Boot.
 */
#define XPM_EVENT_ERROR_MASK_GSW_NCR		BIT(5)

/**
 * XPM_EVENT_ERROR_MASK_CFU: Error event mask for CFU Error.
 */
#define XPM_EVENT_ERROR_MASK_CFU		BIT(6)

/**
 * XPM_EVENT_ERROR_MASK_CFRAME: Error event mask for CFRAME Error.
 */
#define XPM_EVENT_ERROR_MASK_CFRAME		BIT(7)

/**
 * XPM_EVENT_ERROR_MASK_PMC_PSM_CR: Error event mask for PSM Correctable Error,
 * Summary from PSM Error Management.
 */
#define XPM_EVENT_ERROR_MASK_PMC_PSM_CR		BIT(8)

/**
 * XPM_EVENT_ERROR_MASK_PMC_PSM_NCR: Error event mask for PSM Non-Correctable Error,
 * Summary from PSM Error Management.
 */
#define XPM_EVENT_ERROR_MASK_PMC_PSM_NCR	BIT(9)

/**
 * XPM_EVENT_ERROR_MASK_DDRMB_CR: Error event mask for DDRMC MB Correctable ECC Error.
 */
#define XPM_EVENT_ERROR_MASK_DDRMB_CR		BIT(10)

/**
 * XPM_EVENT_ERROR_MASK_DDRMB_NCR: Error event mask for DDRMC MB Non-Correctable ECC Error.
 */
#define XPM_EVENT_ERROR_MASK_DDRMB_NCR		BIT(11)

/**
 * XPM_EVENT_ERROR_MASK_NOCTYPE1_CR: Error event mask for NoC Type1 Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_NOCTYPE1_CR	BIT(12)

/**
 * XPM_EVENT_ERROR_MASK_NOCTYPE1_NCR: Error event mask for NoC Type1 Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_NOCTYPE1_NCR	BIT(13)

/**
 * XPM_EVENT_ERROR_MASK_NOCUSER: Error event mask for NoC User Error.
 */
#define XPM_EVENT_ERROR_MASK_NOCUSER		BIT(14)

/**
 * XPM_EVENT_ERROR_MASK_MMCM: Error event mask for MMCM Lock Error.
 */
#define XPM_EVENT_ERROR_MASK_MMCM		BIT(15)

/**
 * XPM_EVENT_ERROR_MASK_AIE_CR: Error event mask for ME Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_AIE_CR		BIT(16)

/**
 * XPM_EVENT_ERROR_MASK_AIE_NCR: Error event mask for ME Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_AIE_NCR		BIT(17)

/**
 * XPM_EVENT_ERROR_MASK_DDRMC_CR: Error event mask for DDRMC MC Correctable ECC Error.
 */
#define XPM_EVENT_ERROR_MASK_DDRMC_CR		BIT(18)

/**
 * XPM_EVENT_ERROR_MASK_DDRMC_NCR: Error event mask for DDRMC MC Non-Correctable ECC Error.
 */
#define XPM_EVENT_ERROR_MASK_DDRMC_NCR		BIT(19)

/**
 * XPM_EVENT_ERROR_MASK_GT_CR: Error event mask for GT Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_GT_CR		BIT(20)

/**
 * XPM_EVENT_ERROR_MASK_GT_NCR: Error event mask for GT Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_GT_NCR		BIT(21)

/**
 * XPM_EVENT_ERROR_MASK_PLSMON_CR: Error event mask for PL Sysmon Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PLSMON_CR		BIT(22)

/**
 * XPM_EVENT_ERROR_MASK_PLSMON_NCR: Error event mask for PL Sysmon Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PLSMON_NCR		BIT(23)

/**
 * XPM_EVENT_ERROR_MASK_PL0: Error event mask for User defined PL generic error.
 */
#define XPM_EVENT_ERROR_MASK_PL0		BIT(24)

/**
 * XPM_EVENT_ERROR_MASK_PL1: Error event mask for User defined PL generic error.
 */
#define XPM_EVENT_ERROR_MASK_PL1		BIT(25)

/**
 * XPM_EVENT_ERROR_MASK_PL2: Error event mask for User defined PL generic error.
 */
#define XPM_EVENT_ERROR_MASK_PL2		BIT(26)

/**
 * XPM_EVENT_ERROR_MASK_PL3: Error event mask for User defined PL generic error.
 */
#define XPM_EVENT_ERROR_MASK_PL3		BIT(27)

/**
 * XPM_EVENT_ERROR_MASK_NPIROOT: Error event mask for NPI Root Error.
 */
#define XPM_EVENT_ERROR_MASK_NPIROOT		BIT(28)

/**
 * XPM_EVENT_ERROR_MASK_SSIT3: Error event mask for SSIT Error from Slave SLR1,
 * Only used in Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT3		BIT(29)

/**
 * XPM_EVENT_ERROR_MASK_SSIT4: Error event mask for SSIT Error from Slave SLR2,
 * Only used in Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT4		BIT(30)

/**
 * XPM_EVENT_ERROR_MASK_SSIT5: Error event mask for SSIT Error from Slave SLR3,
 * Only used in Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT5		BIT(31)

/*
 * Error Event Mask belongs to PMC ERR2 node,
 * For which Node_Id = XPM_NODETYPE_EVENT_ERROR_PMC_ERR2
 */

/**
 * XPM_EVENT_ERROR_MASK_PMCAPB: Error event mask for General purpose PMC error,
 * can be triggered by any of the following peripherals:,
 * - PMC Global Regsiters,- PMC Clock & Reset (CRP),- PMC IOU Secure SLCR,
 * - PMC IOU SLCR,- BBRAM Controller,- PMC Analog Control Registers,
 * - RTC Control Registers.
 */
#define XPM_EVENT_ERROR_MASK_PMCAPB		BIT(0)

/**
 * XPM_EVENT_ERROR_MASK_PMCROM: Error event mask for PMC ROM Validation Error.
 */
#define XPM_EVENT_ERROR_MASK_PMCROM		BIT(1)

/**
 * XPM_EVENT_ERROR_MASK_MB_FATAL0: Error event mask for PMC PPU0 MB TMR Fatal Error.
 */
#define XPM_EVENT_ERROR_MASK_MB_FATAL0		BIT(2)

/**
 * XPM_EVENT_ERROR_MASK_MB_FATAL1: Error event mask for PMC PPU1 MB TMR Fatal Error.
 */
#define XPM_EVENT_ERROR_MASK_MB_FATAL1		BIT(3)

/**
 * XPM_EVENT_ERROR_MASK_PMCPAR: Error event mask for PMC Switch and PMC IOU Parity Errors.
 */
#define XPM_EVENT_ERROR_MASK_PMCPAR		BIT(4)

/**
 * XPM_EVENT_ERROR_MASK_PMC_CR: Error event mask for PMC Correctable Errors,
 * PPU0 RAM correctable error.,PPU1 instruction RAM correctable error.,
 * PPU1 data RAM correctable error.
 */
#define XPM_EVENT_ERROR_MASK_PMC_CR		BIT(5)

/**
 * XPM_EVENT_ERROR_MASK_PMC_NCR: Error event mask for PMC Non-Correctable Errors,
 * PPU0 RAM non-correctable error.,PPU1 instruction RAM non-correctable error.,
 * PPU1 data RAM non-correctable error.,PRAM non-correctable error.
 */
#define XPM_EVENT_ERROR_MASK_PMC_NCR		BIT(6)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON0: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[0].
 * Indicates an alarm condition on any of SUPPLY0 to SUPPLY31.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON0		BIT(7)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON1: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[1].
 * Indicates an alarm condition on any of SUPPLY32 to SUPPLY63.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON1		BIT(8)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON2: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[2].
 * Indicates an alarm condition on any of SUPPLY64 to SUPPLY95.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON2		BIT(9)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON3: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[3].
 * Indicates an alarm condition on any of SUPPLY96 to SUPPLY127.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON3		BIT(10)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON4: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[4].
 * Indicates an alarm condition on any of SUPPLY128 to SUPPLY159.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON4		BIT(11)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON8: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[8].
 * Indicates an over-temperature alarm.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON8		BIT(15)

/**
 * XPM_EVENT_ERROR_MASK_PMCSMON9: Error event mask for PMC Temperature Shutdown Alert
 * and Power Supply.
 * Failure Detection Errors from PMC Sysmon alarm[9].
 * Indicates a device temperature alarm.
 */
#define XPM_EVENT_ERROR_MASK_PMCSMON9		BIT(16)

/**
 * XPM_EVENT_ERROR_MASK_CFI: Error event mask for CFI Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_CFI		BIT(17)

/**
 * XPM_EVENT_ERROR_MASK_SEUCRC: Error event mask for CFRAME SEU CRC Error.
 */
#define XPM_EVENT_ERROR_MASK_SEUCRC		BIT(18)

/**
 * XPM_EVENT_ERROR_MASK_SEUECC: Error event mask for CFRAME SEU ECC Error.
 */
#define XPM_EVENT_ERROR_MASK_SEUECC		BIT(19)

/**
 * XPM_EVENT_ERROR_MASK_RTCALARM: Error event mask for RTC Alarm Error.
 */
#define XPM_EVENT_ERROR_MASK_RTCALARM		BIT(22)

/**
 * XPM_EVENT_ERROR_MASK_NPLL: Error event mask for PMC NPLL Lock Error,
 * This error can be unmasked after the NPLL is locked to alert when the
 * NPLL loses lock.
 */
#define XPM_EVENT_ERROR_MASK_NPLL		BIT(23)

/**
 * XPM_EVENT_ERROR_MASK_PPLL: Error event mask for PMC PPLL Lock Error,
 * This error can be unmasked after the PPLL is locked to alert when the
 * PPLL loses lock.
 */
#define XPM_EVENT_ERROR_MASK_PPLL		BIT(24)

/**
 * XPM_EVENT_ERROR_MASK_CLKMON: Error event mask for Clock Monitor Errors.,
 * Collected from CRP's CLKMON_STATUS register.
 */
#define XPM_EVENT_ERROR_MASK_CLKMON		BIT(25)

/**
 * XPM_EVENT_ERROR_MASK_PMCTO: Error event mask for PMC Interconnect Timeout Errors.,
 * Collected from:,Interconnect mission interrupt status register.,
 * Interconnect latent status register.,Timeout interrupt status register
 * for SERBs.
 */
#define XPM_EVENT_ERROR_MASK_PMCTO		BIT(26)

/**
 * XPM_EVENT_ERROR_MASK_PMCXMPU: Error event mask for PMC XMPU Errors:,
 * Register access error on APB., Read permission violation.,
 * Write permission violation.,Security violation.
 */
#define XPM_EVENT_ERROR_MASK_PMCXMPU		BIT(27)

/**
 * XPM_EVENT_ERROR_MASK_PMCXPPU: Error event mask for PMC XPPU Errors:,
 * Register access error on APB., Master ID not found.,Read permission violation.,
 * Master ID parity error., Master ID access violation.,
 * TrustZone violation.,Aperture parity error.
 */
#define XPM_EVENT_ERROR_MASK_PMCXPPU		BIT(28)

/**
 * XPM_EVENT_ERROR_MASK_SSIT0: Error event mask for For Master SLR:
 * SSIT Error from Slave SLR1.,
 * For Slave SLRs: SSIT Error0 from Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT0		BIT(29)

/**
 * XPM_EVENT_ERROR_MASK_SSIT1: Error event mask for For Master SLR:
 * SSIT Error from Slave SLR2.,
 * For Slave SLRs: SSIT Error1 from Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT1		BIT(30)

/**
 * XPM_EVENT_ERROR_MASK_SSIT2: Error event mask for For Master SLR:
 * SSIT Error from Slave SLR3.,
 * For Slave SLRs: SSIT Error2 from Master SLR.
 */
#define XPM_EVENT_ERROR_MASK_SSIT2		BIT(31)

/*
 * Error Event Mask belongs to PSM ERR1 node,
 * For which Node_Id = XPM_NODETYPE_EVENT_ERROR_PSM_ERR1
 */

/**
 * XPM_EVENT_ERROR_MASK_PS_SW_CR: Error event mask for PS Software can write to
 * trigger register to generate this Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PS_SW_CR		BIT(0)

/**
 * XPM_EVENT_ERROR_MASK_PS_SW_NCR: Error event mask for PS Software can write to
 * trigger register to generate this Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PS_SW_NCR		BIT(1)

/**
 * XPM_EVENT_ERROR_MASK_PSM_B_CR: Error event mask for PSM Firmware can write to
 * trigger register to generate this Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PSM_B_CR		BIT(2)

/**
 * XPM_EVENT_ERROR_MASK_PSM_B_NCR: Error event mask for PSM Firmware can write to
 * trigger register to generate this Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_PSM_B_NCR		BIT(3)

/**
 * XPM_EVENT_ERROR_MASK_MB_FATAL: Error event mask for Or of MB Fatal1, Fatal2, Fatal3 Error.
 */
#define XPM_EVENT_ERROR_MASK_MB_FATAL		BIT(4)

/**
 * XPM_EVENT_ERROR_MASK_PSM_CR: Error event mask for PSM Correctable.
 */
#define XPM_EVENT_ERROR_MASK_PSM_CR		BIT(5)

/**
 * XPM_EVENT_ERROR_MASK_PSM_NCR: Error event mask for PSM Non-Correctable.
 */
#define XPM_EVENT_ERROR_MASK_PSM_NCR		BIT(6)

/**
 * XPM_EVENT_ERROR_MASK_OCM_ECC: Error event mask for Non-Correctable ECC Error
 * during an OCM access.
 */
#define XPM_EVENT_ERROR_MASK_OCM_ECC		BIT(7)

/**
 * XPM_EVENT_ERROR_MASK_L2_ECC: Error event mask for Non-Correctable ECC Error
 * during APU L2 Cache access.
 */
#define XPM_EVENT_ERROR_MASK_L2_ECC		BIT(8)

/**
 * XPM_EVENT_ERROR_MASK_RPU_ECC: Error event mask for ECC Errors during a RPU memory access.
 * Floating-point operation exceptions. RPU REG APB error.
 */
#define XPM_EVENT_ERROR_MASK_RPU_ECC		BIT(9)

/**
 * XPM_EVENT_ERROR_MASK_RPU_LS: Error event mask for RPU Lockstep Errors from R5_0.
 * The Lockstep error is not initialized until RPU clock is enabled;
 * therefore, error outcomes are masked by default and are expected to be
 * unmasked after processor clock is enabled and before its reset is released.
 */
#define XPM_EVENT_ERROR_MASK_RPU_LS		BIT(10)

/**
 * XPM_EVENT_ERROR_MASK_RPU_CCF: Error event mask for RPU Common Cause Failures ORed together.
 * The CCF Error register with the masking capability has to reside in the RPU.
 */
#define XPM_EVENT_ERROR_MASK_RPU_CCF		BIT(11)

/**
 * XPM_EVENT_ERROR_MASK_GIC_AXI: Error event mask for APU GIC AXI Error by the AXI4 master port,
 * such as SLVERR or DECERR.
 */
#define XPM_EVENT_ERROR_MASK_GIC_AXI		BIT(12)

/**
 * XPM_EVENT_ERROR_MASK_GIC_ECC: Error event mask for APU GIC ECC Error,
 * a Non-Correctable ECC error occurred in any ECC-protected RAM.
 */
#define XPM_EVENT_ERROR_MASK_GIC_ECC		BIT(13)

/**
 * XPM_EVENT_ERROR_MASK_APLL_LOCK: Error event mask for APLL Lock Errors.
 * The error can be unmasked after the PLL is locked to alert when the
 * PLL loses lock.
 */
#define XPM_EVENT_ERROR_MASK_APLL_LOCK		BIT(14)

/**
 * XPM_EVENT_ERROR_MASK_RPLL_LOCK: Error event mask for RPLL Lock Errors.
 * The error can be unmasked after the PLL is locked to alert when the
 * PLL loses lock.
 */
#define XPM_EVENT_ERROR_MASK_RPLL_LOCK		BIT(15)

/**
 * XPM_EVENT_ERROR_MASK_CPM_CR: Error event mask for CPM Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_CPM_CR		BIT(16)

/**
 * XPM_EVENT_ERROR_MASK_CPM_NCR: Error event mask for CPM Non-Correctable Error.
 */
#define XPM_EVENT_ERROR_MASK_CPM_NCR		BIT(17)

/**
 * XPM_EVENT_ERROR_MASK_LPD_APB: Error event mask for LPD APB Errors
 * from:,IPI REG,USB2 REG,CRL REG,LPD AFIFM4 REG,LPD IOU REG,
 * LPD IOU SECURE SLCR REG,LPD SLCR REG,LPD SLCR SECURE REG.
 */
#define XPM_EVENT_ERROR_MASK_LPD_APB		BIT(18)

/**
 * XPM_EVENT_ERROR_MASK_FPD_APB: Error event mask for FPD APB Errors
 * from:,FPD AFIFM0 REG,FPD AFIFM2 REG,FPD SLCR REG,FPD SLCR SECURE REG,
 * CRF REG.
 */
#define XPM_EVENT_ERROR_MASK_FPD_APB		BIT(19)

/**
 * XPM_EVENT_ERROR_MASK_LPD_PAR: Error event mask for Data parity errors
 * from the interfaces connected
 * to the LPD interconnect.
 */
#define XPM_EVENT_ERROR_MASK_LPD_PAR		BIT(20)

/**
 * XPM_EVENT_ERROR_MASK_FPD_PAR: Error event mask for Data parity errors
 * from the interfaces connected
 * to the FPD interconnect.
 */
#define XPM_EVENT_ERROR_MASK_FPD_PAR		BIT(21)

/**
 * XPM_EVENT_ERROR_MASK_IOU_PAR: Error event mask for LPD IO Peripheral Unit Parity Error.
 */
#define XPM_EVENT_ERROR_MASK_IOU_PAR		BIT(22)

/**
 * XPM_EVENT_ERROR_MASK_PSM_PAR: Error event mask for Data parity errors
 * from the interfaces connected to the PSM interconnect.
 */
#define XPM_EVENT_ERROR_MASK_PSM_PAR		BIT(23)

/**
 * XPM_EVENT_ERROR_MASK_LPD_TO: Error event mask for LPD Interconnect Timeout errors.
 * Collected from:,Timeout errors at the slaves connected to the LPD
 * interconnect.,Address decode error.,Interconnect mission errors for
 * the slaves connected to the LPD interconnect.
 */
#define XPM_EVENT_ERROR_MASK_LPD_TO		BIT(24)

/**
 * XPM_EVENT_ERROR_MASK_FPD_TO: Error event mask for FPD Interconnect Timeout errors.
 * Collected from:,Coresight debug trace alarms.,Timeout errors at the
 * slaves connected to the FPD interconnect.,Address decode error.,
 * Data parity errors on the interfaces connected to the FPD interconnect.
 */
#define XPM_EVENT_ERROR_MASK_FPD_TO		BIT(25)

/**
 * XPM_EVENT_ERROR_MASK_PSM_TO: Error event mask for PSM Interconnect Timeout Errors.
 * Collected from:,Interconnect mission errors for PSM_LOCAL slave or
 * PSM_GLOBAL slave or MDM slave or LPD interconnect or PSM master.,
 * Interconnect latent errors for PSM_LOCAL slave or PSM_GLOBAL slave or
 * MDM slave or LPD interconnect or PSM master.,
 * Timeout errors at the slaves connected to the PSM interconnect.
 */
#define XPM_EVENT_ERROR_MASK_PSM_TO		BIT(26)

/**
 * XPM_EVENT_ERROR_MASK_XRAM_CR: Error event mask for XRAM Correctable error.
 * Only applicable in devices that have XRAM.
 */
#define XPM_EVENT_ERROR_MASK_XRAM_CR		BIT(27)

/**
 * XPM_EVENT_ERROR_MASK_XRAM_NCR: Error event mask for XRAM Non-Correctable error.
 * Only applicable in devices that have XRAM.
 */
#define XPM_EVENT_ERROR_MASK_XRAM_NCR		BIT(28)

/*
 * Error Event Mask belongs to PSM ERR2 node,
 * For which Node_Id = XPM_NODETYPE_EVENT_ERROR_PSM_ERR2
 */

/**
 * XPM_EVENT_ERROR_MASK_LPD_SWDT: Error event mask for Error from Watchdog Timer
 * in the LPD Subsystem.
 */
#define XPM_EVENT_ERROR_MASK_LPD_SWDT		BIT(0)

/**
 * XPM_EVENT_ERROR_MASK_FPD_SWDT: Error event mask for Error from Watchdog Timer
 * in the FPD Subsystem.
 */
#define XPM_EVENT_ERROR_MASK_FPD_SWDT		BIT(1)

/**
 * XPM_EVENT_ERROR_MASK_LPD_XMPU: Error event mask for LPD XMPU Errors:,
 * Register access error on APB., Read permission violation.,
 * Write permission violation.,Security violation.
 */
#define XPM_EVENT_ERROR_MASK_LPD_XMPU		BIT(18)

/**
 * XPM_EVENT_ERROR_MASK_LPD_XPPU: Error event mask for LPD XPPU Errors:,
 * Register access error on APB., Master ID not found.,Read permission violation.,
 * Master ID parity error., Master ID access violation.,
 * TrustZone violation.,Aperture parity error.
 */
#define XPM_EVENT_ERROR_MASK_LPD_XPPU		BIT(19)

/**
 * XPM_EVENT_ERROR_MASK_FPD_XMPU: Error event mask for FPD XMPU Errors:,
 * Register access error on APB., Read permission violation.,
 * Write permission violation.,Security violation.
 */
#define XPM_EVENT_ERROR_MASK_FPD_XMPU		BIT(20)

/*
 * Error Event Mask belongs to SW ERR node,
 * For which Node_Id = XPM_NODETYPE_EVENT_ERROR_SW_ERR
 */

/**
 * XPM_EVENT_ERROR_MASK_HB_MON_0: Health Boot Monitoring errors.
 */
#define XPM_EVENT_ERROR_MASK_HB_MON_0		BIT(0)

/**
 * XPM_EVENT_ERROR_MASK_HB_MON_1: Health Boot Monitoring errors.
 */
#define XPM_EVENT_ERROR_MASK_HB_MON_1		BIT(1)

/**
 * XPM_EVENT_ERROR_MASK_HB_MON_2: Health Boot Monitoring errors.
 */
#define XPM_EVENT_ERROR_MASK_HB_MON_2		BIT(2)

/**
 * XPM_EVENT_ERROR_MASK_HB_MON_3: Health Boot Monitoring errors.
 */
#define XPM_EVENT_ERROR_MASK_HB_MON_3		BIT(3)

#endif /* _FIRMWARE_XLNX_ERROR_EVENTS_H_ */
