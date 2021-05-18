/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDMI 2.1 Rx Subsystem register map
 *
 * Copyright (c) 2021 Xilinx
 * Author: Vishal Sagar <vishal.sagar@xilinx.com>
 */

#ifndef __XILINX_HDMIRX_HW_H__
#define __XILINX_HDMIRX_HW_H__

#include <linux/bits.h>
#include <linux/io.h>

/* VER (Version Interface) peripheral register offsets */
#define HDMIRX_VER_BASE				0x0
/* VER Identification */
#define HDMIRX_VER_ID_OFFSET			((HDMIRX_VER_BASE) + (0 * 4))
/* VER Version */
#define HDMIRX_VER_VERSION_OFFSET		((HDMIRX_VER_BASE) + (1 * 4))
/* VCKE System Counts */
#define HDMIRX_VER_VCKE_SYS_CNT_OFFSET		((HDMIRX_VER_BASE) + (2 * 4))
/* SR/SSB period error 0 counter */
#define HDMIRX_VER_SR_SSB_ERR_CNT0_OFFSET	((HDMIRX_VER_BASE) + (3 * 4))
/* SR/SSB period error 1 counter */
#define HDMIRX_VER_SR_SSB_ERR_CNT1_OFFSET	((HDMIRX_VER_BASE) + (4 * 4))
/* SR/SSB period error 2 counter */
#define HDMIRX_VER_SR_SSB_ERR_CNT2_OFFSET	((HDMIRX_VER_BASE) + (5 * 4))
/* SR/SSB period error 3 counter */
#define HDMIRX_VER_SR_SSB_ERR_CNT3_OFFSET	((HDMIRX_VER_BASE) + (6 * 4))
/* FRL word aligner tap select changed */
#define HDMIRX_VER_DBG_STA_OFFSET		((HDMIRX_VER_BASE) + (7 * 4))
/* FRL Map error */
#define HDMIRX_VER_FRL_MAP_ERR_OFFSET		((HDMIRX_VER_BASE) + (8 * 4))

/* PIO (Parallel Interface) peripheral register offsets */
#define HDMIRX_PIO_BASE				(1 * 64)
/* PIO Identification */
#define HDMIRX_PIO_ID_OFFSET			((HDMIRX_PIO_BASE) + (0 * 4))
/* PIO Control */
#define HDMIRX_PIO_CTRL_OFFSET			((HDMIRX_PIO_BASE) + (1 * 4))
/* PIO Control Set */
#define HDMIRX_PIO_CTRL_SET_OFFSET		((HDMIRX_PIO_BASE) + (2 * 4))
/* PIO Control Clear */
#define HDMIRX_PIO_CTRL_CLR_OFFSET		((HDMIRX_PIO_BASE) + (3 * 4))
/* PIO Status */
#define HDMIRX_PIO_STA_OFFSET			((HDMIRX_PIO_BASE) + (4 * 4))
/* PIO Out */
#define HDMIRX_PIO_OUT_OFFSET			((HDMIRX_PIO_BASE) + (5 * 4))
/* PIO Out Set */
#define HDMIRX_PIO_OUT_SET_OFFSET		((HDMIRX_PIO_BASE) + (6 * 4))
/* PIO Out Clear */
#define HDMIRX_PIO_OUT_CLR_OFFSET		((HDMIRX_PIO_BASE) + (7 * 4))
/* PIO Out Mask */
#define HDMIRX_PIO_OUT_MSK_OFFSET		((HDMIRX_PIO_BASE) + (8 * 4))
/* PIO In */
#define HDMIRX_PIO_IN_OFFSET			((HDMIRX_PIO_BASE) + (9 * 4))
/* PIO In Event */
#define HDMIRX_PIO_IN_EVT_OFFSET		((HDMIRX_PIO_BASE) + (10 * 4))
/* PIO In Event Rising Edge */
#define HDMIRX_PIO_IN_EVT_RE_OFFSET		((HDMIRX_PIO_BASE) + (11 * 4))
/* PIO In Event Falling Edge */
#define HDMIRX_PIO_IN_EVT_FE_OFFSET		((HDMIRX_PIO_BASE) + (12 * 4))

/* Timer peripheral register offsets */
#define HDMIRX_TMR_BASE				(2 * 64)
/* TMR Identification */
#define HDMIRX_TMR_ID_OFFSET			((HDMIRX_TMR_BASE) + (0 * 4))
/* TMR Control */
#define HDMIRX_TMR_CTRL_OFFSET			((HDMIRX_TMR_BASE) + (1 * 4))
/* TMR Control Register Set offset */
#define HDMIRX_TMR_CTRL_SET_OFFSET		((HDMIRX_TMR_BASE) + (2 * 4))
/* TMR Control Register Clear offset */
#define HDMIRX_TMR_CTRL_CLR_OFFSET		((HDMIRX_TMR_BASE) + (3 * 4))
/* TMR Status */
#define HDMIRX_TMR_STA_OFFSET			((HDMIRX_TMR_BASE) + (4 * 4))
/* TMR Counter */
#define HDMIRX_TMR_1_CNT_OFFSET			((HDMIRX_TMR_BASE) + (5 * 4))
/* TMR Counter */
#define HDMIRX_TMR_2_CNT_OFFSET			((HDMIRX_TMR_BASE) + (6 * 4))
/* TMR Counter */
#define HDMIRX_TMR_3_CNT_OFFSET			((HDMIRX_TMR_BASE) + (7 * 4))
/* TMR Counter */
#define HDMIRX_TMR_4_CNT_OFFSET			((HDMIRX_TMR_BASE) + (8 * 4))

/* Video Timing Detector (VTD) peripheral register offsets */
#define HDMIRX_VTD_BASE				(3 * 64)
/* VTD Identification */
#define HDMIRX_VTD_ID_OFFSET			((HDMIRX_VTD_BASE) + (0 * 4))
/* VTD Control */
#define HDMIRX_VTD_CTRL_OFFSET			((HDMIRX_VTD_BASE) + (1 * 4))
/* VTD Control Set */
#define HDMIRX_VTD_CTRL_SET_OFFSET		((HDMIRX_VTD_BASE) + (2 * 4))
/* VTD Control Clear */
#define HDMIRX_VTD_CTRL_CLR_OFFSET		((HDMIRX_VTD_BASE) + (3 * 4))
/* VTD Status */
#define HDMIRX_VTD_STA_OFFSET			((HDMIRX_VTD_BASE) + (4 * 4))
/* VTD Total Pixels */
#define HDMIRX_VTD_TOT_PIX_OFFSET		((HDMIRX_VTD_BASE) + (5 * 4))
/* VTD Active Pixels */
#define HDMIRX_VTD_ACT_PIX_OFFSET		((HDMIRX_VTD_BASE) + (6 * 4))
/* VTD Total Lines */
#define HDMIRX_VTD_TOT_LIN_OFFSET		((HDMIRX_VTD_BASE) + (7 * 4))
/* VTD Active Lines */
#define HDMIRX_VTD_ACT_LIN_OFFSET		((HDMIRX_VTD_BASE) + (8 * 4))
/* VTD Vertical Sync Width */
#define HDMIRX_VTD_VSW_OFFSET			((HDMIRX_VTD_BASE) + (9 * 4))
/* VTD Horizontal Sync Width */
#define HDMIRX_VTD_HSW_OFFSET			((HDMIRX_VTD_BASE) + (10 * 4))
/* VTD Vertical Front Porch */
#define HDMIRX_VTD_VFP_OFFSET			((HDMIRX_VTD_BASE) + (11 * 4))
/* VTD Vertical Back Porch */
#define HDMIRX_VTD_VBP_OFFSET			((HDMIRX_VTD_BASE) + (12 * 4))
/* VTD Horizontal Front Porch */
#define HDMIRX_VTD_HFP_OFFSET			((HDMIRX_VTD_BASE) + (13 * 4))
/* VTD Horizontal Back Porch */
#define HDMIRX_VTD_HBP_OFFSET			((HDMIRX_VTD_BASE) + (14 * 4))

/* DDC (Display Data Channel) peripheral register offsets */
#define HDMIRX_DDC_BASE				(4 * 64)
/* DDC Identification */
#define HDMIRX_DDC_ID_OFFSET			((HDMIRX_DDC_BASE) + (0 * 4))
/* DDC Control */
#define HDMIRX_DDC_CTRL_OFFSET			((HDMIRX_DDC_BASE) + (1 * 4))
/* DDC Control Register Set offset */
#define HDMIRX_DDC_CTRL_SET_OFFSET		((HDMIRX_DDC_BASE) + (2 * 4))
/* DDC Control Register Clear offset */
#define HDMIRX_DDC_CTRL_CLR_OFFSET		((HDMIRX_DDC_BASE) + (3 * 4))
/* DDC Status */
#define HDMIRX_DDC_STA_OFFSET			((HDMIRX_DDC_BASE) + (4 * 4))
/* DDC EDID Status */
#define HDMIRX_DDC_EDID_STA_OFFSET		((HDMIRX_DDC_BASE) + (5 * 4))
/* DDC HDCP Status */
#define HDMIRX_DDC_HDCP_STA_OFFSET		((HDMIRX_DDC_BASE) + (6 * 4))
/* DDC Read EDID segment pointer offset */
#define HDMIRX_DDC_EDID_SP_OFFSET		((HDMIRX_DDC_BASE) + (8 * 4))
/* DDC Read EDID write pointer offset */
#define HDMIRX_DDC_EDID_WP_OFFSET		((HDMIRX_DDC_BASE) + (9 * 4))
/* DDC Read EDID read pointer offset */
#define HDMIRX_DDC_EDID_RP_OFFSET		((HDMIRX_DDC_BASE) + (10 * 4))
/* DDC Read EDID data offset */
#define HDMIRX_DDC_EDID_DATA_OFFSET		((HDMIRX_DDC_BASE) + (11 * 4))
/* DDC Read HDCP address offset */
#define HDMIRX_DDC_HDCP_ADDRESS_OFFSET		((HDMIRX_DDC_BASE) + (12 * 4))
/* DDC Read HDCP data offset */
#define HDMIRX_DDC_HDCP_DATA_OFFSET		((HDMIRX_DDC_BASE) + (13 * 4))

/* Auxiliary (AUX) peripheral register offsets */
#define HDMIRX_AUX_BASE				(5 * 64)
/* AUX Identification */
#define HDMIRX_AUX_ID_OFFSET			((HDMIRX_AUX_BASE) + (0 * 4))
/* AUX Control */
#define HDMIRX_AUX_CTRL_OFFSET			((HDMIRX_AUX_BASE) + (1 * 4))
/* AUX Control Register Set offset */
#define HDMIRX_AUX_CTRL_SET_OFFSET		((HDMIRX_AUX_BASE) + (2 * 4))
/* AUX Control Register Clear offset */
#define HDMIRX_AUX_CTRL_CLR_OFFSET		((HDMIRX_AUX_BASE) + (3 * 4))
/* AUX Status */
#define HDMIRX_AUX_STA_OFFSET			((HDMIRX_AUX_BASE) + (4 * 4))
/* AUX Data */
#define HDMIRX_AUX_DAT_OFFSET			((HDMIRX_AUX_BASE) + (5 * 4))
/* AUX Flush Count */
#define HDMIRX_AUX_FLUSH_CNT_OFFSET		((HDMIRX_AUX_BASE) + (6 * 4))

/* Audio (AUD) peripheral register offsets */
#define HDMIRX_AUD_BASE				(6 * 64)
/* AUD Identification */
#define HDMIRX_AUD_ID_OFFSET			((HDMIRX_AUD_BASE) + (0 * 4))
/* AUD Control */
#define HDMIRX_AUD_CTRL_OFFSET			((HDMIRX_AUD_BASE) + (1 * 4))
/* AUD Control Register Set offset */
#define HDMIRX_AUD_CTRL_SET_OFFSET		((HDMIRX_AUD_BASE) + (2 * 4))
/* AUD Control Register Clear offset */
#define HDMIRX_AUD_CTRL_CLR_OFFSET		((HDMIRX_AUD_BASE) + (3 * 4))
/* AUD Status */
#define HDMIRX_AUD_STA_OFFSET			((HDMIRX_AUD_BASE) + (4 * 4))
/* AUD CTS */
#define HDMIRX_AUD_CTS_OFFSET			((HDMIRX_AUD_BASE) + (5 * 4))
/* AUD N */
#define HDMIRX_AUD_N_OFFSET			((HDMIRX_AUD_BASE) + (6 * 4))
/* AUD Flush Count */
#define HDMIRX_AUD_FLUSH_CNT_OFFSET		((HDMIRX_AUD_BASE) + (7 * 4))

/* Link Status (LNKSTA) peripheral register offsets */
#define HDMIRX_LNKSTA_BASE			(7 * 64)
/* LNKSTA Identification */
#define HDMIRX_LNKSTA_ID_OFFSET			((HDMIRX_LNKSTA_BASE) + (0 * 4))
/* LNKSTA Control */
#define HDMIRX_LNKSTA_CTRL_OFFSET		((HDMIRX_LNKSTA_BASE) + (1 * 4))
/* LNKSTA Control Register Set offset */
#define HDMIRX_LNKSTA_CTRL_SET_OFFSET		((HDMIRX_LNKSTA_BASE) + (2 * 4))
/* LNKSTA Control Register Clear offset */
#define HDMIRX_LNKSTA_CTRL_CLR_OFFSET		((HDMIRX_LNKSTA_BASE) + (3 * 4))
/* LNKSTA Status */
#define HDMIRX_LNKSTA_STA_OFFSET		((HDMIRX_LNKSTA_BASE) + (4 * 4))
/* LNKSTA Link Error Counter Channel 0 */
#define HDMIRX_LNKSTA_LNK_ERR0_OFFSET		((HDMIRX_LNKSTA_BASE) + (5 * 4))
/* LNKSTA Link Error Counter Channel 1 */
#define HDMIRX_LNKSTA_LNK_ERR1_OFFSET		((HDMIRX_LNKSTA_BASE) + (6 * 4))
/* LNKSTA Link Error Counter Channel 2 */
#define HDMIRX_LNKSTA_LNK_ERR2_OFFSET		((HDMIRX_LNKSTA_BASE) + (7 * 4))
/* Packet ECC Error */
#define HDMIRX_LNKSTA_PKT_ECC_ERR_OFFSET	((HDMIRX_LNKSTA_BASE) + (8 * 4))
/* Tri-byte Analyzer Timing */
#define HDMIRX_LNKSTA_TRIB_ANLZ_TIM_OFFSET	((HDMIRX_LNKSTA_BASE) + (9 * 4))
/* Tri-byte HBP_HS */
#define HDMIRX_LNKSTA_TRIB_HBP_HS_OFFSET	((HDMIRX_LNKSTA_BASE) + (10 * 4))
/* Tri-byte Analyzer Line Size */
#define HDMIRX_LNKSTA_TRIB_ANLZ_LN_ACT_OFFSET	((HDMIRX_LNKSTA_BASE) + (11 * 4))

/* Fixed Rate Link (FRL) peripheral register offsets */
#define HDMIRX_FRL_BASE				(8 * 64)
/* FRL Identification */
#define HDMIRX_FRL_ID_OFFSET			((HDMIRX_FRL_BASE) + (0 * 4))
/* FRL Control */
#define HDMIRX_FRL_CTRL_OFFSET			((HDMIRX_FRL_BASE) + (1 * 4))
/* FRL Control Register Set offset */
#define HDMIRX_FRL_CTRL_SET_OFFSET		((HDMIRX_FRL_BASE) + (2 * 4))
/* FRL Control Register Clear offset */
#define HDMIRX_FRL_CTRL_CLR_OFFSET		((HDMIRX_FRL_BASE) + (3 * 4))
/* FRL Status */
#define HDMIRX_FRL_STA_OFFSET			((HDMIRX_FRL_BASE) + (4 * 4))
/* FRL Video Clock  to VCKE Ratio */
#define HDMIRX_FRL_VCLK_VCKE_RATIO_OFFSET	((HDMIRX_FRL_BASE) + (7 * 4))
/* FRL Video Clock */
#define HDMIRX_FRL_SCDC_OFFSET			((HDMIRX_FRL_BASE) + (8 * 4))
/* FRL Total Data */
#define HDMIRX_FRL_RATIO_TOT_OFFSET		((HDMIRX_FRL_BASE) + (9 * 4))
/* FRL Total Active Data */
#define HDMIRX_FRL_RATIO_ACT_OFFSET		((HDMIRX_FRL_BASE) + (10 * 4))
/* Reed-Solomon FEC Counter Data */
#define HDMIRX_FRL_RSFC_CNT_OFFSET		((HDMIRX_FRL_BASE) + (11 * 4))
/* FRL Error Count Data */
#define HDMIRX_FRL_ERR_CNT1_OFFSET		((HDMIRX_FRL_BASE) + (12 * 4))
/* Video Lock Count Data */
#define HDMIRX_FRL_VID_LOCK_CNT_OFFSET		((HDMIRX_FRL_BASE) + (13 * 4))

/* VER (Version Interface) peripheral register masks and shift */
/* FRL SR/SSB period error during training period mask */
#define HDMIRX_SR_SSB_ERR1_MASK			GENMASK(15, 0)
/* FRL SR/SSB period error during NON-training period mask */
#define HDMIRX_SR_SSB_ERR2_MASK			GENMASK(31, 16)

/* FRL Word aligner tap select changed all lanes mask */
#define HDMIRX_DBG_STA_WA_TAP_CHGALL_MASK	GENMASK(3, 0)
/* Word aligner tap select changed lane 0 mask */
#define HDMIRX_DBG_STA_WA_TAP_CHG0_MASK		BIT(0)
/* Word aligner tap select changed lane 1 mask */
#define HDMIRX_DBG_STA_WA_TAP_CHG1_MASK		BIT(1)
/* Word aligner tap select changed lane 2 mask */
#define HDMIRX_DBG_STA_WA_TAP_CHG2_MASK		BIT(2)
/* FRL Word aligner tap select changed lane 3 mask */
#define HDMIRX_DBG_STA_WA_TAP_CHG3_MASK		BIT(3)
/* FRL Word aligner tap select changed all lanes mask */
#define HDMIRX_DBG_STA_WA_LOCK_CHGALL_MASK	GENMASK(7, 4)
/* Word aligner tap select changed lane 0 mask */
#define HDMIRX_DBG_STA_WA_LOCK_CHG0_MASK	BIT(4)
/* Word aligner tap select changed lane 1 mask */
#define HDMIRX_DBG_STA_WA_LOCK_CHG1_MASK	BIT(5)
/* Word aligner tap select changed lane 2 mask */
#define HDMIRX_DBG_STA_WA_LOCK_CHG2_MASK	BIT(6)
/* FRL Word aligner tap select changed lane 3 mask */
#define HDMIRX_DBG_STA_WA_LOCK_CHG3_MASK	BIT(7)
/* FRL Word aligner tap select changed all lanes mask */
#define HDMIRX_DBG_STA_SCRM_LOCK_CHGALL_MASK	GENMASK(11, 8)
/* Word aligner tap select changed lane 0 mask */
#define HDMIRX_DBG_STA_SCRM_LOCK_CHG0_MASK	BIT(8)
/* Word aligner tap select changed lane 1 mask */
#define HDMIRX_DBG_STA_SCRM_LOCK_CHG1_MASK	BIT(9)
/* Word aligner tap select changed lane 2 mask */
#define HDMIRX_DBG_STA_SCRM_LOCK_CHG2_MASK	BIT(10)
/* FRL Word aligner tap select changed lane 3 mask */
#define HDMIRX_DBG_STA_SCRM_LOCK_CHG3_MASK	BIT(11)
/* FRL Word aligner tap select changed all lanes mask */
#define HDMIRX_DBG_STA_LANE_LOCK_CHGALL_MASK	GENMASK(15, 12)
/* Word aligner tap select changed lane 0 mask */
#define HDMIRX_DBG_STA_LANE_LOCK_CHG0_MASK	BIT(12)
/* Word aligner tap select changed lane 1 mask */
#define HDMIRX_DBG_STA_LANE_LOCK_CHG1_MASK	BIT(13)
/* Word aligner tap select changed lane 2 mask */
#define HDMIRX_DBG_STA_LANE_LOCK_CHG2_MASK	BIT(14)
/* FRL Word aligner tap select changed lane 3 mask */
#define HDMIRX_DBG_STA_LANE_LOCK_CHG3_MASK	BIT(15)
/* Word aligner tap select changed lane 0 mask */
#define HDMIRX_DBG_STA_SKEW_LOCK_CHG_MASK	BIT(16)

/* PIO peripheral Control register masks */
/* PIO Control Run mask */
#define HDMIRX_PIO_CTRL_RUN_MASK		BIT(0)
/* PIO Control Interrupt Enable mask */
#define HDMIRX_PIO_CTRL_IE_MASK			BIT(1)

/* PIO peripheral Status register masks */
/* PIO Status Interrupt mask */
#define HDMIRX_PIO_STA_IRQ_MASK			BIT(0)
/* PIO Status Event mask */
#define HDMIRX_PIO_STA_EVT_MASK			BIT(1)

/* PIO peripheral PIO Out register masks and shifts */
/* PIO Out Reset mask */
#define HDMIRX_PIO_OUT_RESET_MASK		BIT(0)
/* PIO Out video enable mask */
#define HDMIRX_PIO_OUT_LNK_EN_MASK		BIT(1)
/* PIO Out video enable mask */
#define HDMIRX_PIO_OUT_VID_EN_MASK		BIT(2)
/* PIO Out Hot-Plug Detect mask */
#define HDMIRX_PIO_OUT_HPD_MASK			BIT(3)
/* PIO Out Deep Color mask */
#define HDMIRX_PIO_OUT_DEEP_COLOR_MASK		GENMASK(5, 4)
/* PIO Out Pixel Rate mask */
#define HDMIRX_PIO_OUT_PIXEL_RATE_MASK		GENMASK(7, 6)
/* PIO Out Sample Rate mask */
#define HDMIRX_PIO_OUT_SAMPLE_RATE_MASK		GENMASK(9, 8)
/* PIO Out Color Space mask */
#define HDMIRX_PIO_OUT_COLOR_SPACE_MASK		GENMASK(11, 10)
/* PIO Out Scrambler mask */
#define HDMIRX_PIO_OUT_SCRM_MASK		BIT(12)
/* PIO Out Pixel Phase mask */
#define HDMIRX_PIO_OUT_PP_MASK			GENMASK(18, 16)
/* PIO Out Axis Enable mask */
#define HDMIRX_PIO_OUT_AXIS_EN_MASK		BIT(19)
/* PIO Out Deep Color shift */
#define HDMIRX_PIO_OUT_DEEP_COLOR_SHIFT		4
/* PIO Out Pixel Rate Shift */
#define HDMIRX_PIO_OUT_PIXEL_RATE_SHIFT		6
/* PIO Out Sample Rate shift */
#define HDMIRX_PIO_OUT_SAMPLE_RATE_SHIFT	8
/* PIO Out Color Space shift */
#define HDMIRX_PIO_OUT_COLOR_SPACE_SHIFT	10
/* PIO Out Pixel Phase shift */
#define HDMIRX_PIO_OUT_PP_SHIFT			16
/* PIO Out Bridge_YUV420 mask */
#define HDMIRX_PIO_OUT_BRIDGE_YUV420_MASK	BIT(29)
/* PIO Out Bridge_Pixel drop mask */
#define HDMIRX_PIO_OUT_BRIDGE_PIXEL_MASK	BIT(30)

/* PIO Out INT_VRST mask */
#define HDMIRX_PIO_OUT_INT_VRST_MASK		BIT(0)
/* PIO Out INT_LRST mask */
#define HDMIRX_PIO_OUT_INT_LRST_MASK		BIT(20)
/* PIO Out EXT_VRST mask */
#define HDMIRX_PIO_OUT_EXT_VRST_MASK		BIT(21)
/* PIO Out EXT_SYSRST mask */
#define HDMIRX_PIO_OUT_EXT_SYSRST_MASK		BIT(22)

/* PIO peripheral PIO In register masks */
/* PIO In cable detect mask */
#define HDMIRX_PIO_IN_DET_MASK			BIT(0)
/* PIO In link ready mask */
#define HDMIRX_PIO_IN_LNK_RDY_MASK		BIT(1)
/* PIO In video ready mask */
#define HDMIRX_PIO_IN_VID_RDY_MASK		BIT(2)
/* PIO In Mode mask */
#define HDMIRX_PIO_IN_MODE_MASK			BIT(3)
/* PIO In Scrambler lock 0 mask */
#define HDMIRX_PIO_IN_SCRAMBLER_LOCK0_MASK	BIT(4)
/* PIO In Scrambler lock 1 mask */
#define HDMIRX_PIO_IN_SCRAMBLER_LOCK1_MASK	BIT(5)
/* PIO In Scrambler lock 2 mask */
#define HDMIRX_PIO_IN_SCRAMBLER_LOCK2_MASK	BIT(6)
/* PIO In SCDC scrambler enable mask */
#define HDMIRX_PIO_IN_SCDC_SCRAMBLER_ENABLE_MASK	BIT(7)
/* PIO In SCDC TMDS clock ratio mask */
#define HDMIRX_PIO_IN_SCDC_TMDS_CLOCK_RATIO_MASK	BIT(8)
/* PIO In alinger lock mask */
#define HDMIRX_PIO_IN_ALIGNER_LOCK_MASK		BIT(9)
/* PIO In bridge overflow mask */
#define HDMIRX_PIO_IN_BRDG_OVERFLOW_MASK	BIT(10)

/* Timer peripheral Control register masks */
/* TMR Control Run mask */
#define HDMIRX_TMR1_CTRL_RUN_MASK		BIT(0)
/* TMR Control Interrupt Enable mask */
#define HDMIRX_TMR1_CTRL_IE_MASK		BIT(1)
/* TMR Control Run mask */
#define HDMIRX_TMR2_CTRL_RUN_MASK		BIT(2)
/* TMR Control Interrupt Enable mask */
#define HDMIRX_TMR2_CTRL_IE_MASK		BIT(3)
/* TMR Control Run mask */
#define HDMIRX_TMR3_CTRL_RUN_MASK		BIT(4)
/* TMR Control Interrupt Enable mask */
#define HDMIRX_TMR3_CTRL_IE_MASK		BIT(5)
/* TMR Control Run mask */
#define HDMIRX_TMR4_CTRL_RUN_MASK		BIT(6)
/* TMR Control Interrupt Enable mask */
#define HDMIRX_TMR4_CTRL_IE_MASK		BIT(7)

/* Timer peripheral Status register masks */
/* TMR Status Interrupt mask */
#define HDMIRX_TMR_STA_IRQ_MASK			BIT(0)
/* TMR Status counter Event mask */
#define HDMIRX_TMR1_STA_CNT_EVT_MASK		BIT(1)
/* TMR Status counter Event mask */
#define HDMIRX_TMR2_STA_CNT_EVT_MASK		BIT(3)
/* TMR Status counter Event mask */
#define HDMIRX_TMR3_STA_CNT_EVT_MASK		BIT(5)
/* TMR Status counter Event mask */
#define HDMIRX_TMR4_STA_CNT_EVT_MASK		BIT(7)

/* Video timing detector peripheral Control register masks and shift */
/* VTD Control Run mask */
#define HDMIRX_VTD_CTRL_RUN_MASK		BIT(0)
/* VTD Control Interrupt Enable mask */
#define HDMIRX_VTD_CTRL_IE_MASK			BIT(1)
/* VTD Control field polarity mask */
#define HDMIRX_VTD_CTRL_FIELD_POL_MASK		BIT(2)
/* VTD Control field polarity mask */
#define HDMIRX_VTD_CTRL_SYNC_LOSS_MASK		BIT(3)
/* VTD Control timebase shift */
#define HDMIRX_VTD_CTRL_TIMEBASE_SHIFT		8
/* VTD Control timebase mask */
#define HDMIRX_VTD_CTRL_TIMERBASE_MASK		GENMASK(31, 8)

#define HDMIRX_VTD_VF0_MASK			GENMASK(15, 0)
#define HDMIRX_VTD_VF1_MASK			GENMASK(31, 16)

/* Video timing detector peripheral Status register masks */
/* VTD Status Interrupt mask */
#define HDMIRX_VTD_STA_IRQ_MASK			BIT(0)
/* VTD Status timebase event mask */
#define HDMIRX_VTD_STA_TIMEBASE_EVT_MASK	BIT(1)
/* VTD Status Vsync Polarity mask */
#define HDMIRX_VTD_STA_VS_POL_MASK		BIT(3)
/* VTD Status Hsync Polarity mask */
#define HDMIRX_VTD_STA_HS_POL_MASK		BIT(4)
/* VTD Status Format mask */
#define HDMIRX_VTD_STA_FMT_MASK			BIT(5)
/* VTD Status Sync Loss mask */
#define HDMIRX_VTD_STA_SYNC_LOSS_EVT_MASK	BIT(6)

/* DDC peripheral Control register masks */
/* DDC Control Run mask */
#define HDMIRX_DDC_CTRL_RUN_MASK		BIT(0)
/* DDC Control Interrupt enable mask */
#define HDMIRX_DDC_CTRL_IE_MASK			BIT(1)
/* DDC Control EDID enable mask */
#define HDMIRX_DDC_CTRL_EDID_EN_MASK		BIT(2)
/* DDC Control SCDC enable mask */
#define HDMIRX_DDC_CTRL_SCDC_EN_MASK		BIT(3)
/* DDC Control HDCP enable mask */
#define HDMIRX_DDC_CTRL_HDCP_EN_MASK		BIT(4)
/* DDC Control SCDC clear mask */
#define HDMIRX_DDC_CTRL_SCDC_CLR_MASK		BIT(5)
/* DDC Control write message clear mask */
#define HDMIRX_DDC_CTRL_WMSG_CLR_MASK		BIT(6)
/* DDC Control read message clear mask */
#define HDMIRX_DDC_CTRL_RMSG_CLR_MASK		BIT(7)
/* DDC Control HDCP mode mask */
#define HDMIRX_DDC_CTRL_HDCP_MODE_MASK		BIT(8)
/* DDC Control SCDC Read Write Event mask */
#define HDMIRX_DDC_CTRL_SCDC_RD_WR_EVT_EN_MASK	BIT(9)

/* DDC peripheral Status register masks */
/* DDC Status Interrupt mask */
#define HDMIRX_DDC_STA_IRQ_MASK			BIT(0)
/* DDC Status Event mask */
#define HDMIRX_DDC_STA_EVT_MASK			BIT(1)
/* DDC Status Busy mask */
#define HDMIRX_DDC_STA_BUSY_MASK		BIT(2)
/* DDC Status state of the SCL input mask */
#define HDMIRX_DDC_STA_SCL_MASK			BIT(3)
/* DDC Status state of the SDA input mask */
#define HDMIRX_DDC_STA_SDA_MASK			BIT(4)
/* DDC Status HDCP AKSV event mask */
#define HDMIRX_DDC_STA_HDCP_AKSV_EVT_MASK	BIT(5)
/* DDC Status HDCP write message buffer new event mask */
#define HDMIRX_DDC_STA_HDCP_WMSG_NEW_EVT_MASK	BIT(6)
/* DDC Status HDCP read message buffer end event mask */
#define HDMIRX_DDC_STA_HDCP_RMSG_END_EVT_MASK	BIT(7)
/* DDC Status HDCP read message buffer not completed event mask */
#define HDMIRX_DDC_STA_HDCP_RMSG_NC_EVT_MASK	BIT(8)
/* DDC Status HDCP 1.4 protocol flag */
#define HDMIRX_DDC_STA_HDCP_1_PROT_MASK		BIT(9)
/* DDC Status HDCP 2.2 protocol flag */
#define HDMIRX_DDC_STA_HDCP_2_PROT_MASK		BIT(10)
/* DDC Status HDCP 1.4 protocol event flag */
#define HDMIRX_DDC_STA_HDCP_1_PROT_EVT_MASK	BIT(11)
/* DDC Status HDCP 2.2 protocol event flag */
#define HDMIRX_DDC_STA_HDCP_2_PROT_EVT_MASK	BIT(12)
/* DDC Status SCDC Read Write event flag */
#define HDMIRX_DDC_STA_SCDC_RD_WR_EVT_MASK	BIT(13)
/* DDC Status EDID words mask */
#define HDMIRX_DDC_STA_EDID_WORDS_MASK		GENMASK(15, 0)
/* DDC Status HDCP 2.2 write message buffer words mask */
#define HDMIRX_DDC_STA_HDCP_WMSG_WORDS_MASK	GENMASK(10, 0)
/* DDC Status HDCP 2.2 write message buffer empty mask */
#define HDMIRX_DDC_STA_HDCP_WMSG_EP_MASK	BIT(11)
/* DDC Status HDCP 2.2 read message buffer words mask */
#define HDMIRX_DDC_STA_HDCP_RMSG_WORDS_MASK	GENMASK(26, 16)
/* DDC Status HDCP 2.2 read message buffer empty mask */
#define HDMIRX_DDC_STA_HDCP_RMSG_EP_MASK	BIT(27)

/* AUX peripheral Control register masks */
/* AUX Control Run mask */
#define HDMIRX_AUX_CTRL_RUN_MASK		BIT(0)
/* AUX Control Interrupt Enable mask */
#define HDMIRX_AUX_CTRL_IE_MASK			BIT(1)

/* AUX peripheral Status register masks and shifts */
/* AUX Status Interrupt mask */
#define HDMIRX_AUX_STA_IRQ_MASK			BIT(0)
/* AUX Status New Packet mask */
#define HDMIRX_AUX_STA_NEW_MASK			BIT(1)
/* AUX Status New Packet mask */
#define HDMIRX_AUX_STA_ERR_MASK			BIT(2)
/* AUX Status AVI infoframe mask */
#define HDMIRX_AUX_STA_AVI_MASK			BIT(3)
/* AUX Status General control packet mask */
#define HDMIRX_AUX_STA_GCP_MASK			BIT(4)
/* AUX Status FIFO Empty mask */
#define HDMIRX_AUX_STA_FIFO_EP_MASK		BIT(5)
/* AUX Status FIFO Full mask */
#define HDMIRX_AUX_STA_FIFO_FL_MASK		BIT(6)
/* AUX Status Dynamic HDR event mask */
#define HDMIRX_AUX_STA_DYN_HDR_EVT_MASK		BIT(20)
/* AUX Status VRR CD mask */
#define HDMIRX_AUX_STA_VRR_CD_EVT_MASK		BIT(21)
/* AUX Status FSYNC CD mask */
#define HDMIRX_AUX_STA_FSYNC_CD_EVT_MASK	BIT(22)
/* AUX Status GCP ColorDepth mask */
#define HDMIRX_AUX_STA_GCP_CD_EVT_MASK		BIT(25)
/* AUX Status GCP avmute mask */
#define HDMIRX_AUX_STA_GCP_AVMUTE_MASK		BIT(31)
/* AUX Status AVI VIC mask */
#define HDMIRX_AUX_STA_AVI_VIC_MASK		GENMASK(15, 8)
/* AUX Status AVI colorspace mask */
#define HDMIRX_AUX_STA_AVI_CS_MASK		GENMASK(17, 16)
/* AUX Status GCP colordepth mask */
#define HDMIRX_AUX_STA_GCP_CD_MASK		GENMASK(27, 26)
/* AUX Status GCP pixel phase mask */
#define HDMIRX_AUX_STA_GCP_PP_MASK		GENMASK(30, 28)

/* Audio peripheral Control register masks */
/* AUD Control Run mask */
#define HDMIRX_AUD_CTRL_RUN_MASK		BIT(0)
/* AUD Control Interrupt Enable mask */
#define HDMIRX_AUD_CTRL_IE_MASK			BIT(1)
/* AUD Control ACR Update Event Enable mask */
#define HDMIRX_AUD_CTRL_ACR_UPD_EVT_EN_MASK	BIT(2)

/* AUD peripheral Status register masks and shift */
/* AUD Status Interrupt mask */
#define HDMIRX_AUD_STA_IRQ_MASK			BIT(0)
/* AUD Status Event mask */
#define HDMIRX_AUD_STA_ACT_EVT_MASK		BIT(1)
/* AUD Status Event mask */
#define HDMIRX_AUD_STA_CH_EVT_MASK		BIT(2)
/* AUD Status Active mask */
#define HDMIRX_AUD_STA_ACT_MASK			BIT(3)
/* AUD Status Audio channel mask */
#define HDMIRX_AUD_STA_AUD_CH_MASK		GENMASK(5, 4)
/* AUD Status Audio Format mask */
#define HDMIRX_AUD_STA_AUD_FMT_MASK		GENMASK(8, 6)
/* AUD Status ACR Update mask */
#define HDMIRX_AUD_STA_ACR_UPD_MASK		BIT(9)

/* Link Status (LNKSTA) peripheral Control register masks */
/* LNKSTA Control Run mask */
#define HDMIRX_LNKSTA_CTRL_RUN_MASK		BIT(0)
/* LNKSTA Control Interrupt Enable mask */
#define HDMIRX_LNKSTA_CTRL_IE_MASK		BIT(1)
/* LNKSTA Control Error Clear mask */
#define HDMIRX_LNKSTA_CTRL_ERR_CLR_MASK		BIT(2)

/* Link Status (LNKSTA) peripheral Status register masks */
/* LNKSTA Status Interrupt mask */
#define HDMIRX_LNKSTA_STA_IRQ_MASK		BIT(0)
/* LNKSTA Status Maximum Errors mask */
#define HDMIRX_LNKSTA_STA_ERR_MAX_MASK		BIT(1)
#define HDMIRX_LNKSTA_STA_DCS_8CD_LOCK_MASK	BIT(2)
#define HDMIRX_LNKSTA_STA_DCS_DEEP_LOCK_MASK	BIT(3)

/* Tri-byte Analyzer register masks */
/* Tri-byte analyzer timing changed count mask */
#define HDMIRX_TRIB_ANLZ_TIM_CHGD_CNT_MASK	GENMASK(15, 0)
/* Tri-byte analyzer timing vsync polarity mask */
#define HDMIRX_TRIB_ANLZ_TIM_VS_POL_MASK	BIT(16)
/* Tri-byte analyzer timing hsync polarity mask */
#define HDMIRX_TRIB_ANLZ_TIM_HS_POL_MASK	BIT(17)

/* Tri-byte Analyzer register masks */
/* Tri-byte hsync size mask */
#define HDMIRX_TRIB_HBP_HS_HS_SZ_MASK		GENMASK(15, 0)
/* Tri-byte hbp size mask */
#define HDMIRX_TRIB_HBP_HS_HBP_SZ_MASK		GENMASK(31, 16)

/* Tri-byte Analyzer register masks */
/* Tri-byte analyzer act size mask */
#define HDMIRX_TRIB_ANLZ_LN_ACT_ACT_SZ_MASK	GENMASK(15, 0)
/* Tri-byte analyzer line act mask */
#define HDMIRX_TRIB_ANLZ_LN_ACT_LN_SZ_MASK	GENMASK(31, 16)

/* FRL Control register masks */
/* FRL Control Resetn mask */
#define HDMIRX_FRL_CTRL_RSTN_MASK			BIT(0)
/* FRL Control Interrupt Enable mask */
#define HDMIRX_FRL_CTRL_IE_MASK				BIT(1)
/* FRL Control Clock Ratio Update Event Enable mask */
#define HDMIRX_FRL_CTRL_CLK_RATIO_UPD_EVT_EN_MASK	BIT(2)
/* FRL Control Skew Event Enable mask */
#define HDMIRX_FRL_CTRL_SKEW_EVT_EN_MASK		BIT(3)
/* FRL Control FLT Clear mask */
#define HDMIRX_FRL_CTRL_FLT_CLR_MASK			BIT(5)
/* FRL Control FLT Threshold mask */
#define HDMIRX_FRL_CTRL_FLT_THRES_MASK			GENMASK(13, 6)
/* FRL Control FRL Rate Write Event Enable */
#define HDMIRX_FRL_CTRL_FRL_RATE_WR_EVT_EN_MASK		BIT(14)
/* FRL Control DPACK Reset mask */
#define HDMIRX_FRL_CTRL_DPACK_RST_MASK			BIT(15)
/* FRL Control DPACK Error Counter Clear mask */
#define HDMIRX_FRL_CTRL_DPACK_ERR_CNT_CLR_MASK		BIT(16)
/* FRL Control DPACK Auto Reset Disable mask */
#define HDMIRX_FRL_CTRL_DPACK_AUTO_RST_DIS_MASK		BIT(17)
/* FRL Control Video Lock Reset Disable Mask */
#define HDMIRX_FRL_CTRL_VID_LOCK_RST_DIS_MASK		BIT(19)
/* FRL PARS module reset */
#define HDMIRX_FRL_CTRL_PARS_RST_MASK			BIT(21)
/* FRL FEC module reset */
#define HDMIRX_FRL_CTRL_FEC_RST_MASK			BIT(22)
/* FRL MAP module reset */
#define HDMIRX_FRL_CTRL_MAP_RST_MASK			BIT(23)
/* FRL SKEW module reset */
#define HDMIRX_FRL_CTRL_SKEW_RST_MASK			BIT(24)

/* FRL Status register masks */
/* FRL Status Interrupt mask */
#define HDMIRX_FRL_STA_IRQ_MASK				BIT(0)
/* FRL Status Event mask */
#define HDMIRX_FRL_STA_EVT_MAS				BIT(1)
/* FRL Status FLT Pattern Match event mask */
#define HDMIRX_FRL_STA_FLT_PM_EVT_MASK			BIT(2)
/* FRL Status FLT Pattern Match All Lanes mask */
#define HDMIRX_FRL_STA_FLT_PM_ALLL_MASK			GENMASK(6, 3)
/* FRL Status FLT Pattern MatchLane 0 mask */
#define HDMIRX_FRL_STA_FLT_PM_L0_MASK			BIT(3)
/* FRL Status FLT Pattern MatchLane 1 mask */
#define HDMIRX_FRL_STA_FLT_PM_L1_MASK			BIT(4)
/* FRL Status FLT Pattern MatchLane 2 mask */
#define HDMIRX_FRL_STA_FLT_PM_L2_MASK			BIT(5)
/* FRL Status FLT Pattern MatchLane 3 mask */
#define HDMIRX_FRL_STA_FLT_PM_L3_MASK			BIT(6)
/* FRL Status FLT Update event mask */
#define HDMIRX_FRL_STA_FLT_UPD_EVT_MASK			BIT(7)
/* FRL Status FRL Rate change event mask */
#define HDMIRX_FRL_STA_RATE_EVT_MASK			BIT(8)
/* FRL Status Lane Lock event mask */
#define HDMIRX_FRL_STA_LANE_LOCK_EVT_MASK		BIT(9)
/* FRL Status Clock Ratio Update event mask */
#define HDMIRX_FRL_STA_CLK_RATIO_UPD_EVT_MASK		BIT(10)
/* FRL Status Skew Lock event mask */
#define HDMIRX_FRL_STA_SKEW_LOCK_EVT_MASK		BIT(11)
/* FRL Status Lane Lock All Lanes mask */
#define HDMIRX_FRL_STA_LANE_LOCK_ALLL_MASK		GENMASK(15, 12)
/* FRL Status Lane Lock L0 mask */
#define HDMIRX_FRL_STA_LANE_LOCK_L0_MASK		BIT(12)
/* FRL Status Lane Lock L1 mask */
#define HDMIRX_FRL_STA_LANE_LOCK_L1_MASK		BIT(13)
/* FRL Status Lane Lock L2 mask */
#define HDMIRX_FRL_STA_LANE_LOCK_L2_MASK		BIT(14)
/* FRL Status Lane Lock L3 mask */
#define HDMIRX_FRL_STA_LANE_LOCK_L3_MASK		BIT(15)
/* FRL Status Aligner Lock All Lanes mask */
#define HDMIRX_FRL_STA_WA_LOCK_ALLL_MASK		GENMASK(19, 16)
/* FRL Status Aligner Lock L0 mask */
#define HDMIRX_FRL_STA_WA_LOCK_L0_MASK			BIT(16)
/* FRL Status Aligner Lock L1 mask */
#define HDMIRX_FRL_STA_WA_LOCK_L1_MASK			BIT(17)
/* FRL Status Aligner Lock L2 mask */
#define HDMIRX_FRL_STA_WA_LOCK_L2_MASK			BIT(18)
/* FRL Status Aligner Lock L3 mask */
#define HDMIRX_FRL_STA_WA_LOCK_L3_MASK			BIT(19)
/* FRL Status Scrambler All Lanes mask */
#define HDMIRX_FRL_STA_SCRM_LOCK_ALLL_MASK		GENMASK(23, 20)
/* FRL Status Scrambler Lock L0 mask */
#define HDMIRX_FRL_STA_SCRM_LOCK_L0_MASK		BIT(20)
/* FRL Status Scrambler Lock L1 mask */
#define HDMIRX_FRL_STA_SCRM_LOCK_L1_MASK		BIT(21)
/* FRL Status Scrambler Lock L2 mask */
#define HDMIRX_FRL_STA_SCRM_LOCK_L2_MASK		BIT(22)
/* FRL Status Scrambler Lock L3 mask */
#define HDMIRX_FRL_STA_SCRM_LOCK_L3_MASK		BIT(23)
/* FRL Status Skew Lock mask */
#define HDMIRX_FRL_STA_SKEW_LOCK_MASK			BIT(24)
/* FRL Status Video STR mask */
#define HDMIRX_FRL_STA_STR_MASK				BIT(25)
/* FRL Status Video Lock mask */
#define HDMIRX_FRL_STA_VID_LOCK_MASK			BIT(26)
/* FRL Status Mode mask */
#define HDMIRX_FRL_STA_FRL_MODE_MASK			BIT(27)
/* FRL Status Lanes mask */
#define HDMIRX_FRL_STA_FRL_LANES_MASK			BIT(28)
/* FRL Status Rate mask */
#define HDMIRX_FRL_STA_FRL_RATE_MASK			GENMASK(31, 29)

/* FRL Link Clock register masks */
/* FRL Link Clock mask */
#define HDMIRX_FRL_LNK_CLK_MASK		0xFFFFF
/* FRL Video Clock mask */
#define HDMIRX_FRL_VID_CLK_MASK		0xFFFFF

/* FRL SCDC register masks */
/* FRL SCDC Address mask */
#define HDMIRX_FRL_SCDC_ADDR_MASK			GENMASK(7, 0)
/* FRL SCDC Data mask */
#define HDMIRX_FRL_SCDC_DAT_MASK			GENMASK(15, 8)
/* FRL SCDC Data Shift */
#define HDMIRX_FRL_SCDC_DAT_SHIFT			8
/* FRL SCDC Write mask */
#define HDMIRX_FRL_SCDC_WR_MASK				BIT(16)
/* FRL SCDC Read mask */
#define HDMIRX_FRL_SCDC_RD_MASK				BIT(17)
/* FRL SCDC Ready mask */
#define HDMIRX_FRL_SCDC_RDY_MASK			BIT(18)

#define HDMIRX_FRL_RATIO_TOT_MASK	0xFFFFFF
#define HDMIRX_FRL_RATIO_ACT_MASK	0xFFFFFF

#define HDMIRX_FRL_ERR_CNT1_DPACK_ERR_CNT_MASK		GENMASK(31, 16)
#define HDMIRX_FRL_ERR_CNT1_RSCC_ERR_CNT_MASK		GENMASK(15, 0)

/* Peripheral ID and General shift values */
/* 16 shift value */
#define HDMIRX_SHIFT_16		16
/* 16 bit mask value */
#define HDMIRX_MASK_16		GENMASK(15, 0)
/* PIO ID */
#define HDMIRX_PIO_ID		0x2200

#endif /* __XILINX_HDMIRX_HW_H__ */
