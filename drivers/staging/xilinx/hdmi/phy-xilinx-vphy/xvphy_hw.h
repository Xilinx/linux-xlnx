/*******************************************************************************
 *
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
 *
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file xvphy_hw.h
 *
 * This header file contains the identifiers and low-level driver functions (or
 * macros) that can be used to access the device. High-level driver functions
 * are defined in xvphy.h.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   als  10/19/15 Initial release.
 * 1.1   gm   02/01/16 Added GTPE2 and GTHE4 support
 * 1.4   gm   29/11/16 Added ERR_IRQ register offset
 * </pre>
 *
*******************************************************************************/

#ifndef XVPHY_HW_H_
/* Prevent circular inclusions by using protection macros. */
#define XVPHY_HW_H_

/***************************** Include Files **********************************/

#include "xil_io.h"
#include "xil_types.h"

/************************** Constant Definitions ******************************/

/******************************************************************************/
/**
 * Address mapping for the Video PHY core.
 *
*******************************************************************************/
/** @name VPHY core registers: General registers.
  * @{
  */
#define XVPHY_VERSION_REG			0x000
#define XVPHY_BANK_SELECT_REG		0x00C
#define XVPHY_REF_CLK_SEL_REG		0x010
#define XVPHY_PLL_RESET_REG			0x014
#define XVPHY_PLL_LOCK_STATUS_REG	0x018
#define XVPHY_TX_INIT_REG			0x01C
#define XVPHY_TX_INIT_STATUS_REG	0x020
#define XVPHY_RX_INIT_REG			0x024
#define XVPHY_RX_INIT_STATUS_REG	0x028
#define XVPHY_IBUFDS_GTXX_CTRL_REG	0x02C
#define XVPHY_POWERDOWN_CONTROL_REG	0x030
#define XVPHY_LOOPBACK_CONTROL_REG	0x038
/* @} */

/** @name VPHY core registers: Dynamic reconfiguration port (DRP) registers.
  * @{
  */
#define XVPHY_DRP_CONTROL_CH1_REG	0x040
#define XVPHY_DRP_CONTROL_CH2_REG	0x044
#define XVPHY_DRP_CONTROL_CH3_REG	0x048
#define XVPHY_DRP_CONTROL_CH4_REG	0x04C
#define XVPHY_DRP_STATUS_CH1_REG	0x050
#define XVPHY_DRP_STATUS_CH2_REG	0x054
#define XVPHY_DRP_STATUS_CH3_REG	0x058
#define XVPHY_DRP_STATUS_CH4_REG	0x05C
#define XVPHY_DRP_CONTROL_COMMON_REG	0x060
#define XVPHY_DRP_STATUS_COMMON_REG	0x064
/* @} */

/** @name VPHY core registers: Transmitter function registers.
  * @{
  */
#define XVPHY_TX_CONTROL_REG		0x070
#define XVPHY_TX_BUFFER_BYPASS_REG	0x074
#define XVPHY_TX_STATUS_REG			0x078
#define XVPHY_TX_DRIVER_CH12_REG	0x07C
#define XVPHY_TX_DRIVER_CH34_REG	0x080
/* @} */

/** @name VPHY core registers: Receiver function registers.
  * @{
  */
#define XVPHY_RX_CONTROL_REG		0x100
#define XVPHY_RX_STATUS_REG		    0x104
#define XVPHY_RX_EQ_CDR_REG		    0x108
#define XVPHY_RX_TDLOCK_REG		    0x10C
/* @} */

/** @name VPHY core registers: Interrupt registers.
  * @{
  */
#define XVPHY_ERR_IRQ			0x03C
#define XVPHY_INTR_EN_REG		0x110
#define XVPHY_INTR_DIS_REG		0x114
#define XVPHY_INTR_MASK_REG		0x118
#define XVPHY_INTR_STS_REG		0x11C
/* @} */

/** @name User clocking registers: MMCM and BUFGGT registers.
  * @{
  */
#define XVPHY_MMCM_TXUSRCLK_CTRL_REG	0x0120
#define XVPHY_MMCM_TXUSRCLK_REG1	0x0124
#define XVPHY_MMCM_TXUSRCLK_REG2	0x0128
#define XVPHY_MMCM_TXUSRCLK_REG3	0x012C
#define XVPHY_MMCM_TXUSRCLK_REG4	0x0130
#define XVPHY_BUFGGT_TXUSRCLK_REG 	0x0134
#define XVPHY_MISC_TXUSRCLK_REG		0x0138

#define XVPHY_MMCM_RXUSRCLK_CTRL_REG	0x0140
#define XVPHY_MMCM_RXUSRCLK_REG1	0x0144
#define XVPHY_MMCM_RXUSRCLK_REG2	0x0148
#define XVPHY_MMCM_RXUSRCLK_REG3	0x014C
#define XVPHY_MMCM_RXUSRCLK_REG4	0x0150
#define XVPHY_BUFGGT_RXUSRCLK_REG 	0x0154
#define XVPHY_MISC_RXUSRCLK_REG		0x0158
/* @} */

/** @name Clock detector (HDMI) registers.
  * @{
  */
#define XVPHY_CLKDET_CTRL_REG		0x0200
#define XVPHY_CLKDET_STAT_REG		0x0204
#define XVPHY_CLKDET_FREQ_TMR_TO_REG	0x0208
#define XVPHY_CLKDET_FREQ_TX_REG	0x020C
#define XVPHY_CLKDET_FREQ_RX_REG	0x0210
#define XVPHY_CLKDET_TMR_TX_REG		0x0214
#define XVPHY_CLKDET_TMR_RX_REG		0x0218
#define XVPHY_CLKDET_FREQ_DRU_REG	0x021C
/* @} */

/** @name Data recovery unit registers (HDMI).
  * @{
  */
#define XVPHY_DRU_CTRL_REG		0x0300
#define XVPHY_DRU_STAT_REG		0x0304

#define XVPHY_DRU_CFREQ_L_REG(Ch)	(0x0308 + (12 * (Ch - 1)))
#define XVPHY_DRU_CFREQ_H_REG(Ch)	(0x030C + (12 * (Ch - 1)))
#define XVPHY_DRU_GAIN_REG(Ch)		(0x0310 + (12 * (Ch - 1)))
/* @} */

/******************************************************************************/

/** @name VPHY core masks, shifts, and register values.
  * @{
  */
/* 0x0F8: VERSION */
#define XVPHY_VERSION_INTER_REV_MASK \
				0x000000FF	/**< Internal revision. */
#define XVPHY_VERSION_CORE_PATCH_MASK \
				0x00000F00	/**< Core patch details. */
#define XVPHY_VERSION_CORE_PATCH_SHIFT 8	/**< Shift bits for core patch
							details. */
#define XVPHY_VERSION_CORE_VER_REV_MASK \
				0x0000F000	/**< Core version revision. */
#define XVPHY_VERSION_CORE_VER_REV_SHIFT 12	/**< Shift bits for core version
							revision. */
#define XVPHY_VERSION_CORE_VER_MNR_MASK \
				0x00FF0000	/**< Core minor version. */
#define XVPHY_VERSION_CORE_VER_MNR_SHIFT 16	/**< Shift bits for core minor
							version. */
#define XVPHY_VERSION_CORE_VER_MJR_MASK \
				0xFF000000	/**< Core major version. */
#define XVPHY_VERSION_CORE_VER_MJR_SHIFT 24	/**< Shift bits for core major
							version. */
/* 0x00C: BANK_SELECT_REG */
#define XVPHY_BANK_SELECT_TX_MASK	0x00F
#define XVPHY_BANK_SELECT_RX_MASK	0xF00
#define XVPHY_BANK_SELECT_RX_SHIFT	8
/* 0x010: REF_CLK_SEL */
#define XVPHY_REF_CLK_SEL_QPLL0_MASK	0x0000000F
#define XVPHY_REF_CLK_SEL_CPLL_MASK	0x000000F0
#define XVPHY_REF_CLK_SEL_CPLL_SHIFT	4
#define XVPHY_REF_CLK_SEL_QPLL1_MASK	0x00000F00
#define XVPHY_REF_CLK_SEL_QPLL1_SHIFT	8
#define XVPHY_REF_CLK_SEL_XPLL_GTREFCLK0 1
#define XVPHY_REF_CLK_SEL_XPLL_GTREFCLK1 2
#define XVPHY_REF_CLK_SEL_XPLL_GTNORTHREFCLK0 3
#define XVPHY_REF_CLK_SEL_XPLL_GTNORTHREFCLK1 4
#define XVPHY_REF_CLK_SEL_XPLL_GTSOUTHREFCLK0 5
#define XVPHY_REF_CLK_SEL_XPLL_GTSOUTHREFCLK1 6
#define XVPHY_REF_CLK_SEL_XPLL_GTEASTREFCLK0 3
#define XVPHY_REF_CLK_SEL_XPLL_GTEASTREFCLK1 4
#define XVPHY_REF_CLK_SEL_XPLL_GTWESTREFCLK0 5
#define XVPHY_REF_CLK_SEL_XPLL_GTWESTREFCLK1 6
#define XVPHY_REF_CLK_SEL_XPLL_GTGREFCLK 7
#define XVPHY_REF_CLK_SEL_SYSCLKSEL_MASK 0x0F000000
#define XVPHY_REF_CLK_SEL_SYSCLKSEL_SHIFT 24
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_PLL0 0
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_PLL1 1
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_CPLL 0
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL 1
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL0 3
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_DATA_QPLL1 2
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CH 0
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN 1
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN0 2
#define XVPHY_REF_CLK_SEL_XXSYSCLKSEL_OUT_CMN1 3
#define XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_MASK(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 0x03000000 : 0x02000000)
#define XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_MASK(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 0x0C000000 : 0x08000000)
#define XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_MASK(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 0x30000000 : 0x01000000)
#define XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_MASK(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 0xC0000000 : 0x04000000)
#define XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_SHIFT(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 24 : 25)
#define XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_SHIFT(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 26 : 27)
#define XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_SHIFT(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 28 : 24)
#define XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_SHIFT(G) \
	((((G) == XVPHY_GT_TYPE_GTHE3) || \
		((G) == XVPHY_GT_TYPE_GTHE4)) ? 30 : 26)
/* 0x014: PLL_RESET */
#define XVPHY_PLL_RESET_CPLL_MASK	0x1
#define XVPHY_PLL_RESET_QPLL0_MASK	0x2
#define XVPHY_PLL_RESET_QPLL1_MASK	0x4
/* 0x018: PLL_LOCK_STATUS */
#define XVPHY_PLL_LOCK_STATUS_CPLL_MASK(Ch) \
		(0x01 << (Ch - 1))
#define XVPHY_PLL_LOCK_STATUS_QPLL0_MASK	0x10
#define XVPHY_PLL_LOCK_STATUS_QPLL1_MASK	0x20
#define XVPHY_PLL_LOCK_STATUS_CPLL_ALL_MASK \
		(XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH3) | \
		 XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK \
		(XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_PLL_LOCK_STATUS_CPLL_MASK(XVPHY_CHANNEL_ID_CH3))
/* 0x01C, 0x024: TX_INIT, RX_INIT */
#define XVPHY_TXRX_INIT_GTRESET_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_PMARESET_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_PCSRESET_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_TX_INIT_USERRDY_MASK(Ch) \
		(0x08 << (8 * (Ch - 1)))
#define XVPHY_RX_INIT_USERRDY_MASK(Ch) \
		(0x40 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_PLLGTRESET_MASK(Ch) \
		(0x80 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_GTRESET_ALL_MASK \
		(XVPHY_TXRX_INIT_GTRESET_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_TXRX_INIT_GTRESET_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_TXRX_INIT_GTRESET_MASK(XVPHY_CHANNEL_ID_CH3) | \
		 XVPHY_TXRX_INIT_GTRESET_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_TX_INIT_USERRDY_ALL_MASK \
		(XVPHY_TX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_TX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_TX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH3) | \
		 XVPHY_TX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_RX_INIT_USERRDY_ALL_MASK \
		(XVPHY_RX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_RX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_RX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH3) | \
		 XVPHY_RX_INIT_USERRDY_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_TXRX_INIT_PLLGTRESET_ALL_MASK \
		(XVPHY_TXRX_INIT_PLLGTRESET_MASK(XVPHY_CHANNEL_ID_CH1) | \
		 XVPHY_TXRX_INIT_PLLGTRESET_MASK(XVPHY_CHANNEL_ID_CH2) | \
		 XVPHY_TXRX_INIT_PLLGTRESET_MASK(XVPHY_CHANNEL_ID_CH3) | \
		 XVPHY_TXRX_INIT_PLLGTRESET_MASK(XVPHY_CHANNEL_ID_CH4))
/* 0x020, 0x028: TX_STATUS, RX_STATUS */
#define XVPHY_TXRX_INIT_STATUS_RESETDONE_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_STATUS_PMARESETDONE_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_STATUS_POWERGOOD_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_TXRX_INIT_STATUS_RESETDONE_ALL_MASK \
	(XVPHY_TXRX_INIT_STATUS_RESETDONE_MASK(XVPHY_CHANNEL_ID_CH1) | \
	 XVPHY_TXRX_INIT_STATUS_RESETDONE_MASK(XVPHY_CHANNEL_ID_CH2) | \
	 XVPHY_TXRX_INIT_STATUS_RESETDONE_MASK(XVPHY_CHANNEL_ID_CH3) | \
	 XVPHY_TXRX_INIT_STATUS_RESETDONE_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_TXRX_INIT_STATUS_PMARESETDONE_ALL_MASK \
	(XVPHY_TXRX_INIT_STATUS_PMARESETDONE_MASK(XVPHY_CHANNEL_ID_CH1) | \
	 XVPHY_TXRX_INIT_STATUS_PMARESETDONE_MASK(XVPHY_CHANNEL_ID_CH2) | \
	 XVPHY_TXRX_INIT_STATUS_PMARESETDONE_MASK(XVPHY_CHANNEL_ID_CH3) | \
	 XVPHY_TXRX_INIT_STATUS_PMARESETDONE_MASK(XVPHY_CHANNEL_ID_CH4))
/* 0x02C: IBUFDS_GTXX_CTRL */
#define XVPHY_IBUFDS_GTXX_CTRL_GTREFCLK0_CEB_MASK	0x1
#define XVPHY_IBUFDS_GTXX_CTRL_GTREFCLK1_CEB_MASK	0x2
/* 0x030: POWERDOWN_CONTROL */
#define XVPHY_POWERDOWN_CONTROL_CPLLPD_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_RXPD_MASK(Ch) \
		(0x18 << (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_RXPD_SHIFT(Ch) \
		(3 + (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_TXPD_MASK(Ch) \
		(0x60 << (8 * (Ch - 1)))
#define XVPHY_POWERDOWN_CONTROL_TXPD_SHIFT(Ch) \
		(5 + (8 * (Ch - 1)))
/* 0x038: LOOPBACK_CONTROL */
#define XVPHY_LOOPBACK_CONTROL_CH_MASK(Ch) \
		(0x03 << (8 * (Ch - 1)))
#define XVPHY_LOOPBACK_CONTROL_CH_SHIFT(Ch) \
		(8 * (Ch - 1))
/* 0x040, 0x044, 0x048, 0x04C, 0x060: DRP_CONTROL_CH[1-4], DRP_CONTROL_COMMON */
#define XVPHY_DRP_CONTROL_DRPADDR_MASK	0x00000FFF
#define XVPHY_DRP_CONTROL_DRPEN_MASK	0x00001000
#define XVPHY_DRP_CONTROL_DRPWE_MASK	0x00002000
#define XVPHY_DRP_CONTROL_DRPRESET_MASK	0x00004000
#define XVPHY_DRP_CONTROL_DRPDI_MASK	0xFFFF0000
#define XVPHY_DRP_CONTROL_DRPDI_SHIFT	16
/* 0x050, 0x054, 0x058, 0x05C, 0x064: DRP_STATUS_CH[1-4], DRP_STATUS_COMMON */
#define XVPHY_DRP_STATUS_DRPO_MASK	0x0FFFF
#define XVPHY_DRP_STATUS_DRPRDY_MASK	0x10000
#define XVPHY_DRP_STATUS_DRPBUSY_MASK	0x20000
/* 0x070: TX_CONTROL */
#define XVPHY_TX_CONTROL_TX8B10BEN_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_TX_CONTROL_TX8B10BEN_ALL_MASK \
		(XVPHY_TX_CONTROL_TX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH1) | \
		XVPHY_TX_CONTROL_TX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH2) | \
		XVPHY_TX_CONTROL_TX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH3) | \
		XVPHY_TX_CONTROL_TX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_TX_CONTROL_TXPOLARITY_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_TX_CONTROL_TXPRBSSEL_MASK(Ch) \
		(0x1C << (8 * (Ch - 1)))
#define XVPHY_TX_CONTROL_TXPRBSSEL_SHIFT(Ch) \
		(2 + (8 * (Ch - 1)))
#define XVPHY_TX_CONTROL_TXPRBSFORCEERR_MASK(Ch) \
		(0x20 << (8 * (Ch - 1)))
/* 0x074: TX_BUFFER_BYPASS */
#define XVPHY_TX_BUFFER_BYPASS_TXPHDLYRESET_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXPHALIGN_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXPHALIGNEN_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXPHDLYPD_MASK(Ch) \
		(0x08 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXPHINIT_MASK(Ch) \
		(0x10 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXDLYRESET_MASK(Ch) \
		(0x20 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXDLYBYPASS_MASK(Ch) \
		(0x40 << (8 * (Ch - 1)))
#define XVPHY_TX_BUFFER_BYPASS_TXDLYEN_MASK(Ch) \
		(0x80 << (8 * (Ch - 1)))
/* 0x078: TX_STATUS */
#define XVPHY_TX_STATUS_TXPHALIGNDONE_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_TX_STATUS_TXPHINITDONE_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_TX_STATUS_TXDLYRESETDONE_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_TX_STATUS_TXBUFSTATUS_MASK(Ch) \
		(0x18 << (8 * (Ch - 1)))
#define XVPHY_TX_STATUS_TXBUFSTATUS_SHIFT(Ch) \
		(3 + (8 * (Ch - 1)))
/* 0x07C, 0x080: TX_DRIVER_CH12, TX_DRIVER_CH34 */
#define XVPHY_TX_DRIVER_TXDIFFCTRL_MASK(Ch) \
		(0x000F << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(Ch) \
		(16 * ((Ch - 1) % 2))
#define XVPHY_TX_DRIVER_TXELECIDLE_MASK(Ch) \
		(0x0010 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXELECIDLE_SHIFT(Ch) \
		(4 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXINHIBIT_MASK(Ch) \
		(0x0020 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXINHIBIT_SHIFT(Ch) \
		(5 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPOSTCURSOR_MASK(Ch) \
		(0x07C0 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPOSTCURSOR_SHIFT(Ch) \
		(6 + (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPRECURSOR_MASK(Ch) \
		(0xF800 << (16 * ((Ch - 1) % 2)))
#define XVPHY_TX_DRIVER_TXPRECURSOR_SHIFT(Ch) \
		(11 + (16 * ((Ch - 1) % 2)))
/* 0x100: RX_CONTROL */
#define XVPHY_RX_CONTROL_RX8B10BEN_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_RX_CONTROL_RX8B10BEN_ALL_MASK \
		(XVPHY_RX_CONTROL_RX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH1) | \
		XVPHY_RX_CONTROL_RX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH2) | \
		XVPHY_RX_CONTROL_RX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH3) | \
		XVPHY_RX_CONTROL_RX8B10BEN_MASK(XVPHY_CHANNEL_ID_CH4))
#define XVPHY_RX_CONTROL_RXPOLARITY_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_RX_CONTROL_RXPRBSCNTRESET_MASK(Ch) \
		(0x08 << (8 * (Ch - 1)))
#define XVPHY_RX_CONTROL_RXPRBSSEL_MASK(Ch) \
		(0x70 << (8 * (Ch - 1)))
#define XVPHY_RX_CONTROL_RXPRBSSEL_SHIFT(Ch) \
		(4 + (8 * (Ch - 1)))
/* 0x104: RX_STATUS */
#define XVPHY_RX_STATUS_RXCDRLOCK_MASK(Ch) \
		(0x1 << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXBUFSTATUS_MASK(Ch) \
		(0xE << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXBUFSTATUS_SHIFT(Ch) \
		(1 + (8 * (Ch - 1)))
/* 0x104: RX_EQ_CDR */
#define XVPHY_RX_CONTROL_RXLPMEN_MASK(Ch) \
		(0x01 << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXCDRHOLD_MASK(Ch) \
		(0x02 << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXOSOVRDEN_MASK(Ch) \
		(0x04 << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXLPMLFKLOVRDEN_MASK(Ch) \
		(0x08 << (8 * (Ch - 1)))
#define XVPHY_RX_STATUS_RXLPMHFOVRDEN_MASK(Ch) \
		(0x10 << (8 * (Ch - 1)))
#define XVPHY_RX_CONTROL_RXLPMEN_ALL_MASK \
		(XVPHY_RX_CONTROL_RXLPMEN_MASK(XVPHY_CHANNEL_ID_CH1) | \
		XVPHY_RX_CONTROL_RXLPMEN_MASK(XVPHY_CHANNEL_ID_CH2) | \
		XVPHY_RX_CONTROL_RXLPMEN_MASK(XVPHY_CHANNEL_ID_CH3) | \
		XVPHY_RX_CONTROL_RXLPMEN_MASK(XVPHY_CHANNEL_ID_CH4))
/* 0x110, 0x114, 0x118, 0x11C: INTR_EN, INTR_DIS, INTR_MASK, INTR_STS */
#define XVPHY_INTR_TXRESETDONE_MASK		0x00000001
#define XVPHY_INTR_RXRESETDONE_MASK		0x00000002
#define XVPHY_INTR_CPLL_LOCK_MASK		0x00000004
#define XVPHY_INTR_QPLL0_LOCK_MASK		0x00000008
#define XVPHY_INTR_TXALIGNDONE_MASK		0x00000010
#define XVPHY_INTR_QPLL1_LOCK_MASK		0x00000020
#define XVPHY_INTR_TXCLKDETFREQCHANGE_MASK	0x00000040
#define XVPHY_INTR_RXCLKDETFREQCHANGE_MASK	0x00000080
#define XVPHY_INTR_TXTMRTIMEOUT_MASK		0x40000000
#define XVPHY_INTR_RXTMRTIMEOUT_MASK		0x80000000
#define XVPHY_INTR_QPLL_LOCK_MASK		XVPHY_INTR_QPLL0_LOCK_MASK
/* 0x120, 0x140: MMCM_TXUSRCLK_CTRL, MMCM_RXUSRCLK_CTRL */
#define XVPHY_MMCM_USRCLK_CTRL_CFG_NEW_MASK	0x01
#define XVPHY_MMCM_USRCLK_CTRL_RST_MASK		0x02
#define XVPHY_MMCM_USRCLK_CTRL_CFG_SUCCESS_MASK	0x10
#define XVPHY_MMCM_USRCLK_CTRL_LOCKED_MASK	0x20
#define XVPHY_MMCM_USRCLK_CTRL_PWRDWN_MASK	0x400
#define XVPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK	0x800
/* 0x124, 0x144: MMCM_TXUSRCLK_REG1, MMCM_RXUSRCLK_REG1 */
#define XVPHY_MMCM_USRCLK_REG1_DIVCLK_MASK \
					0x00000FF
#define XVPHY_MMCM_USRCLK_REG1_CLKFBOUT_MULT_MASK \
					0x000FF00
#define XVPHY_MMCM_USRCLK_REG1_CLKFBOUT_MULT_SHIFT \
					8
#define XVPHY_MMCM_USRCLK_REG1_CLKFBOUT_FRAC_MASK \
					0x3FF0000
#define XVPHY_MMCM_USRCLK_REG1_CLKFBOUT_FRAC_SHIFT \
					16
/* 0x128, 0x148: MMCM_TXUSRCLK_REG2, MMCM_RXUSRCLK_REG2 */
#define XVPHY_MMCM_USRCLK_REG2_DIVCLK_MASK \
					0x00000FF
#define XVPHY_MMCM_USRCLK_REG2_CLKOUT0_FRAC_MASK \
					0x3FF0000
#define XVPHY_MMCM_USRCLK_REG2_CLKOUT0_FRAC_SHIFT \
					16
/* 0x12C, 0x130, 0x14C, 0x150: MMCM_TXUSRCLK_REG[3,4], MMCM_RXUSRCLK_REG[3,4] */
#define XVPHY_MMCM_USRCLK_REG34_DIVCLK_MASK \
					0x00000FF
/* 0x134, 0x154: BUFGT_TXUSRCLK, BUFGT_RXUSRCLK */
#define XVPHY_BUFGGT_XXUSRCLK_CLR_MASK	0x1
#define XVPHY_BUFGGT_XXUSRCLK_DIV_MASK	0xE
#define XVPHY_BUFGGT_XXUSRCLK_DIV_SHIFT	1
/* 0x138, 0x158: MISC_TXUSRCLK_REG, MISC_RXUSERCLK_REG */
#define XVPHY_MISC_XXUSRCLK_CKOUT1_OEN_MASK	0x1
#define XVPHY_MISC_XXUSRCLK_REFCLK_CEB_MASK	0x2
/* 0x200: CLKDET_CTRL */
#define XVPHY_CLKDET_CTRL_RUN_MASK			0x1
#define XVPHY_CLKDET_CTRL_TX_TMR_CLR_MASK		0x2
#define XVPHY_CLKDET_CTRL_RX_TMR_CLR_MASK		0x4
#define XVPHY_CLKDET_CTRL_TX_FREQ_RST_MASK		0x8
#define XVPHY_CLKDET_CTRL_RX_FREQ_RST_MASK		0x10
#define XVPHY_CLKDET_CTRL_FREQ_LOCK_THRESH_MASK		0x1FE0
#define XVPHY_CLKDET_CTRL_FREQ_LOCK_THRESH_SHIFT	5
/* 0x204: CLKDET_STAT */
#define XVPHY_CLKDET_STAT_TX_FREQ_ZERO_MASK		0x1
#define XVPHY_CLKDET_STAT_RX_FREQ_ZERO_MASK		0x2
#define XVPHY_CLKDET_STAT_TX_REFCLK_LOCK_MASK		0x3
#define XVPHY_CLKDET_STAT_TX_REFCLK_LOCK_CAP_MASK	0x4
/* 0x300: DRU_CTRL */
#define XVPHY_DRU_CTRL_RST_MASK(Ch)	(0x01 << (8 * (Ch - 1)))
#define XVPHY_DRU_CTRL_EN_MASK(Ch)	(0x02 << (8 * (Ch - 1)))
/* 0x304: DRU_STAT */
#define XVPHY_DRU_STAT_ACTIVE_MASK(Ch)	(0x01 << (8 * (Ch - 1)))
#define XVPHY_DRU_STAT_VERSION_MASK	0xFF000000
#define XVPHY_DRU_STAT_VERSION_SHIFT	24
/* 0x30C, 0x318, 0x324, 0x330: DRU_CFREQ_H_CH[1-4] */
#define XVPHY_DRU_CFREQ_H_MASK		0x1F
/* 0x310, 0x31C, 0x328, 0x334: DRU_GAIN_CH[1-4] */
#define XVPHY_DRU_GAIN_G1_MASK		0x00001F
#define XVPHY_DRU_GAIN_G1_SHIFT		0
#define XVPHY_DRU_GAIN_G1_P_MASK	0x001F00
#define XVPHY_DRU_GAIN_G1_P_SHIFT	8
#define XVPHY_DRU_GAIN_G2_MASK		0x1F0000
#define XVPHY_DRU_GAIN_G2_SHIFT		16
/* @} */

/******************* Macros (Inline Functions) Definitions ********************/

/** @name Register access macro definitions.
  * @{
  */
#define XVphy_In32 Xil_In32
#define XVphy_Out32 Xil_Out32
/* @} */

/******************************************************************************/
/**
 * This is a low-level function that reads from the specified register.
 *
 * @param	BaseAddress is the base address of the device.
 * @param	RegOffset is the register offset to be read from.
 *
 * @return	The 32-bit value of the specified register.
 *
 * @note	C-style signature:
 *		u32 XVphy_ReadReg(u32 BaseAddress, u32 RegOffset)
 *
*******************************************************************************/
#define XVphy_ReadReg(BaseAddress, RegOffset) \
	XVphy_In32((BaseAddress) + (RegOffset))

/******************************************************************************/
/**
 * This is a low-level function that writes to the specified register.
 *
 * @param	BaseAddress is the base address of the device.
 * @param	RegOffset is the register offset to write to.
 * @param	Data is the 32-bit data to write to the specified register.
 *
 * @return	None.
 *
 * @note	C-style signature:
 *		void XVphy_WriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
 *
*******************************************************************************/
#define XVphy_WriteReg(BaseAddress, RegOffset, Data) \
	XVphy_Out32((BaseAddress) + (RegOffset), (Data))

#endif /* XVPHY_HW_H_ */
