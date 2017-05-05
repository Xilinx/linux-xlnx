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
 * @file xvphy_hdmi.h
 *
 * The Xilinx Video PHY (VPHY) driver. This driver supports the Xilinx Video PHY
 * IP core.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm   10/19/15 Initial release.
 * 1.1   gm   02/01/16 Added GTPE2 and GTHE4 constants
 * 1.2   gm            Added XVphy_HdmiMmcmStart function
 * 1.4   gm   29/11/16 Added preprocessor directives for sw footprint reduction
 *                     Removed XVphy_HdmiMmcmStart API
 *                     Corrected GTPE2 DRU REFCLK range
 * </pre>
 *
*******************************************************************************/
#if defined (XPAR_XV_HDMITX_0_DEVICE_ID) || defined (XPAR_XV_HDMIRX_0_DEVICE_ID)

#ifndef XVPHY_HDMI_H_
/* Prevent circular inclusions by using protection macros. */
#define XVPHY_HDMI_H_

/************************** Constant Definitions ******************************/

#define XVPHY_HDMI_GTHE4_DRU_LRATE		2500000000U
#define XVPHY_HDMI_GTHE4_DRU_REFCLK		156250000LL
#define XVPHY_HDMI_GTHE4_DRU_REFCLK_MIN	156240000LL
#define XVPHY_HDMI_GTHE4_DRU_REFCLK_MAX	156260000LL
#define XVPHY_HDMI_GTHE4_PLL_SCALE		1000
#define XVPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN	61250000LL
#define XVPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN	50000000LL
#define XVPHY_HDMI_GTHE4_CPLL_REFCLK_MIN	100000000LL
#define XVPHY_HDMI_GTHE4_TX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE4_TX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE4_TX_MMCM_FVCO_MAX	1200000000U
#define XVPHY_HDMI_GTHE4_RX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE4_RX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE4_RX_MMCM_FVCO_MAX	1200000000U

#define XVPHY_HDMI_GTHE3_DRU_LRATE		2500000000U
#define XVPHY_HDMI_GTHE3_DRU_REFCLK		156250000LL
#define XVPHY_HDMI_GTHE3_DRU_REFCLK_MIN	156240000LL
#define XVPHY_HDMI_GTHE3_DRU_REFCLK_MAX	156260000LL
#define XVPHY_HDMI_GTHE3_PLL_SCALE		1000
#define XVPHY_HDMI_GTHE3_QPLL0_REFCLK_MIN	61250000LL
#define XVPHY_HDMI_GTHE3_QPLL1_REFCLK_MIN	50000000LL
#define XVPHY_HDMI_GTHE3_CPLL_REFCLK_MIN	100000000LL
#define XVPHY_HDMI_GTHE3_TX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE3_TX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE3_TX_MMCM_FVCO_MAX	1200000000U
#define XVPHY_HDMI_GTHE3_RX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE3_RX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE3_RX_MMCM_FVCO_MAX	1200000000U

#define XVPHY_HDMI_GTHE2_DRU_LRATE		2500000000U
#define XVPHY_HDMI_GTHE2_DRU_REFCLK		125000000LL
#define XVPHY_HDMI_GTHE2_DRU_REFCLK_MIN	124990000LL
#define XVPHY_HDMI_GTHE2_DRU_REFCLK_MAX	125010000LL
#define XVPHY_HDMI_GTHE2_PLL_SCALE		1000
#define XVPHY_HDMI_GTHE2_QPLL_REFCLK_MIN	61250000LL
#define XVPHY_HDMI_GTHE2_CPLL_REFCLK_MIN	80000000LL
#define XVPHY_HDMI_GTHE2_TX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE2_TX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE2_TX_MMCM_FVCO_MAX	1200000000U
#define XVPHY_HDMI_GTHE2_RX_MMCM_SCALE		1
#define XVPHY_HDMI_GTHE2_RX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTHE2_RX_MMCM_FVCO_MAX	1200000000U

#define XVPHY_HDMI_GTXE2_DRU_LRATE		2000000000U
#define XVPHY_HDMI_GTXE2_DRU_REFCLK		125000000LL
#define XVPHY_HDMI_GTXE2_DRU_REFCLK_MIN	124990000LL
#define XVPHY_HDMI_GTXE2_DRU_REFCLK_MAX	125010000LL
#define XVPHY_HDMI_GTXE2_PLL_SCALE		1000
#define XVPHY_HDMI_GTXE2_QPLL_REFCLK_MIN	74125000LL
#define XVPHY_HDMI_GTXE2_CPLL_REFCLK_MIN	80000000LL
#define XVPHY_HDMI_GTXE2_TX_MMCM_SCALE		1
#define XVPHY_HDMI_GTXE2_TX_MMCM_FVCO_MIN	800000000U
#define XVPHY_HDMI_GTXE2_TX_MMCM_FVCO_MAX	1866000000U
#define XVPHY_HDMI_GTXE2_RX_MMCM_SCALE		1
#define XVPHY_HDMI_GTXE2_RX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTXE2_RX_MMCM_FVCO_MAX	1200000000U

#define XVPHY_HDMI_GTPE2_DRU_LRATE		2500000000U
#define XVPHY_HDMI_GTPE2_DRU_REFCLK		100000000LL
#define XVPHY_HDMI_GTPE2_DRU_REFCLK_MIN	 99990000LL
#define XVPHY_HDMI_GTPE2_DRU_REFCLK_MAX	100010000LL
#define XVPHY_HDMI_GTPE2_PLL_SCALE		1000
#define XVPHY_HDMI_GTPE2_QPLL_REFCLK_MIN	80000000LL
#define XVPHY_HDMI_GTPE2_CPLL_REFCLK_MIN	80000000LL
#define XVPHY_HDMI_GTPE2_TX_MMCM_SCALE		1
#define XVPHY_HDMI_GTPE2_TX_MMCM_FVCO_MIN	800000000U
#define XVPHY_HDMI_GTPE2_TX_MMCM_FVCO_MAX	1866000000U
#define XVPHY_HDMI_GTPE2_RX_MMCM_SCALE		1
#define XVPHY_HDMI_GTPE2_RX_MMCM_FVCO_MIN	600000000U
#define XVPHY_HDMI_GTPE2_RX_MMCM_FVCO_MAX	1200000000U

/**************************** Function Prototypes *****************************/

u32 XVphy_HdmiQpllParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir);
u32 XVphy_HdmiCpllParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir);
void XVphy_TxAlignReset(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Reset);
void XVphy_TxAlignStart(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Start);
void XVphy_ClkDetEnable(XVphy *InstancePtr, u8 Enable);
void XVphy_ClkDetTimerClear(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir);
void XVphy_ClkDetSetFreqLockThreshold(XVphy *InstancePtr, u16 ThresholdVal);
u8 XVphy_ClkDetCheckFreqZero(XVphy *InstancePtr, XVphy_DirectionType Dir);
void XVphy_ClkDetSetFreqTimeout(XVphy *InstancePtr, u32 TimeoutVal);
void XVphy_ClkDetTimerLoad(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, u32 TimeoutVal);
void XVphy_DruReset(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Reset);
void XVphy_DruEnable(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Enable);
u16 XVphy_DruGetVersion(XVphy *InstancePtr);
void XVphy_DruSetCenterFreqHz(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u64 CenterFreqHz);
void XVphy_DruSetGain(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 G1, u8 G1_P,
		u8 G2);
u64 XVphy_DruCalcCenterFreqHz(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId);
void XVphy_HdmiGtDruModeEnable(XVphy *InstancePtr, u8 Enable);
void XVphy_HdmiIntrHandlerCallbackInit(XVphy *InstancePtr);

#endif /* XVPHY_HDMI_H_ */
#endif
