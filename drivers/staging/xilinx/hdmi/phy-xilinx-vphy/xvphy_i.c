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
 * @file xvphy_i.c
 *
 * Contains generic APIs that are locally called or used within the
 * VPHY driver.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm,  11/09/16 Initial release.
 * 1.4   gm   29/11/16 Fixed c++ compiler warnings
 *                     Added xcvr adaptor functions for C++ compilations
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include <linux/string.h>
#include "xstatus.h"
#include "xvphy.h"
#include "xvphy_i.h"
#include "xvphy_hdmi.h"
#include <linux/delay.h>
#include "xvphy_gt.h"

/**************************** Function Prototypes *****************************/


/**************************** Function Definitions ****************************/

/*****************************************************************************/
/**
* This function will enable or disable the LPM logic in the Video PHY core.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	Enable will enable (if 1) or disable (if 0) the LPM logic.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_SetRxLpm(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Enable)
{
	u32 RegVal;
	u32 MaskVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;
	Dir = Dir;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
							XVPHY_RX_EQ_CDR_REG);

	if (ChId == XVPHY_CHANNEL_ID_CHA) {
		MaskVal = XVPHY_RX_CONTROL_RXLPMEN_ALL_MASK;
	}
	else {
		MaskVal = XVPHY_RX_CONTROL_RXLPMEN_MASK(ChId);
	}

	if (Enable) {
		RegVal |= MaskVal;
	}
	else {
		RegVal &= ~MaskVal;
	}
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_RX_EQ_CDR_REG,
									RegVal);
}

/*****************************************************************************/
/**
* This function will set the TX voltage swing value for a given channel.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Vs is the voltage swing value to write.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_SetTxVoltageSwing(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, u8 Vs)
{
	u32 RegVal;
	u32 MaskVal;
	u32 RegOffset;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if ((ChId == XVPHY_CHANNEL_ID_CH1) || (ChId == XVPHY_CHANNEL_ID_CH2)) {
		RegOffset = XVPHY_TX_DRIVER_CH12_REG;
	}
	else {
		RegOffset = XVPHY_TX_DRIVER_CH34_REG;
	}
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffset);

	MaskVal = XVPHY_TX_DRIVER_TXDIFFCTRL_MASK(ChId);
	RegVal &= ~MaskVal;
	RegVal |= (Vs << XVPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(ChId));
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);
}

/*****************************************************************************/
/**
* This function will set the TX pre-emphasis value for a given channel.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Pe is the pre-emphasis value to write.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_SetTxPreEmphasis(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u8 Pe)
{
	u32 RegVal;
	u32 MaskVal;
	u32 RegOffset;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if ((ChId == XVPHY_CHANNEL_ID_CH1) || (ChId == XVPHY_CHANNEL_ID_CH2)) {
		RegOffset = XVPHY_TX_DRIVER_CH12_REG;
	}
	else {
		RegOffset = XVPHY_TX_DRIVER_CH34_REG;
	}
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffset);

	MaskVal = XVPHY_TX_DRIVER_TXPRECURSOR_MASK(ChId);
	RegVal &= ~MaskVal;
	RegVal |= (Pe << XVPHY_TX_DRIVER_TXPRECURSOR_SHIFT(ChId));
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);
}

/*****************************************************************************/
/**
* This function writes the current software configuration for the reference
* clock selections to hardware for the specified quad on all channels.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
*
* @return
*		- XST_SUCCESS.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_WriteCfgRefClkSelReg(XVphy *InstancePtr, u8 QuadId)
{
	u32 RegVal = 0;
	XVphy_Channel *ChPtr;
	XVphy_GtType GtType = InstancePtr->Config.XcvrType;

	/* Point to the first channel since settings apply to all channels. */
	ChPtr = &InstancePtr->Quads[QuadId].Ch1;

	/* PllRefClkSel. */
	/* - QPLL0. */
	RegVal &= ~XVPHY_REF_CLK_SEL_QPLL0_MASK;
	RegVal = InstancePtr->Quads[QuadId].Cmn0.PllRefClkSel;
	/* - CPLL. */
	RegVal &= ~XVPHY_REF_CLK_SEL_CPLL_MASK;
	RegVal |= (ChPtr->CpllRefClkSel << XVPHY_REF_CLK_SEL_CPLL_SHIFT);
	if ((GtType == XVPHY_GT_TYPE_GTHE3) ||
            (GtType == XVPHY_GT_TYPE_GTHE4) ||
            (GtType == XVPHY_GT_TYPE_GTPE2)) {
		/* - QPLL1. */
		RegVal &= ~XVPHY_REF_CLK_SEL_QPLL1_MASK;
		RegVal |= (InstancePtr->Quads[QuadId].Cmn1.PllRefClkSel <<
				XVPHY_REF_CLK_SEL_QPLL1_SHIFT);
	}

	/* SysClkDataSel. PLLCLKSEL */
	RegVal &= ~XVPHY_REF_CLK_SEL_SYSCLKSEL_MASK;
	/* - TXSYSCLKSEL[0]. TXPLLCLKSEL*/
	RegVal |= (ChPtr->TxDataRefClkSel <<
		XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_SHIFT(GtType)) &
		XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_MASK(GtType);
	/* - RXSYSCLKSEL[0]. RXPLLCLKSEL*/
	RegVal |= (ChPtr->RxDataRefClkSel <<
		XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_SHIFT(GtType)) &
		XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_MASK(GtType);

	/* SysClkOutSel. */
	/* - TXSYSCLKSEL[1]. */
	RegVal |= (ChPtr->TxOutRefClkSel <<
		XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_SHIFT(GtType)) &
		XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_MASK(GtType);
	/* - RXSYSCLKSEL[1]. */
	RegVal |= (ChPtr->RxOutRefClkSel <<
		XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_SHIFT(GtType)) &
		XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_MASK(GtType);

	/* Write to hardware. */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_REF_CLK_SEL_REG,
			RegVal);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* Configure the PLL reference clock selection for the specified channel(s).
* This is applied to both direction to the software configuration only.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	SysClkDataSel is the reference clock selection to configure.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_CfgPllRefClkSel(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_PllRefClkSelType RefClkSel)
{
	u8 Id, Id0, Id1;

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].PllRefClkSel =
					RefClkSel;
	}
}

/*****************************************************************************/
/**
* Configure the SYSCLKDATA reference clock selection for the direction. Same
* configuration applies to all channels in the quad. This is applied to the
* software configuration only.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	SysClkDataSel is the reference clock selection to configure.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_CfgSysClkDataSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_SysClkDataSelType SysClkDataSel)
{
	XVphy_Channel *ChPtr;
	u8 Id, Id0, Id1;

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	/* Select in software - same for all channels. */
	for (Id = Id0; Id <= Id1; Id++) {
		ChPtr = &InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)];
		ChPtr->DataRefClkSel[Dir] = SysClkDataSel;
	}
}

/*****************************************************************************/
/**
* Configure the SYSCLKOUT reference clock selection for the direction. Same
* configuration applies to all channels in the quad. This is applied to the
* software configuration only.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	SysClkOutSel is the reference clock selection to configure.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_CfgSysClkOutSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_SysClkOutSelType SysClkOutSel)
{
	XVphy_Channel *ChPtr;
	u8 Id, Id0, Id1;

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	/* Select in software - same for all channels. */
	for (Id = Id0; Id <= Id1; Id++) {
		ChPtr = &InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)];
		ChPtr->OutRefClkSel[Dir] = SysClkOutSel;
	}
}

/*****************************************************************************/
/**
* Obtain the reconfiguration channel ID for given PLL type
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	PllType is the PLL type being used by the channel.
*
* @return	The Channel ID to be used for reconfiguration
*
* @note		None.
*
******************************************************************************/
XVphy_ChannelId XVphy_GetRcfgChId(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_PllType PllType)
{
	XVphy_ChannelId ChId;

	/* Suppress Warning Messages */
	InstancePtr = InstancePtr;
	QuadId = QuadId;
	Dir = Dir;

	/* Determine which channel(s) to operate on. */
	switch (PllType) {
	case XVPHY_PLL_TYPE_QPLL:
	case XVPHY_PLL_TYPE_QPLL0:
	case XVPHY_PLL_TYPE_PLL0:
		ChId = XVPHY_CHANNEL_ID_CMN0;
		break;
	case XVPHY_PLL_TYPE_QPLL1:
	case XVPHY_PLL_TYPE_PLL1:
		ChId = XVPHY_CHANNEL_ID_CMN1;
		break;
	default:
		ChId = XVPHY_CHANNEL_ID_CHA;
		break;
	}

	return ChId;
}

/*****************************************************************************/
/**
* Obtain the current reference clock frequency for the quad based on the
* reference clock type.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	RefClkType is the type to obtain the clock selection for.
*
* @return	The current reference clock frequency for the quad for the
*		specified type selection.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_GetQuadRefClkFreq(XVphy *InstancePtr, u8 QuadId,
		XVphy_PllRefClkSelType RefClkType)
{
	u32 FreqHz;

	u8 RefClkIndex = RefClkType - XVPHY_PLL_REFCLKSEL_TYPE_GTREFCLK0;

	FreqHz = (RefClkType > XVPHY_PLL_REFCLKSEL_TYPE_GTGREFCLK) ? 0 :
		InstancePtr->Quads[QuadId].RefClkHz[RefClkIndex];

	return FreqHz;
}

/*****************************************************************************/
/**
* Obtain the current [RT]XSYSCLKSEL[0] configuration.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	ChId is the channel ID which to operate on.
*
* @return	The current [RT]XSYSCLKSEL[0] selection.
*
* @note		None.
*
******************************************************************************/
XVphy_SysClkDataSelType XVphy_GetSysClkDataSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_ChannelId ChId)
{
	u32 Sel;
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;
	ChId = ChId;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
							XVPHY_REF_CLK_SEL_REG);

	if (Dir == XVPHY_DIR_TX) {
		/* Synchronize software configuration to hardware. */
		Sel = RegVal & XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_MASK(
				InstancePtr->Config.XcvrType);
		Sel >>= XVPHY_REF_CLK_SEL_TXSYSCLKSEL_DATA_SHIFT(
				InstancePtr->Config.XcvrType);
	}
	else {
		/* Synchronize software configuration to hardware. */
		Sel = RegVal & XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_MASK(
				InstancePtr->Config.XcvrType);
		Sel >>= XVPHY_REF_CLK_SEL_RXSYSCLKSEL_DATA_SHIFT(
				InstancePtr->Config.XcvrType);
	}

	return (XVphy_SysClkDataSelType) Sel;
}

/*****************************************************************************/
/**
* Obtain the current [RT]XSYSCLKSEL[1] configuration.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	ChId is the channel ID which to operate on.
*
* @return	The current [RT]XSYSCLKSEL[1] selection.
*
* @note		None.
*
******************************************************************************/
XVphy_SysClkOutSelType XVphy_GetSysClkOutSel(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, XVphy_ChannelId ChId)
{
	u32 Sel;
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;
	ChId = ChId;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_REF_CLK_SEL_REG);

	if (Dir == XVPHY_DIR_TX) {
		/* Synchronize software configuration to hardware. */
		Sel = RegVal & XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_MASK(
				InstancePtr->Config.XcvrType);
		Sel >>= XVPHY_REF_CLK_SEL_TXSYSCLKSEL_OUT_SHIFT(
				InstancePtr->Config.XcvrType);
	}
	else {
		/* Synchronize software configuration to hardware. */
		Sel = RegVal & XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_MASK(
				InstancePtr->Config.XcvrType);
		Sel >>= XVPHY_REF_CLK_SEL_RXSYSCLKSEL_OUT_SHIFT(
				InstancePtr->Config.XcvrType);
	}

	return (XVphy_SysClkOutSelType)Sel;
}

/*****************************************************************************/
/**
* This function will check the status of a PLL lock on the specified channel.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID which to operate on.
*
* @return
*		- XST_SUCCESS if the specified PLL is locked.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_IsPllLocked(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	u32 RegVal;
	u32 MaskVal;
	XVphy_PllType TxPllType;
	XVphy_PllType RxPllType;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (ChId == XVPHY_CHANNEL_ID_CMN0) {
		MaskVal = XVPHY_PLL_LOCK_STATUS_QPLL0_MASK;
	}
	else if (ChId == XVPHY_CHANNEL_ID_CMN1) {
		MaskVal = XVPHY_PLL_LOCK_STATUS_QPLL1_MASK;
	}
	else if (ChId == XVPHY_CHANNEL_ID_CMNA) {
		MaskVal = XVPHY_PLL_LOCK_STATUS_QPLL0_MASK |
			  XVPHY_PLL_LOCK_STATUS_QPLL1_MASK;
	}
	else if (ChId == XVPHY_CHANNEL_ID_CHA) {
		TxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
				XVPHY_CHANNEL_ID_CH1);
		RxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
				XVPHY_CHANNEL_ID_CH1);
		if (RxPllType == XVPHY_PLL_TYPE_CPLL &&
				InstancePtr->Config.RxProtocol == XVPHY_PROTOCOL_HDMI) {
			MaskVal = XVPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK;
		}
		else if (TxPllType == XVPHY_PLL_TYPE_CPLL &&
				InstancePtr->Config.TxProtocol == XVPHY_PROTOCOL_HDMI) {
			MaskVal = XVPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK;
		}
		else {
			MaskVal = XVPHY_PLL_LOCK_STATUS_CPLL_ALL_MASK;
		}
	}
	else {
		MaskVal = XVPHY_PLL_LOCK_STATUS_CPLL_MASK(ChId);
	}
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_PLL_LOCK_STATUS_REG);

	if ((RegVal & MaskVal) == MaskVal) {
		return XST_SUCCESS;
	}

	return XST_FAILURE;
}

/*****************************************************************************/
/**
* This function will reset and enable the Video PHY's user core logic.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID which to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	Hold is an indicator whether to "hold" the reset if set to 1.
*		If set to 0: reset, then enable.
*
* @return
*		- XST_SUCCESS.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_GtUserRdyEnable(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u8 Hold)
{
	u32 RegVal;
	u32 MaskVal;
	u32 RegOffset;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (Dir == XVPHY_DIR_TX) {
		RegOffset = XVPHY_TX_INIT_REG;

		if (ChId == XVPHY_CHANNEL_ID_CHA) {
			MaskVal = XVPHY_TX_INIT_USERRDY_ALL_MASK;
		}
		else {
			MaskVal = XVPHY_TX_INIT_USERRDY_MASK(ChId);
		}
	}
	else {
		RegOffset = XVPHY_RX_INIT_REG;
		if (ChId == XVPHY_CHANNEL_ID_CHA) {
			MaskVal = XVPHY_RX_INIT_USERRDY_ALL_MASK;
		}
		else {
			MaskVal = XVPHY_RX_INIT_USERRDY_MASK(ChId);
		}
	}

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffset);
	/* Assert reset. */
	RegVal |= MaskVal;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);

	if (!Hold) {
		/* De-assert reset. */
		RegVal &= ~MaskVal;
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function will reset the mixed-mode clock manager (MMCM) core.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	Hold is an indicator whether to "hold" the reset if set to 1.
*		If set to 0: reset, then enable.
*
* @return
*		- XST_SUCCESS.
*
* @note		None.
*
******************************************************************************/
void XVphy_MmcmReset(XVphy *InstancePtr, u8 QuadId, XVphy_DirectionType Dir,
		u8 Hold)
{
	u32 RegOffsetCtrl;
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (Dir == XVPHY_DIR_TX) {
		RegOffsetCtrl = XVPHY_MMCM_TXUSRCLK_CTRL_REG;
	}
	else {
		RegOffsetCtrl = XVPHY_MMCM_RXUSRCLK_CTRL_REG;
	}

	/* Assert reset. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl);
	RegVal |= XVPHY_MMCM_USRCLK_CTRL_RST_MASK;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl, RegVal);

	if (!Hold) {
		/* De-assert reset. */
		RegVal &= ~XVPHY_MMCM_USRCLK_CTRL_RST_MASK;
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl,
									RegVal);
	}
}

/*****************************************************************************/
/**
* This function will reset the mixed-mode clock manager (MMCM) core.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
* @param	Enable is an indicator whether to "Enable" the locked mask
*		if set to 1. If set to 0: reset, then disable.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_MmcmLockedMaskEnable(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, u8 Enable)
{
	u32 RegOffsetCtrl;
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (Dir == XVPHY_DIR_TX) {
		RegOffsetCtrl = XVPHY_MMCM_TXUSRCLK_CTRL_REG;
	}
	else {
		RegOffsetCtrl = XVPHY_MMCM_RXUSRCLK_CTRL_REG;
	}

	/* Assert reset. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl);
	RegVal |= XVPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl, RegVal);

	if (!Enable) {
		/* De-assert reset. */
		RegVal &= ~XVPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK;
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffsetCtrl,
									RegVal);
	}
}

/*****************************************************************************/
/**
* This function obtains the divider value of the BUFG_GT peripheral.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Dir is an indicator for TX or RX
* @param	Div 3-bit divider value
*
* @return	None.
*
******************************************************************************/
void XVphy_SetBufgGtDiv(XVphy *InstancePtr, XVphy_DirectionType Dir, u8 Div)
{
	u32 RegVal;
	u32 RegOffset;
	u8 Divider = Div;

	if (Divider == 0) {
		Divider = 1;
	}
	else {
		Divider = Divider - 1;
	}


	if (Dir == XVPHY_DIR_TX) {
		RegOffset = XVPHY_BUFGGT_TXUSRCLK_REG;
	}
	else {
		RegOffset = XVPHY_BUFGGT_RXUSRCLK_REG;
	}

	/* Read BUFG_GT register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffset);
	RegVal &= ~XVPHY_BUFGGT_XXUSRCLK_DIV_MASK;

	/* Shift divider value to correct position. */
	Divider <<= XVPHY_BUFGGT_XXUSRCLK_DIV_SHIFT;
	Divider &= XVPHY_BUFGGT_XXUSRCLK_DIV_MASK;
	RegVal |= Divider;

	/* Write new value to BUFG_GT ctrl register. */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);
}

/*****************************************************************************/
/**
* This function will power down the specified GT PLL.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to power down the PLL for.
* @param	Dir is an indicator for TX or RX.
* @param	Hold is an indicator whether to "hold" the power down if set
*		to 1. If set to 0: power down, then power back up.
*
* @return
*		- XST_SUCCESS.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_PowerDownGtPll(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		u8 Hold)
{
	u32 MaskVal = 0;
	u32 RegVal;
	u8 Id, Id0, Id1;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (XVPHY_ISCH(ChId)) {
		XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	}
	else {
		/* When powering down a QPLL, power down for all channels. */
		XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	}
	for (Id = Id0; Id <= Id1; Id++) {
		if (ChId == XVPHY_CHANNEL_ID_CMN0) {
			MaskVal |= XVPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(Id);
		}
		else if (ChId == XVPHY_CHANNEL_ID_CMN1) {
			MaskVal |= XVPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(Id);
		}
		else if (ChId == XVPHY_CHANNEL_ID_CMNA) {
			MaskVal |= XVPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(Id) |
				   XVPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(Id);
		}
		else {
			MaskVal |= XVPHY_POWERDOWN_CONTROL_CPLLPD_MASK(Id);
		}
	}

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
					XVPHY_POWERDOWN_CONTROL_REG);
	RegVal |= MaskVal;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr,
					XVPHY_POWERDOWN_CONTROL_REG, RegVal);

	if (!Hold) {
		RegVal &= ~MaskVal;
		XVphy_WriteReg(InstancePtr->Config.BaseAddr,
					XVPHY_POWERDOWN_CONTROL_REG, RegVal);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function will try to find the necessary PLL divisor values to produce
* the configured line rate given the specified PLL input frequency. This will
* be done for all channels specified by ChId.
* This function is a wrapper for XVphy_PllCalculator.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to calculate the PLL values for.
* @param	ChId is the channel ID to calculate the PLL values for.
* @param	Dir is an indicator for TX or RX.
* @param	PllClkInFreqHz is the PLL input frequency on which to base the
*		calculations on. A value of 0 indicates to use the currently
*		configured quad PLL reference clock. A non-zero value indicates
*		to ignore what is currently configured in SW, and use a custom
*		frequency instead.
*
* @return
*		- XST_SUCCESS if valid PLL values were found to satisfy the
*		  constraints.
*		- XST_FAILURE otherwise.
*
* @note		If successful, the channel's PllParams structure will be
*		modified with the valid PLL parameters.
*
******************************************************************************/
u32 XVphy_ClkCalcParams(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir, u32 PllClkInFreqHz)
{
	u32 Status = XST_SUCCESS;
	u8 Id, Id0, Id1;

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		Status = XVphy_PllCalculator(InstancePtr, QuadId,
					(XVphy_ChannelId)Id, Dir, PllClkInFreqHz);
		if (Status != XST_SUCCESS) {
			return Status;
		}
	}

	return Status;
}

/*****************************************************************************/
/**
* This function will set the current output divider configuration over DRP.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID for which to write the settings for.
* @param	Dir is an indicator for RX or TX.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_OutDivReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir)
{
	u32 Status;
	u8 Id;
	u8 Id0;
	u8 Id1;

	if (!XVPHY_ISCH(ChId)) {
		ChId = XVPHY_CHANNEL_ID_CHA;
	}

	XVphy_LogWrite(InstancePtr, (Dir == XVPHY_DIR_TX) ?
		XVPHY_LOG_EVT_GT_TX_RECONFIG : XVPHY_LOG_EVT_GT_RX_RECONFIG, 0);

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		Status = XVphy_OutDivChReconfig(InstancePtr, QuadId,
					(XVphy_ChannelId)Id, Dir);
		if (Status != XST_SUCCESS) {
			break;
		}
	}

	return Status;
}

/*****************************************************************************/
/**
* This function will set the current RX/TX configuration over DRP.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID for which to write the settings for.
* @param	Dir is an indicator for RX or TX.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_DirReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir)
{
	u32 Status = XST_SUCCESS;
	u8 Id, Id0, Id1;

	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE2) &&
			(Dir == XVPHY_DIR_TX)) {
		return XST_SUCCESS;
	}

    if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) &&
		((InstancePtr->Config.TxProtocol == XVPHY_PROTOCOL_DP) ||
		 (InstancePtr->Config.RxProtocol == XVPHY_PROTOCOL_DP))) {
               ChId = XVPHY_CHANNEL_ID_CHA;
    }

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		if (Dir == XVPHY_DIR_TX) {
			Status = XVphy_TxChReconfig(InstancePtr, QuadId,
											(XVphy_ChannelId)Id);
		}
		else {
			Status = XVphy_RxChReconfig(InstancePtr, QuadId,
											(XVphy_ChannelId)Id);
		}
		if (Status != XST_SUCCESS) {
			break;
		}
	}

	XVphy_LogWrite(InstancePtr, (Dir == XVPHY_DIR_TX) ?
		XVPHY_LOG_EVT_GT_TX_RECONFIG : XVPHY_LOG_EVT_GT_RX_RECONFIG, 1);

	return Status;
}

/*****************************************************************************/
/**
* This function will set the current clocking settings for each channel to
* hardware based on the configuration stored in the driver's instance.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID for which to write the settings for.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_ClkReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	u32 Status;
	u8 Id;
	u8 Id0;
	u8 Id1;

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		if (XVPHY_ISCH(Id)) {
			Status = XVphy_ClkChReconfig(InstancePtr, QuadId,
											(XVphy_ChannelId)Id);
		}
		else if (XVPHY_ISCMN(ChId)) {
			Status = XVphy_ClkCmnReconfig(InstancePtr, QuadId,
											(XVphy_ChannelId)Id);
		}
		if (Status != XST_SUCCESS) {
			return Status;
		}
	}

	if (XVPHY_ISCH(Id)) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_CPLL_RECONFIG, 1);
	}
	else if (XVPHY_ISCMN(ChId) &&
			(InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2)) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_QPLL_RECONFIG, 1);
	}
	else if (XVPHY_ISCMN(ChId)) { /* GTPE2. */
		XVphy_LogWrite(InstancePtr, (ChId == XVPHY_CHANNEL_ID_CMN0) ?
			XVPHY_LOG_EVT_PLL0_RECONFIG :
			XVPHY_LOG_EVT_PLL1_RECONFIG, 1);
	}

	return Status;
}

/*****************************************************************************/
/**
* This function will set the channel IDs to correspond with the supplied
* channel ID based on the protocol. HDMI uses 3 channels; DP uses 4. This ID
* translation is done to allow other functions to operate iteratively over
* multiple channels.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID used to determine the indices.
* @param	Id0 is a pointer to the start channel ID to set.
* @param	Id1 is a pointer to the end channel ID to set.
*
* @return	None.
*
* @note		The contents of Id0 and Id1 will be set according to ChId.
*
******************************************************************************/
void XVphy_Ch2Ids(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u8 *Id0, u8 *Id1)
{
	u8 Channels = 4;

	if (ChId == XVPHY_CHANNEL_ID_CHA) {
		*Id0 = XVPHY_CHANNEL_ID_CH1;
		if ((InstancePtr->Config.TxProtocol == XVPHY_PROTOCOL_HDMI) ||
			(InstancePtr->Config.RxProtocol == XVPHY_PROTOCOL_HDMI)) {
			*Id1 = XVPHY_CHANNEL_ID_CH3;
		}
		else {
			Channels = ((InstancePtr->Config.TxChannels >=
							InstancePtr->Config.RxChannels) ?
									InstancePtr->Config.TxChannels :
									InstancePtr->Config.RxChannels);

			if (Channels == 1) {
				*Id1 = XVPHY_CHANNEL_ID_CH1;
			}
			else if (Channels == 2) {
				*Id1 = XVPHY_CHANNEL_ID_CH2;
			}
			else if (Channels == 3) {
				*Id1 = XVPHY_CHANNEL_ID_CH3;
			}
			else {
				*Id1 = XVPHY_CHANNEL_ID_CH4;
			}
		}
	}
	else if (ChId == XVPHY_CHANNEL_ID_CMNA) {
		*Id0 = XVPHY_CHANNEL_ID_CMN0;
		if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) ||
		    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE4)) {
			*Id1 = XVPHY_CHANNEL_ID_CMN1;
		}
		else {
			*Id1 = XVPHY_CHANNEL_ID_CMN0;
		}
	}
	else {
		*Id0 = *Id1 = ChId;
	}
}

/*****************************************************************************/
/**
* This function will translate from XVphy_PllType to XVphy_SysClkDataSelType.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	The reference clock type based on the PLL selection.
*
* @note		None.
*
******************************************************************************/
XVphy_SysClkDataSelType Pll2SysClkData(XVphy_PllType PllSelect)
{
	return	(PllSelect == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_SYSCLKSELDATA_TYPE_CPLL_OUTCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL) ?
			XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL0) ?
			XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL1) ?
			XVPHY_SYSCLKSELDATA_TYPE_QPLL1_OUTCLK :
		(PllSelect == XVPHY_PLL_TYPE_PLL0) ?
			XVPHY_SYSCLKSELDATA_TYPE_PLL0_OUTCLK :
		XVPHY_SYSCLKSELDATA_TYPE_PLL1_OUTCLK;
}

/*****************************************************************************/
/**
* This function will translate from XVphy_PllType to XVphy_SysClkOutSelType.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	The reference clock type based on the PLL selection.
*
* @note		None.
*
******************************************************************************/
XVphy_SysClkOutSelType Pll2SysClkOut(XVphy_PllType PllSelect)
{
	return	(PllSelect == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_SYSCLKSELOUT_TYPE_CPLL_REFCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL) ?
			XVPHY_SYSCLKSELOUT_TYPE_QPLL_REFCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL0) ?
			XVPHY_SYSCLKSELOUT_TYPE_QPLL0_REFCLK :
		(PllSelect == XVPHY_PLL_TYPE_QPLL1) ?
			XVPHY_SYSCLKSELOUT_TYPE_QPLL1_REFCLK :
		(PllSelect == XVPHY_PLL_TYPE_PLL0) ?
			XVPHY_SYSCLKSELOUT_TYPE_PLL0_REFCLK :
		XVPHY_SYSCLKSELOUT_TYPE_PLL1_REFCLK;
}

/*****************************************************************************/
/**
* This function will try to find the necessary PLL divisor values to produce
* the configured line rate given the specified PLL input frequency.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to calculate the PLL values for.
* @param	ChId is the channel ID to calculate the PLL values for.
* @param	Dir is an indicator for TX or RX.
* @param	PllClkInFreqHz is the PLL input frequency on which to base the
*		calculations on. A value of 0 indicates to use the currently
*		configured quad PLL reference clock. A non-zero value indicates
*		to ignore what is currently configured in SW, and use a custom
*		frequency instead.
*
* @return
*		- XST_SUCCESS if valid PLL values were found to satisfy the
*		  constraints.
*		- XST_FAILURE otherwise.
*
* @note		If successful, the channel's PllParams structure will be
*		modified with the valid PLL parameters.
*
******************************************************************************/
u32 XVphy_PllCalculator(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir,
		u32 PllClkInFreqHz)
{
	u32 Status;
	u64 PllClkOutFreqHz;
	u64 CalcLineRateFreqHz;
	u8 Id, Id0, Id1;
	u64 PllClkInFreqHzIn = PllClkInFreqHz;
	XVphy_Channel *PllPtr = &InstancePtr->Quads[QuadId].
		Plls[XVPHY_CH2IDX(ChId)];

	if (!PllClkInFreqHzIn) {
		PllClkInFreqHzIn = XVphy_GetQuadRefClkFreq(InstancePtr, QuadId,
					PllPtr->PllRefClkSel);
	}

	/* Select PLL value table offsets. */
	const XVphy_GtPllDivs *GtPllDivs;
	if (XVPHY_ISCH(ChId)) {
		GtPllDivs = &InstancePtr->GtAdaptor->CpllDivs;
	}
	else {
		GtPllDivs = &InstancePtr->GtAdaptor->QpllDivs;
	}

	const u8 *M, *N1, *N2, *D;
	for (N2 = GtPllDivs->N2; *N2 != 0; N2++) {
	for (N1 = GtPllDivs->N1; *N1 != 0; N1++) {
	for (M = GtPllDivs->M;   *M != 0;  M++) {
		PllClkOutFreqHz = (PllClkInFreqHzIn * *N1 * *N2) / *M;

		/* Test if the calculated PLL clock is in the VCO range. */
		Status = XVphy_CheckPllOpRange(InstancePtr, QuadId, ChId,
				PllClkOutFreqHz);
		if (Status != XST_SUCCESS) {
			continue;
		}

		if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) ||
				(XVPHY_ISCH(ChId))) {
			PllClkOutFreqHz *= 2;
		}
		/* Apply TX/RX divisor. */
		for (D = GtPllDivs->D; *D != 0; D++) {
			CalcLineRateFreqHz = PllClkOutFreqHz / *D;
			if (CalcLineRateFreqHz == PllPtr->LineRateHz) {
				goto calc_done;
			}
		}
	}
	}
	}
	/* Calculation failed, don't change divisor settings. */
	return XST_FAILURE;

calc_done:
	/* Found the multiplier and divisor values for requested line rate. */
	PllPtr->PllParams.MRefClkDiv = *M;
	PllPtr->PllParams.NFbDiv = *N1;
	PllPtr->PllParams.N2FbDiv = *N2; /* Won't be used for QPLL.*/
	PllPtr->PllParams.IsLowerBand = 1; /* Won't be used for CPLL. */

	if (XVPHY_ISCMN(ChId)) {
		/* Same divisor value for all channels if using a QPLL. */
		ChId = XVPHY_CHANNEL_ID_CHA;
	}

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].OutDiv[Dir] =
			*D;
		if (Dir == XVPHY_DIR_RX) {
			XVphy_CfgSetCdr(InstancePtr, QuadId, (XVphy_ChannelId)Id);
		}
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function calculates the PLL VCO operating frequency.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for TX or RX.
*
* @return	PLL VCO frequency in Hz
*
* @note		None.
*
******************************************************************************/
u64 XVphy_GetPllVcoFreqHz(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir)
{
	u64 PllxVcoRateHz;
	u64 PllRefClkHz;
	XVphy_Channel *PllPtr = &InstancePtr->Quads[QuadId].
                    Plls[XVPHY_CH2IDX(ChId)];

	if (Dir == XVPHY_DIR_TX) {
		if (InstancePtr->Config.TxProtocol == XVPHY_PROTOCOL_HDMI) {
			PllRefClkHz = InstancePtr->HdmiTxRefClkHz;
		}
		else {
			PllRefClkHz = XVphy_GetQuadRefClkFreq(InstancePtr, QuadId,
									PllPtr->PllRefClkSel);
		}
	}
	else {
		if (InstancePtr->Config.RxProtocol == XVPHY_PROTOCOL_HDMI) {
#if defined (XPAR_XV_HDMITX_0_DEVICE_ID) || defined (XPAR_XV_HDMIRX_0_DEVICE_ID)
			if (InstancePtr->HdmiRxDruIsEnabled) {
				PllRefClkHz = XVphy_DruGetRefClkFreqHz(InstancePtr);
			}
			else {
				PllRefClkHz = InstancePtr->HdmiRxRefClkHz;
			}
#else
			PllRefClkHz = 0;
#endif
		}
		else {
			PllRefClkHz = XVphy_GetQuadRefClkFreq(InstancePtr, QuadId,
									PllPtr->PllRefClkSel);
		}
	}

	PllxVcoRateHz = (PllRefClkHz *
				InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)].
								PllParams.N1FbDiv *
				InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)].
								PllParams.N2FbDiv) /
				InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)].
								PllParams.MRefClkDiv;

	return PllxVcoRateHz;
}

#ifdef __cplusplus
/*****************************************************************************/
/**
* This function is a transceiver adaptor to set the clock and data recovery
* (CDR) values for a given channel.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_CfgSetCdr(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	return InstancePtr->GtAdaptor->CfgSetCdr(InstancePtr, QuadId, ChId);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to check if a given PLL output
* frequency is within the operating range of the PLL for the GT type.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	PllClkOutFreqHz is the frequency to check.
*
* @return
*		- XST_SUCCESS if the frequency resides within the PLL's range.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_CheckPllOpRange(XVphy *InstancePtr, u8 QuadId,
							XVphy_ChannelId ChId, u64 PllClkOutFreqHz)
{
	return InstancePtr->GtAdaptor->CheckPllOpRange(InstancePtr, QuadId, ChId,
							PllClkOutFreqHz);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to set the output divider logic for
* a given channel.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Dir is an indicator for RX or TX.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_OutDivChReconfig(XVphy *InstancePtr, u8 QuadId,
							XVphy_ChannelId ChId, XVphy_DirectionType Dir)
{
	return InstancePtr->GtAdaptor->OutDivChReconfig(InstancePtr, QuadId,
							ChId, Dir);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to configure the channel
* clock settings.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_ClkChReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	return InstancePtr->GtAdaptor->ClkChReconfig(InstancePtr, QuadId, ChId);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to configure the common channel
* clock settings.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	CmnId is the common channel ID to operate on.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_ClkCmnReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	return InstancePtr->GtAdaptor->ClkCmnReconfig(InstancePtr, QuadId, ChId);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to configure the channel's
* RX settings.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_RxChReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	return InstancePtr->GtAdaptor->RxChReconfig(InstancePtr, QuadId, ChId);
}

/*****************************************************************************/
/**
* This function is a transceiver adaptor to configure the channel's
* TX settings.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- XST_SUCCESS if the configuration was successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_TxChReconfig(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	return InstancePtr->GtAdaptor->TxChReconfig(InstancePtr, QuadId, ChId);
}
#endif
