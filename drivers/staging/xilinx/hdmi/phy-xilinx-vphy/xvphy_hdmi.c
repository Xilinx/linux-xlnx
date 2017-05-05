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
 * @file xvphy_hdmi.c
 *
 * This file contains video PHY functionality specific to the HDMI protocol.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm   10/19/15 Initial release.
 * 1.1   gm   02/01/16 Added GTPE2 and GTHE4 support.
 *       MG   03/08/16 Fixed issue in function XVphy_HdmiCfgCalcMmcmParam
 *                       for single pixel calculation.
 * 1.2   gm            Added XVphy_HdmiMmcmStart and
 *                       XVphy_HdmiMmcmWriteParameters functions
 *                     Replaced xil_printf with log events
 *                     Modified XVphy_DruGetRefClkFreqHz
 *                     Suppressed warning messages due to unused arguments
 * 1.3   gm   01/11/16 Added error message in XVphy_HdmiCpllParam when DRU is
 *                     enabled and RX TMDS ratio is 1/40
 *                     Fixed rounding of DRU refclk frequency
 *                     Fixed a bug in XVphy_SetHdmiRxParam to update the ChId
 *                        when DRU is enabled
 * 1.4   gm   29/11/16 Added preprocessor directives for sw footprint reduction
 *                     Added TX datawidth dynamic reconfiguration
 *                     Incorporated AXIlite Freq auto extraction
 *                     Added extra routine for 2/4 byte tranceiver modes
 *                     Added logging and register access for ERR_IRQ impl
 *                     Removed XVphy_HdmiMmcmStart API
 *                     Fixed c++ compiler warnings
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#if defined (XPAR_XV_HDMITX_0_DEVICE_ID) || defined (XPAR_XV_HDMIRX_0_DEVICE_ID)
#include "xstatus.h"
#include "xvphy.h"
#include "xvphy_i.h"
#include "xvphy_hdmi.h"

/****************************** Type Definitions ******************************/

typedef struct {
	u64 DruLineRate;
	u16 PllScale;
	u32 Qpll0RefClkMin;
	u32 Qpll1RefClkMin;
	u32 CpllRefClkMin;
	u16 TxMmcmScale;
	u32 TxMmcmFvcoMin;
	u32 TxMmcmFvcoMax;
	u16 RxMmcmScale;
	u32 RxMmcmFvcoMin;
	u32 RxMmcmFvcoMax;
} XVphy_GtHdmiChars;

/**************************** Function Prototypes *****************************/

extern void XVphy_Ch2Ids(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u8 *Id0, u8 *Id1);
static const XVphy_GtHdmiChars *GetGtHdmiPtr(XVphy *InstancePtr);
static void XVphy_HdmiSetSystemClockSelection(XVphy *InstancePtr, u8 QuadId);

/**************************** Function Definitions ****************************/

/******************************************************************************/
/**
 * This function initializes the Video PHY for HDMI.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 * @param	CfgPtr is a pointer to the configuration structure that will
 *		be used to copy the settings from.
 * @param	SystemFrequency is the system frequency for the HDMI logic
 *		to be based on.
 *
 * @return	None.
 *
 * @note	None.
 *
*******************************************************************************/
u32 XVphy_HdmiInitialize(XVphy *InstancePtr, u8 QuadId, XVphy_Config *CfgPtr,
		u32 SystemFrequency)
{
	u8 Id, Id0, Id1;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(CfgPtr != NULL);

	/* Init done. */
	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_INIT, 0);

	/* Setup the instance. */
	XVphy_CfgInitialize(InstancePtr, CfgPtr, CfgPtr->BaseAddr);

	/* Set default. */
	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].TxState =
			XVPHY_GT_STATE_IDLE;
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].RxState =
			XVPHY_GT_STATE_IDLE;
		//Initialize Transceiver Width values
		if (InstancePtr->Config.TransceiverWidth == 2) {
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				TxDataWidth = 20;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				TxIntDataWidth = 2;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				RxDataWidth = 20;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				RxIntDataWidth = 2;
		}
		else {
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				TxDataWidth = 40;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				TxIntDataWidth = 4;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				RxDataWidth = 40;
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				RxIntDataWidth = 4;
		}
	}
	/* Interrupt Disable. */
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT);
	XVphy_IntrDisable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);

	/* Setup HDMI interrupt handler callback*/
	XVphy_HdmiIntrHandlerCallbackInit(InstancePtr);

	/* Configure clock detector. */
	XVphy_ClkDetEnable(InstancePtr, FALSE);
	XVphy_ClkDetSetFreqTimeout(InstancePtr, SystemFrequency);
	XVphy_ClkDetSetFreqLockThreshold(InstancePtr, 40);

	/* Start capturing logs. */
	XVphy_LogReset(InstancePtr);
	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_INIT, 0);

	XVphy_HdmiSetSystemClockSelection(InstancePtr, QuadId);

	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE4)) {
		XVphy_SetBufgGtDiv(InstancePtr, XVPHY_DIR_TX, 1);
		XVphy_SetBufgGtDiv(InstancePtr, XVPHY_DIR_RX, 1);
	}
	XVphy_ResetGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_RX, TRUE);
	XVphy_ResetGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX, TRUE);
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
		XVphy_ResetGtTxRx(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_RX, TRUE);
		XVphy_ResetGtTxRx(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, TRUE);
	}
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_PowerDownGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CMNA,
				TRUE);
		XVphy_PowerDownGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
				TRUE);
	}
	XVphy_MmcmReset(InstancePtr, QuadId, XVPHY_DIR_TX, TRUE);
	XVphy_MmcmReset(InstancePtr, QuadId, XVPHY_DIR_RX, TRUE);
	XVphy_IBufDsEnable(InstancePtr, QuadId, XVPHY_DIR_TX, (FALSE));
	XVphy_IBufDsEnable(InstancePtr, QuadId, XVPHY_DIR_RX, (FALSE));


	/* DRU Settings. */
	if (InstancePtr->Config.DruIsPresent) {
		XVphy_IBufDsEnable(InstancePtr, QuadId, XVPHY_DIR_RX, TRUE);
		XVphy_DruReset(InstancePtr, XVPHY_CHANNEL_ID_CHA, TRUE);
		XVphy_DruEnable(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);
		if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
		    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
			XVphy_DruSetGain(InstancePtr, XVPHY_CHANNEL_ID_CHA,
					9, 16, 5);
		}
		else {
			XVphy_DruSetGain(InstancePtr, XVPHY_CHANNEL_ID_CHA,
					9, 16, 4);
		}
	}

	XVphy_SetRxLpm(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_RX,
			1);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		XVphy_SetTxVoltageSwing(InstancePtr, QuadId, (XVphy_ChannelId)Id, 0x1);
		XVphy_SetTxPreEmphasis(InstancePtr, QuadId, (XVphy_ChannelId)Id, 0x1);
	}

	/* Clear Interrupt Register */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_STS_REG,
			0xFFFFFFFF);

	/* Interrupt Enable. */
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT);
	XVphy_IntrEnable(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);
	XVphy_ClkDetEnable(InstancePtr, TRUE);

	/* Set the flag to indicate the driver is. */
	InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

	/* Init done. */
	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_INIT, 1);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function Sets the System Clock Selection
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void XVphy_HdmiSetSystemClockSelection(XVphy *InstancePtr, u8 QuadId)
{
	XVphy_PllType XVphy_QPllType;

	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
			XVphy_QPllType = XVPHY_PLL_TYPE_QPLL;
		}
		else {
			XVphy_QPllType = XVPHY_PLL_TYPE_QPLL0;
		}

		/* Set system clock selections */
		if (InstancePtr->Config.TxSysPllClkSel ==
				InstancePtr->Config.RxSysPllClkSel) {
			if (InstancePtr->Config.RxSysPllClkSel ==
					XVPHY_SYSCLKSELDATA_TYPE_CPLL_OUTCLK) {
				XVphy_PllInitialize(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CHA,
						InstancePtr->Config.RxRefClkSel,
						InstancePtr->Config.RxRefClkSel,
						XVPHY_PLL_TYPE_CPLL,
						XVPHY_PLL_TYPE_CPLL);
			}
			else {
				XVphy_PllInitialize(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CMN0,
						InstancePtr->Config.RxRefClkSel,
						InstancePtr->Config.RxRefClkSel,
						XVphy_QPllType,
						XVphy_QPllType);
			}
		}
		else if (InstancePtr->Config.TxSysPllClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_CPLL_OUTCLK) {
			XVphy_PllInitialize(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CHA,
					InstancePtr->Config.RxRefClkSel,
					InstancePtr->Config.TxRefClkSel,
					XVPHY_PLL_TYPE_CPLL,
					XVphy_QPllType);
		}
		else {
			XVphy_PllInitialize(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CMN0,
					InstancePtr->Config.TxRefClkSel,
					InstancePtr->Config.RxRefClkSel,
					XVphy_QPllType,
					XVPHY_PLL_TYPE_CPLL);
		}
	}
	/* GTPE2 */
	else {
		if (InstancePtr->Config.TxSysPllClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_PLL0_OUTCLK) {
		XVphy_PllInitialize(InstancePtr, QuadId,
			XVPHY_CHANNEL_ID_CMN0,
			InstancePtr->Config.TxRefClkSel,
			InstancePtr->Config.RxRefClkSel,
			XVPHY_PLL_TYPE_PLL0,
			XVPHY_PLL_TYPE_PLL1);
		}
		else {
		XVphy_PllInitialize(InstancePtr, QuadId,
				XVPHY_CHANNEL_ID_CMN0,
				InstancePtr->Config.RxRefClkSel,
				InstancePtr->Config.TxRefClkSel,
				XVPHY_PLL_TYPE_PLL1,
				XVPHY_PLL_TYPE_PLL0);
		}
	}
}

/*****************************************************************************/
/**
* This function Updates the VPHY clocking.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	TxSysPllClkSel is the SYSCLKDATA selection for TX.
* @param	RxSysPllClkSel is the SYSCLKDATA selection for RX.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiUpdateClockSelection(XVphy *InstancePtr, u8 QuadId,
		XVphy_SysClkDataSelType TxSysPllClkSel,
		XVphy_SysClkDataSelType RxSysPllClkSel)
{
	u8 Id, Id0, Id1;

	/* Reset PLL */
	XVphy_ResetGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_RX, TRUE);
	XVphy_ResetGtPll(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX, TRUE);

	/* Set default. */
	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].TxState =
			XVPHY_GT_STATE_IDLE;
		InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].RxState =
			XVPHY_GT_STATE_IDLE;
	}

	/* Update VPhy Clocking */
	InstancePtr->Config.TxSysPllClkSel = TxSysPllClkSel;
	InstancePtr->Config.RxSysPllClkSel = RxSysPllClkSel;
	XVphy_HdmiSetSystemClockSelection(InstancePtr, QuadId);
}

/*****************************************************************************/
/**
* This function resets the GT TX alignment module.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID to operate on.
* @param	Reset specifies TRUE/FALSE value to either assert or deassert
*		reset on the TX alignment module, respectively.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_TxAlignReset(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Reset)
{
	u32 RegVal;
	u32 MaskVal = 0;
	u8 Id, Id0, Id1;

	/* Read TX align register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_TX_BUFFER_BYPASS_REG);

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		MaskVal |= XVPHY_TX_BUFFER_BYPASS_TXPHDLYRESET_MASK(Id);
	}

	/* Write new value to BUFG_GT register. */
	if (Reset) {
		RegVal |= MaskVal;
	}
	else {
		RegVal &= ~MaskVal;
	}
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_TX_BUFFER_BYPASS_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function resets the GT TX alignment module.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID to operate on.
* @param	Start specifies TRUE/FALSE value to either start or ttop the TX
*		alignment module, respectively.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_TxAlignStart(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Start)
{
	u32 RegVal;
	u32 MaskVal = 0;
	u8 Id, Id0, Id1;

	/* Read TX align register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_TX_BUFFER_BYPASS_REG);

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		MaskVal |= XVPHY_TX_BUFFER_BYPASS_TXPHALIGN_MASK(Id);
	}

	/* Write new value to BUFG_GT register. */
	if (Start) {
		RegVal |= MaskVal;
	}
	else {
		RegVal &= ~MaskVal;
	}

	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_TX_BUFFER_BYPASS_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function enables the VPHY's detector peripheral.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Enable specifies TRUE/FALSE value to either enable or disable
*		the clock detector respectively.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetEnable(XVphy *InstancePtr, u8 Enable)
{
	u32 RegVal;

	/* Read clkdet ctrl register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_CTRL_REG);

	/* Write new value to clkdet ctrl register. */
	if (Enable) {
		RegVal |= XVPHY_CLKDET_CTRL_RUN_MASK;
	}
	else {
		RegVal &= ~XVPHY_CLKDET_CTRL_RUN_MASK;
	}
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_CLKDET_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function clears the clock detector TX/RX timer.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for RX or TX.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetTimerClear(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir)
{
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	/* Read the clock detector control register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_CTRL_REG);

	if (Dir == XVPHY_DIR_TX) {
		RegVal |= XVPHY_CLKDET_CTRL_TX_TMR_CLR_MASK;
	}
	else {
		RegVal |= XVPHY_CLKDET_CTRL_RX_TMR_CLR_MASK;
	}

	/* Write new value to clkdet ctrl register. */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_CLKDET_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function resets clock detector TX/RX frequency.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for RX or TX.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetFreqReset(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir)
{
	u32 RegVal;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	/* Read clkdet ctrl register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_CTRL_REG);

	if (Dir == XVPHY_DIR_TX) {
		RegVal |= XVPHY_CLKDET_CTRL_TX_FREQ_RST_MASK;
	}
	else {
		RegVal |= XVPHY_CLKDET_CTRL_RX_FREQ_RST_MASK;
	}

	/* Write new value to clkdet ctrl register. */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_CLKDET_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function sets the clock detector frequency lock counter threshold value.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ThresholdVal is the threshold value to be set.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetSetFreqLockThreshold(XVphy *InstancePtr, u16 ThresholdVal)
{
	u32 RegVal;

	/* Read clkdet ctrl register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_CTRL_REG);
	RegVal &= ~XVPHY_CLKDET_CTRL_RX_FREQ_RST_MASK;

	/* Update with new threshold. */
	RegVal |= (ThresholdVal << XVPHY_CLKDET_CTRL_FREQ_LOCK_THRESH_SHIFT);

	/* Write new value to clkdet ctrl register. */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_CLKDET_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function checks clock detector RX/TX frequency zero indicator bit.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Dir is an indicator for RX or TX.
*
* @return	- TRUE if zero frequency.
*		- FALSE otherwise, if non-zero frequency.
*
* @note		None.
*
******************************************************************************/
u8 XVphy_ClkDetCheckFreqZero(XVphy *InstancePtr, XVphy_DirectionType Dir)
{
	u32 MaskVal = 0;
	u32 RegVal;

	if (Dir == XVPHY_DIR_TX) {
		MaskVal = XVPHY_CLKDET_STAT_TX_FREQ_ZERO_MASK;
	}
	else {
		MaskVal = XVPHY_CLKDET_STAT_RX_FREQ_ZERO_MASK;
	}

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_DRU_STAT_REG);
	RegVal &= MaskVal;

	if (RegVal) {
		return (TRUE);
	}

	return (FALSE);
}

/*****************************************************************************/
/**
* This function sets clock detector frequency lock counter threshold value.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	TimeoutVal is the timeout value and is normally the system clock
*		frequency.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetSetFreqTimeout(XVphy *InstancePtr, u32 TimeoutVal)
{
	XVphy_WriteReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_FREQ_TMR_TO_REG, TimeoutVal);
}

/*****************************************************************************/
/**
* This function loads the timer to TX/RX in the clock detector.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	Dir is an indicator for RX or TX.
* @param	TimeoutVal is the timeout value to store in the clock detector.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetTimerLoad(XVphy *InstancePtr, u8 QuadId,
		XVphy_DirectionType Dir, u32 TimeoutVal)
{
	u32 RegOffset;

	/* Suppress Warning Messages */
	QuadId = QuadId;

	if (Dir == XVPHY_DIR_TX) {
		RegOffset = XVPHY_CLKDET_TMR_TX_REG;
	}
	else {
		RegOffset = XVPHY_CLKDET_TMR_RX_REG;
	}

	XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, TimeoutVal);
}

/*****************************************************************************/
/**
* This function returns the frequency of the RX/TX reference clock as
* measured by the clock detector peripheral.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Dir is an indicator for RX or TX.
*
* @return	The measured frequency of the RX/TX reference clock.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_ClkDetGetRefClkFreqHz(XVphy *InstancePtr, XVphy_DirectionType Dir)
{
	u32 RegOffset;

	if (Dir == XVPHY_DIR_TX) {
		RegOffset = XVPHY_CLKDET_FREQ_TX_REG;
	}
	else {
		RegOffset = XVPHY_CLKDET_FREQ_RX_REG;
	}

	return XVphy_ReadReg(InstancePtr->Config.BaseAddr, RegOffset);
}

/*****************************************************************************/
/**
* This function returns the frequency of the DRU reference clock as measured by
* the clock detector peripheral.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	The measured frequency of the DRU reference clock.
*
* @note		The design must have a DRU for this function to return a valid
*		value.
*
******************************************************************************/
u32 XVphy_DruGetRefClkFreqHz(XVphy *InstancePtr)
{
	u32 DruFreqHz = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_CLKDET_FREQ_DRU_REG);



	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
		if (DruFreqHz > XVPHY_HDMI_GTXE2_DRU_REFCLK_MIN &&
				DruFreqHz < XVPHY_HDMI_GTXE2_DRU_REFCLK_MAX){
			return XVPHY_HDMI_GTXE2_DRU_REFCLK;
		}
	}
	else if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE2) {
		if (DruFreqHz > XVPHY_HDMI_GTHE2_DRU_REFCLK_MIN &&
				DruFreqHz < XVPHY_HDMI_GTHE2_DRU_REFCLK_MAX){
			return XVPHY_HDMI_GTHE2_DRU_REFCLK;
		}
	}
	else if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		if (DruFreqHz > XVPHY_HDMI_GTPE2_DRU_REFCLK_MIN &&
				DruFreqHz < XVPHY_HDMI_GTPE2_DRU_REFCLK_MAX){
			return XVPHY_HDMI_GTPE2_DRU_REFCLK;
		}
	}
	else if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) {
		if (DruFreqHz > XVPHY_HDMI_GTHE3_DRU_REFCLK_MIN &&
				DruFreqHz < XVPHY_HDMI_GTHE3_DRU_REFCLK_MAX){
			return XVPHY_HDMI_GTHE3_DRU_REFCLK;
		}
	}
	else {
		if (DruFreqHz > XVPHY_HDMI_GTHE4_DRU_REFCLK_MIN &&
				DruFreqHz < XVPHY_HDMI_GTHE4_DRU_REFCLK_MAX){
			return XVPHY_HDMI_GTHE4_DRU_REFCLK;
		}
	}
	/* Return Failure */
	return XST_FAILURE;
}

/*****************************************************************************/
/**
* This function resets the DRU in the VPHY.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID to operate on.
* @param	Reset specifies TRUE/FALSE value to either enable or disable
*		the DRU respectively.
*
* @return	None.
*
******************************************************************************/
void XVphy_DruReset(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Reset)
{
	u32 RegVal;
	u32 MaskVal = 0;
	u8 Id, Id0, Id1;

	/* Read DRU ctrl register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_DRU_CTRL_REG);

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		MaskVal |= XVPHY_DRU_CTRL_RST_MASK(Id);
	}

	/* Write DRU ctrl register. */
	if (Reset) {
		RegVal |= MaskVal;
	}
	else {
		RegVal &= ~MaskVal;
	}
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_DRU_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function enabled/disables the DRU in the VPHY.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID to operate on.
* @param	Enable specifies TRUE/FALSE value to either enable or disable
*		the DRU, respectively.
*
* @return	None.
*
******************************************************************************/
void XVphy_DruEnable(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 Enable)
{
	u32 RegVal;
	u32 MaskVal = 0;
	u8 Id, Id0, Id1;

	/* Read DRU ctrl register. */
	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_DRU_CTRL_REG);

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		MaskVal |= XVPHY_DRU_CTRL_EN_MASK(Id);
	}

	/* Write DRU ctrl register. */
	if (Enable) {
		RegVal |= MaskVal;
	}
	else {
		RegVal &= ~MaskVal;
	}
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_DRU_CTRL_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function gets the DRU version
*
* @param	InstancePtr is a pointer to the XVphy core instance.
*
* @return	None.
*
******************************************************************************/
u16 XVphy_DruGetVersion(XVphy *InstancePtr)
{
	u32 RegVal;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_DRU_STAT_REG);
	RegVal &= XVPHY_DRU_STAT_VERSION_MASK;
	RegVal >>= XVPHY_DRU_STAT_VERSION_SHIFT;

	return ((u16)RegVal);
}

/*****************************************************************************/
/**
* This function sets the DRU center frequency.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId specifies the channel ID.
* @param	CenterFreqHz is the frequency value to set.
*
* @return	None.
*
******************************************************************************/
void XVphy_DruSetCenterFreqHz(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u64 CenterFreqHz)
{
	u32 CenterFreqL;
	u32 CenterFreqH;
	u32 RegOffset;
	u8 Id, Id0, Id1;

	/* Split the 64-bit input into 2 32-bit values. */
	CenterFreqL = (u32)CenterFreqHz;
	CenterFreqHz >>= 32;
	CenterFreqHz &= XVPHY_DRU_CFREQ_H_MASK;;
	CenterFreqH = (u32)CenterFreqHz;

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		RegOffset = XVPHY_DRU_CFREQ_L_REG(Id);
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset,
				CenterFreqL);

		RegOffset = XVPHY_DRU_CFREQ_H_REG(Id);
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset,
				CenterFreqH);
	}
}

/*****************************************************************************/
/**
* This function sets the DRU gain.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	ChId is the channel ID to operate on.
* @param	G1 gain value.
* @param	G1_P gain value.
* @param	G2 gain value.
*
* @return	None.
*
******************************************************************************/
void XVphy_DruSetGain(XVphy *InstancePtr, XVphy_ChannelId ChId, u8 G1, u8 G1_P,
		u8 G2)
{
	u32 RegVal;
	u32 RegOffset;
	u8 Id, Id0, Id1;

	RegVal = G1 & XVPHY_DRU_GAIN_G1_MASK;
	RegVal |= (G1_P << XVPHY_DRU_GAIN_G1_P_SHIFT) &
		XVPHY_DRU_GAIN_G1_P_MASK;
	RegVal |= (G2 << XVPHY_DRU_GAIN_G2_SHIFT) & XVPHY_DRU_GAIN_G2_MASK;

	XVphy_Ch2Ids(InstancePtr, ChId, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		RegOffset = XVPHY_DRU_GAIN_REG(Id);
		XVphy_WriteReg(InstancePtr->Config.BaseAddr, RegOffset, RegVal);
	}
}

/*****************************************************************************/
/**
* This function calculates the center frequency value for the DRU.
*
* @param	InstancePtr is a pointer to the XVphy GT core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return	The calculated DRU Center frequency value.
*
* @note		According to XAPP875:
*			Center_f = fDIN * (2^32)/fdruclk
*		The DRU clock is derived from the measured reference clock and
*		the current QPLL settings.
*
******************************************************************************/
u64 XVphy_DruCalcCenterFreqHz(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId)
{
	XVphy_Channel *ChPtr, *CmnPtr;
	u64 DruRefClk;
	u64 ClkDetRefClk;
	u64 DataRate;
	u64 FDin;
	u64 FDruClk;

	DruRefClk = XVphy_DruGetRefClkFreqHz(InstancePtr);
	ClkDetRefClk = XVphy_ClkDetGetRefClkFreqHz(InstancePtr, XVPHY_DIR_RX);

	/* Take the master channel (channel 1). */
	ChPtr = &InstancePtr->Quads[QuadId].Ch1;

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		CmnPtr = &InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)];
		FDruClk = (DruRefClk * CmnPtr->PllParams.N1FbDiv *
			CmnPtr->PllParams.N2FbDiv * 2) /
			(CmnPtr->PllParams.MRefClkDiv * ChPtr->RxOutDiv * 20);
	}
	else if ((ChId == XVPHY_CHANNEL_ID_CMN0) ||
			(ChId == XVPHY_CHANNEL_ID_CMN1)) {
		FDruClk = (DruRefClk * InstancePtr->Quads[QuadId].Plls[
			XVPHY_CH2IDX(ChId)].PllParams.NFbDiv) /
			(ChPtr->RxOutDiv * 20);
	}
	else {
		FDruClk = (DruRefClk * ChPtr->PllParams.N1FbDiv *
			ChPtr->PllParams.N2FbDiv * 2) /
			(ChPtr->PllParams.MRefClkDiv * ChPtr->RxOutDiv * 20);
	}

	DataRate = 10 * ClkDetRefClk;
	FDin = DataRate * ((u64)1 << 32);

	/* Check for divide by zero. */
	if (FDin && FDruClk) {
		return (FDin / FDruClk);
	}
	return 0;
}

/*****************************************************************************/
/**
* This function sets the GT RX CDR and Equalization for DRU mode.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	Enable enables the DRU logic (when 1), or disables (when 0).
*
* @return	None.
*
******************************************************************************/
void XVphy_HdmiGtDruModeEnable(XVphy *InstancePtr, u8 Enable)
{
	u32 RegVal;
	u32 RegMask = 0;
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_DRU_EN, Enable);

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_RX_EQ_CDR_REG);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		RegMask |= XVPHY_RX_STATUS_RXCDRHOLD_MASK(Id) |
			XVPHY_RX_STATUS_RXOSOVRDEN_MASK(Id) |
			XVPHY_RX_STATUS_RXLPMLFKLOVRDEN_MASK(Id) |
			XVPHY_RX_STATUS_RXLPMHFOVRDEN_MASK(Id);
	}

	if (Enable) {
		RegVal |= RegMask;
	}
	else {
		RegVal &= ~RegMask;
	}

	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_RX_EQ_CDR_REG,
			RegVal);
}

/*****************************************************************************/
/**
* This function calculates the HDMI MMCM parameters.
*
* @param	InstancePtr is a pointer to the Vphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Dir is an indicator for RX or TX.
* @param	Ppc specifies the total number of pixels per clock.
*		- 1 = XVIDC_PPC_1
*		- 2 = XVIDC_PPC_2
*		- 4 = XVIDC_PPC_4
* @param	Bpc specifies the color depth/bits per color component.
*		- 6 = XVIDC_BPC_6
*		- 8 = XVIDC_BPC_8
*		- 10 = XVIDC_BPC_10
*		- 12 = XVIDC_BPC_12
*		- 16 = XVIDC_BPC_16
*
* @return
*		- XST_SUCCESS if calculated PLL parameters updated successfully.
*		- XST_FAILURE if parameters not updated.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_HdmiCfgCalcMmcmParam(XVphy *InstancePtr, u8 QuadId,
		XVphy_ChannelId ChId, XVphy_DirectionType Dir,
		XVidC_PixelsPerClock Ppc, XVidC_ColorDepth Bpc)
{
	u32 RefClk;
	u8 Div;
	u8 Mult;
	u8 Valid;
	u64 LineRate = 0;
	XVphy_Mmcm *MmcmPtr;
	XVphy_PllType PllType;

	/* Suppress Warning Messages */
	ChId = ChId;

	if (Dir == XVPHY_DIR_RX) {
		RefClk = InstancePtr->HdmiRxRefClkHz;
		MmcmPtr= &InstancePtr->Quads[QuadId].RxMmcm;

		RefClk = RefClk / (GetGtHdmiPtr(InstancePtr))->RxMmcmScale;
		Mult = (GetGtHdmiPtr(InstancePtr))->RxMmcmFvcoMax / RefClk;
	}
	else {
		RefClk = InstancePtr->HdmiTxRefClkHz;
		MmcmPtr= &InstancePtr->Quads[QuadId].TxMmcm;

		RefClk = RefClk / (GetGtHdmiPtr(InstancePtr))->TxMmcmScale;
		Mult = (GetGtHdmiPtr(InstancePtr))->TxMmcmFvcoMax / RefClk;

		/* Get line rate. */
		PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
				XVPHY_CHANNEL_ID_CH1);

		switch (PllType) {
			case XVPHY_PLL_TYPE_QPLL:
			case XVPHY_PLL_TYPE_QPLL0:
			case XVPHY_PLL_TYPE_PLL0:
				LineRate = InstancePtr->Quads[QuadId].Cmn0.LineRateHz;
				break;
			case XVPHY_PLL_TYPE_QPLL1:
			case XVPHY_PLL_TYPE_PLL1:
				LineRate = InstancePtr->Quads[QuadId].Cmn1.LineRateHz;
				break;
			default:
				LineRate = InstancePtr->Quads[QuadId].Ch1.LineRateHz;
				break;
		}
	}

	Div = 1;

	if (((LineRate / 1000000) > 2970) && (Ppc == XVIDC_PPC_1)) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_1PPC_ERR, 1);
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_MMCM_CFG, 1);
		return (XST_FAILURE);
	}

	/* In case of 4 pixels per clock, the M must be a multiple of four. */
	if (Ppc == XVIDC_PPC_4) {
		Mult = Mult / 4;
		Mult = Mult * 4;
	}
	/* Else the M must be a multiple of two. */
	else if (Ppc == XVIDC_PPC_2) {
		Mult = Mult / 2;
		Mult = Mult * 2;
	}

	if (!((Mult >= 1) && (Mult < 65))) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_MMCM_ERR, 1);
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_MMCM_CFG, 1);
		return (XST_FAILURE); /* Mult is out of range. */
	}

	Valid = (FALSE);
	do {
		MmcmPtr->ClkFbOutMult = Mult;
		MmcmPtr->DivClkDivide = Div;

		if (InstancePtr->Config.TransceiverWidth == 4) {
			/* Link clock: TMDS clock ratio 1/40. */
			if ((LineRate / 1000000) >= 3400) {
				MmcmPtr->ClkOut0Div = Mult;
			}
			/* Link clock: TMDS clock ratio 1/10. */
			else {
				MmcmPtr->ClkOut0Div = Mult * 4;
			}
		}
		else {//2 Byte Mode
			/* Link clock: TMDS clock ratio 1/40. */
			if ((LineRate / 1000000) >= 3400) {
				MmcmPtr->ClkOut0Div = Mult / 2;
			}
			/* Link clock: TMDS clock ratio 1/10. */
			else {
				MmcmPtr->ClkOut0Div = Mult * 2;
			}
		}

		/* TMDS Clock */
		MmcmPtr->ClkOut1Div = Mult * ((Dir == XVPHY_DIR_TX) ?
				(InstancePtr->HdmiTxSampleRate) : 1);

		/* Video clock. */
		MmcmPtr->ClkOut2Div = 0;

		switch (Bpc) {
		case XVIDC_BPC_10:
			/* Quad pixel. */
			if (Ppc == (XVIDC_PPC_4)) {
				MmcmPtr->ClkOut2Div = (Mult * 5 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Dual pixel. */
			else if (Ppc == (XVIDC_PPC_2)) {
				/* The clock ratio is 2.5 */
				/* The PLL only supports integer values */
				/* The mult must be dividable by two (2 * 2.5 = 5)
					to get an integer number */
				if ((Mult % 2) == 0) {
					MmcmPtr->ClkOut2Div = (Mult * 5 / 2 *
						((Dir == XVPHY_DIR_TX)?
						(InstancePtr->HdmiTxSampleRate) : 1));
				}
			}
			/* Single pixel. */
			else {
				/* The clock ratio is 1.25 */
				/* The PLL only supports integer values */
				/* The mult must be dividable by four (4 * 1.25 = 5)
					to get an integer number */
				if ((Mult % 4) == 0) {
					MmcmPtr->ClkOut2Div = (Mult * 5 / 4 *
						((Dir == XVPHY_DIR_TX) ?
						(InstancePtr->HdmiTxSampleRate) : 1));
				}
			}
			break;
		case XVIDC_BPC_12:
			/* Quad pixel. */
			if (Ppc == (XVIDC_PPC_4)) {
				MmcmPtr->ClkOut2Div = (Mult * 6 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Dual pixel. */
			else if (Ppc == (XVIDC_PPC_2)) {
				MmcmPtr->ClkOut2Div = (Mult * 3 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Single pixel. */
			else {
				/* The clock ratio is 1.5 */
				/* The PLL only supports integer values */
				/* The mult must be dividable by two (2 * 1.5 = 3)
					to get an integer number */
				if ((Mult % 2) == 0) {
					MmcmPtr->ClkOut2Div = (Mult * 3 / 2 *
						((Dir == XVPHY_DIR_TX) ?
						(InstancePtr->HdmiTxSampleRate) : 1));
				}
			}
			break;
		case XVIDC_BPC_16 :
			/* Quad pixel. */
			if (Ppc == (XVIDC_PPC_4)) {
				MmcmPtr->ClkOut2Div = (Mult * 8 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Dual pixel. */
			else if (Ppc == (XVIDC_PPC_2)) {
				MmcmPtr->ClkOut2Div = (Mult * 4 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Single pixel. */
			else {
				MmcmPtr->ClkOut2Div = (Mult * 2 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			break;
		case XVIDC_BPC_8:
		default:
			/* Quad pixel. */
			if (Ppc == (XVIDC_PPC_4)) {
				MmcmPtr->ClkOut2Div = (Mult * 4 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Dual pixel. */
			else if (Ppc == (XVIDC_PPC_2)) {
				MmcmPtr->ClkOut2Div = (Mult * 2 *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			/* Single pixel. */
			else {
				MmcmPtr->ClkOut2Div = (Mult *
					((Dir == XVPHY_DIR_TX) ?
					(InstancePtr->HdmiTxSampleRate) : 1));
			}
			break;
		}

		/* Only do this when the ClkOut2Div has been set */
		if (MmcmPtr->ClkOut2Div) {
			if (Dir == XVPHY_DIR_RX) {
				/* Correct divider value if TMDS clock ratio is 1/40. */
				if (InstancePtr->HdmiRxTmdsClockRatio) {
					if ((MmcmPtr->ClkOut2Div % 4) == 0) {
						MmcmPtr->ClkOut2Div =
							MmcmPtr->ClkOut2Div / 4;
					}
					/* Not divisible by 4: repeat loop with a lower
					 * multiply value. */
					else {
						MmcmPtr->ClkOut2Div = 255;
					}
				}
			}
			/* TX. */
			else if ((LineRate / 1000000) >= 3400) {
				if ((MmcmPtr->ClkOut2Div % 4) == 0) {
					MmcmPtr->ClkOut2Div =
						MmcmPtr->ClkOut2Div / 4;
				}
				/* Not divisible by 4: repeat loop with a lower
				 * multiply value. */
				else {
					MmcmPtr->ClkOut2Div = 255;
				}
			}
		}

		/* Check values. */
		if ((MmcmPtr->ClkOut0Div <= 128) &&
				(MmcmPtr->ClkOut1Div <= 128) &&
				(MmcmPtr->ClkOut2Div <= 128) &&
				(MmcmPtr->ClkOut2Div > 0)) {
			Valid = (TRUE);
		}
		else {
			/* 4 pixels per clock. */
			if (Ppc == (XVIDC_PPC_4)) {
				/* Decrease Mult value. */
				Mult -= 4;
			}
			/* 2 pixels per clock. */
			else if (Ppc == (XVIDC_PPC_2)) {
				/* Decrease M value. */
				Mult -= 2;
			}
			/* 1 pixel per clock */
			else {
				/* Decrease M value */
				Mult -= 1;
			}
		}
	} while (!Valid);

	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) &&
			(((RefClk/1000)*(Mult/MmcmPtr->ClkOut2Div)) > 148500)) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_VDCLK_HIGH_ERR, 1);
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_MMCM_CFG, 1);
		return (XST_FAILURE);
	}

	if (Valid) {
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_MMCM_CFG, 0);
		return (XST_SUCCESS);
	}
	else {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_MMCM_ERR, 1);
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_MMCM_CFG, 1);
		return (XST_FAILURE);
	}
}

/*****************************************************************************/
/**
* This function calculates the QPLL parameters.
*
* @param	InstancePtr is a pointer to the HDMI GT core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Dir is an indicator for RX or TX.
*
* @return
*		- XST_SUCCESS if calculated QPLL parameters updated
*		  successfully.
*		- XST_FAILURE if parameters not updated.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_HdmiQpllParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir)
{
	u32 Status;
	u64 RefClk = 0;
	u32 *RefClkPtr;
	u64 TxLineRate = 0;
	u8 Id, Id0, Id1;

	u8 SRArray[] = {1, 3, 5};
	u8 SRIndex;
	u8 SRValue;

	/* Suppress Warning Messages */
	ChId = ChId;

	XVphy_SysClkDataSelType SysClkDataSel = (XVphy_SysClkDataSelType) 0;
	XVphy_SysClkOutSelType SysClkOutSel = (XVphy_SysClkOutSelType) 0;
	XVphy_ChannelId ActiveCmnId = XVPHY_CHANNEL_ID_CMN0;

	u32 QpllRefClk;
	u32 QpllClkMin = 0;

	/* Determine QPLL reference clock from the first (master) channel. */
	if (Dir == XVPHY_DIR_RX) {
		QpllRefClk = InstancePtr->HdmiRxRefClkHz;
		RefClkPtr = &InstancePtr->HdmiRxRefClkHz;
	}
	else {
		QpllRefClk = InstancePtr->HdmiTxRefClkHz;
		RefClkPtr = &InstancePtr->HdmiTxRefClkHz;
	}

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE3)
	/* Determine which QPLL to use. */
	if (((101875000 <= QpllRefClk) && (QpllRefClk <= 122500000)) ||
		((203750000 <= QpllRefClk) &&
			(QpllRefClk <= 245000000)) ||
		((407000000 <= QpllRefClk) &&
			(QpllRefClk <= 490000000))) {
		SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL1_OUTCLK;
		SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL1_REFCLK;
		ActiveCmnId = XVPHY_CHANNEL_ID_CMN1;
		QpllClkMin = (u32) XVPHY_HDMI_GTHE3_QPLL1_REFCLK_MIN;
	}
	else {
		SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK;
		SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL0_REFCLK;
		ActiveCmnId = XVPHY_CHANNEL_ID_CMN0;
		QpllClkMin = (u32) XVPHY_HDMI_GTHE3_QPLL0_REFCLK_MIN;
	}
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE4)
	/* Determine which QPLL to use. */
	if (((101875000 <= QpllRefClk) && (QpllRefClk <= 122500000)) ||
		((203750000 <= QpllRefClk) && (QpllRefClk <= 245000000)) ||
		((407000000 <= QpllRefClk) && (QpllRefClk <= 490000000))) {
		SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL1_OUTCLK;
		SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL1_REFCLK;
		ActiveCmnId = XVPHY_CHANNEL_ID_CMN1;
		QpllClkMin = (u32) XVPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN;
	}
	else {
		SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK;
		SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL0_REFCLK;
		ActiveCmnId = XVPHY_CHANNEL_ID_CMN0;
		QpllClkMin = (u32) XVPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN;
	}
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE2)
	SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK;
	SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL_REFCLK;
	ActiveCmnId = XVPHY_CHANNEL_ID_CMN;
	QpllClkMin = (GetGtHdmiPtr(InstancePtr))->Qpll0RefClkMin;
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
	SysClkDataSel = XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK;
	SysClkOutSel = XVPHY_SYSCLKSELOUT_TYPE_QPLL_REFCLK;
	ActiveCmnId = XVPHY_CHANNEL_ID_CMN;
	QpllClkMin = (GetGtHdmiPtr(InstancePtr))->Qpll0RefClkMin;
#endif

	/* Update QPLL clock selections. */
	XVphy_CfgSysClkDataSel(InstancePtr, QuadId, Dir, SysClkDataSel);
	XVphy_CfgSysClkOutSel(InstancePtr, QuadId, Dir, SysClkOutSel);

	/* RX is using QPLL. */
	if (Dir == XVPHY_DIR_RX) {
		/* Check if the reference clock is not below the minimum QPLL
		 * input frequency. */
		if (QpllRefClk >= QpllClkMin) {
			RefClk = QpllRefClk;

			/* Scaled line rate. */
			if (InstancePtr->HdmiRxTmdsClockRatio) {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CMNA, (RefClk * 40));
			}
			else {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CMNA, (RefClk * 10));
			}

			/* Clear DRU is enabled flag. */
			InstancePtr->HdmiRxDruIsEnabled = 0;

			/* Set RX data width. */
			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
					&Id0, &Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				if (InstancePtr->Config.TransceiverWidth == 2) {
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxDataWidth = 20;
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxIntDataWidth = 2;
				}
				else {
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxDataWidth = 40;
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxIntDataWidth = 4;
				}
			}

		}
		/* The reference clock is below the minimum frequency thus
		 * select the DRU. */
		else if (InstancePtr->Config.DruIsPresent) {
			RefClk = XVphy_DruGetRefClkFreqHz(InstancePtr);

			/* Round input frequency to 10 kHz. */
			RefClk = (RefClk+5000) / 10000;
			RefClk = RefClk * 10000;

			/* Set the DRU to operate at a linerate of 2.5 Gbps. */
			XVphy_CfgLineRate(InstancePtr,
				QuadId, XVPHY_CHANNEL_ID_CMNA,
				(GetGtHdmiPtr(InstancePtr))->DruLineRate);

			/* Set DRU is enabled flag. */
			InstancePtr->HdmiRxDruIsEnabled = 1;

			/* Set RX data width to 40 and 4 bytes. */
			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
					&Id0, &Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[QuadId].Plls[
					XVPHY_CH2IDX(Id)].RxDataWidth = 20;
				InstancePtr->Quads[QuadId].Plls[
					XVPHY_CH2IDX(Id)].RxIntDataWidth = 2;
			}
		}
		else {
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_NO_DRU, 1);
			XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_NO_DRU, 1);
			return (XST_FAILURE);
		}
	}

	/* TX is using QPLL. */
	else {
		/* Update TX line rates. */
		XVphy_CfgLineRate(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CMNA,
				(u64)((*RefClkPtr) * 10));
		TxLineRate = (*RefClkPtr) / 100000;;

		/* Set default TX sample rate. */
		InstancePtr->HdmiTxSampleRate = 1;

		/* Check if the linerate is above the 340 Mcsc. */
		if ((TxLineRate) >= 3400) {
			(*RefClkPtr) = (*RefClkPtr) / 4;
		}
	}

	/* Calculate QPLL values. */
	for (SRIndex = 0; SRIndex < sizeof(SRArray); SRIndex++) {
		/* Only use oversampling when then TX is using the QPLL. */
		if (Dir == XVPHY_DIR_TX) {
			SRValue = SRArray[SRIndex];

			/* TX reference clock is below the minimum QPLL clock
			 * input frequency. */
			if ((*RefClkPtr) < QpllClkMin) {
				RefClk = ((*RefClkPtr) * SRValue);

				/* Calculate scaled line rate. */
				if (TxLineRate >= 3400) {
					XVphy_CfgLineRate(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CMNA,
						(u64)(RefClk * 40));
				}
				else {
					XVphy_CfgLineRate(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CMNA,
						(u64)(RefClk * 10));
				}
			}
			/* TX reference clock is in QPLL clock input range.
			 * In this case don't increase the reference clock, but
			 * increase the line rate. */
			else {
				RefClk = (*RefClkPtr);

				/* Calculate scaled line rate. */
				if (TxLineRate >= 3400) {
					XVphy_CfgLineRate(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CMNA,
						(u64)(RefClk * 40 *SRValue));
				}

				else {
					XVphy_CfgLineRate(InstancePtr, QuadId,
						XVPHY_CHANNEL_ID_CMNA,
						(u64)(RefClk * 10 *SRValue));
				}
			}
		}
		/* For all other reference clocks force sample rate to one. */
		else {
			SRValue = 1;
		}

		Status = XVphy_ClkCalcParams(InstancePtr, QuadId, ActiveCmnId,
						Dir, RefClk);
		if (Status == (XST_SUCCESS)) {
			/* Only execute when the TX is using the QPLL. */
			if (Dir == XVPHY_DIR_TX) {
				/* Set TX sample rate. */
				InstancePtr->HdmiTxSampleRate = SRValue;

				/* Update reference clock only when the
				 * reference clock is below the minimum QPLL
				 * input frequency. */
				if ((*RefClkPtr) < QpllClkMin) {
					(*RefClkPtr) = (*RefClkPtr) * SRValue;
				}
				else if (SRValue > 1) {
					XVphy_LogWrite(InstancePtr,
							XVPHY_LOG_EVT_GT_PLL_LAYOUT, 1);
					XVphy_CfgErrIntr(InstancePtr,
							XVPHY_ERRIRQ_QPLL_CFG, 1);
					return (XST_FAILURE);
				}
			}
			if (Dir == XVPHY_DIR_RX) {
			XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_NO_DRU, 0);
			}
			XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_QPLL_CFG, 0);
			return (XST_SUCCESS);
		}
	}
	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_GT_QPLL_CFG_ERR, 1);
	XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_QPLL_CFG, 1);
	return (XST_FAILURE);
}

/*****************************************************************************/
/**
* This function calculates the CPLL parameters.
*
* @param	InstancePtr is a pointer to the HDMI GT core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Dir is an indicator for RX or TX.
*
* @return
*		- XST_SUCCESS if calculated CPLL parameters updated
*		  successfully.
*		- XST_FAILURE if parameters not updated.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_HdmiCpllParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVphy_DirectionType Dir)
{
	u32 Status;
	u64 RefClk = 0;
	u32 *RefClkPtr;
	u32 TxLineRate = 0;
	XVphy_ChannelId ChannelId = XVPHY_CHANNEL_ID_CHA;
	u8 Id, Id0, Id1;

	u8 SRArray[] = {1, 3, 5};
	u8 SRIndex;
	u8 SRValue;

	XVphy_PllType PllType;

	/* Suppress Warning Messages */
	ChId = ChId;

	/* Change Channel ID to Common if GTPE2 */
	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		PllType = XVphy_GetPllType(InstancePtr, QuadId, Dir,
				XVPHY_CHANNEL_ID_CH1);
		if (PllType == XVPHY_PLL_TYPE_PLL0) {
			ChannelId = XVPHY_CHANNEL_ID_CMN0;
		}
		else {
			ChannelId = XVPHY_CHANNEL_ID_CMN1;
		}
	}

	/* TX is using CPLL. */
	if ((Dir == XVPHY_DIR_TX) && (!XVphy_IsBonded(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CH1))) {

		/* Set default TX sample rate. */
		InstancePtr->HdmiTxSampleRate = 1;

		/* Set line rate.  */
		RefClkPtr = &InstancePtr->HdmiTxRefClkHz;
		XVphy_CfgLineRate(InstancePtr, QuadId, ChannelId,
				(u64)((*RefClkPtr) * 10));
		TxLineRate = (*RefClkPtr)  / 100000;

		/* Check if the line rate is above the 340 Mcsc. */
		if (TxLineRate >= 3400) {
			(*RefClkPtr) = (*RefClkPtr) / 4;
		}
	}
	/* RX is using CPLL. */
	else {
		RefClkPtr = &InstancePtr->HdmiRxRefClkHz;

		/* Check if the reference clock is not below the minimum CPLL
		 * input frequency. */
		if ((*RefClkPtr) >=
				(GetGtHdmiPtr(InstancePtr))->CpllRefClkMin) {
			RefClk = (*RefClkPtr);

			/* Scaled linerate */
			if (InstancePtr->HdmiRxTmdsClockRatio) {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					ChannelId, (RefClk * 40));
			}
			else {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					ChannelId, (RefClk * 10));
			}

			/* Clear DRU is enabled flag. */
			InstancePtr->HdmiRxDruIsEnabled = 0;

			/* Set RX data width. */
			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
					&Id0, &Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				if ((InstancePtr->Config.TransceiverWidth == 2) ||
					(InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxDataWidth = 20;
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxIntDataWidth = 2;
				}
				else {
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxDataWidth = 40;
					InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
						RxIntDataWidth = 4;
				}
			}

		}
		/* The reference clock is below the minimum frequency thus
		 * select the DRU. */
		else {
			if (InstancePtr->Config.DruIsPresent) {
				/* Return config not found error when TMDS ratio is 1/40 */
                if (InstancePtr->HdmiRxTmdsClockRatio) {
                    XVphy_LogWrite(InstancePtr,
                        XVPHY_LOG_EVT_GT_CPLL_CFG_ERR, 1);
				XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_CPLL_CFG, 1);
				return (XST_FAILURE);
                }

                RefClk = XVphy_DruGetRefClkFreqHz(InstancePtr);

				/* Round input frequency to 10 kHz. */
				RefClk = (RefClk+5000) / 10000;
				RefClk = RefClk * 10000;

				/* Set the DRU to operate at a linerate of
				 * 2.5 Gbps. */
				XVphy_CfgLineRate(InstancePtr, QuadId,
						ChannelId,
						(GetGtHdmiPtr(InstancePtr))->
						DruLineRate);

				/* Set DRU is enabled flag. */
				InstancePtr->HdmiRxDruIsEnabled = 1;

				/* Set RX data width. */
				XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
						&Id0, &Id1);
				for (Id = Id0; Id <= Id1; Id++) {
					InstancePtr->Quads[QuadId].Plls[
						XVPHY_CH2IDX(Id)].
						RxDataWidth = 20;
					InstancePtr->Quads[QuadId].Plls[
						XVPHY_CH2IDX(Id)].
						RxIntDataWidth = 2;
				}

				if (TxLineRate > (((GetGtHdmiPtr(InstancePtr))
						->DruLineRate) / 1000000)) {
					XVphy_LogWrite(InstancePtr,
							XVPHY_LOG_EVT_VD_NOT_SPRTD_ERR, 1);
				XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_CPLL_CFG, 1);
				return (XST_FAILURE);
				}
			}
			else {
				XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_NO_DRU, 1);
			XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_NO_DRU, 1);
			return (XST_FAILURE);
			}
		}
	}

	/* Try different sample rates. */
	for (SRIndex = 0; SRIndex < sizeof(SRArray); SRIndex++) {
		/* Only use oversampling when then TX is using the CPLL. */
		if ((Dir == XVPHY_DIR_TX) && (!XVphy_IsBonded(InstancePtr,
					QuadId, XVPHY_CHANNEL_ID_CH1))) {
			SRValue = SRArray[SRIndex];

			/* Multiply the reference clock with the sample rate
			 * value. */
			RefClk = ((*RefClkPtr) * SRValue);

			/* Calculate scaled line rate. */
			if (TxLineRate >= 3400) {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					ChannelId, (RefClk * 40));
			}
			else {
				XVphy_CfgLineRate(InstancePtr, QuadId,
					ChannelId, (RefClk * 10));
			}
		}
		/* For all other reference clocks force sample rate to one. */
		else {
			SRValue = 1;
		}

		Status = XVphy_ClkCalcParams(InstancePtr, QuadId,
					ChannelId, Dir, RefClk);
		if (Status == (XST_SUCCESS)) {
			/* Only execute when the TX is using the QPLL. */
			if ((Dir == XVPHY_DIR_TX) && (!XVphy_IsBonded(
					InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CH1))) {
				InstancePtr->HdmiTxSampleRate = SRValue;

				(*RefClkPtr) = (*RefClkPtr) * SRValue;
			}
			if (Dir == XVPHY_DIR_RX) {
			XVphy_CfgErrIntr(InstancePtr,
					XVPHY_ERRIRQ_NO_DRU, 0);
			}
			XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_CPLL_CFG, 0);
			return (XST_SUCCESS);
		}
	}

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_GT_CPLL_CFG_ERR, 1);
	XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_CPLL_CFG, 1);
	return (XST_FAILURE);
}

/*****************************************************************************/
/**
* This function update/set the HDMI TX parameter.
*
* @param	InstancePtr is a pointer to the Vphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
* @param	Ppc is the pixels per clock to set.
* @param	Bpc is the bits per color to set.
* @param	ColorFormat is the color format to set.
*
* @return
*		- XST_SUCCESS if TX parameters set/updated.
*		- XST_FAILURE if low resolution video not supported.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_SetHdmiTxParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId,
		XVidC_PixelsPerClock Ppc, XVidC_ColorDepth Bpc,
		XVidC_ColorFormat ColorFormat)
{
	u32 Status;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid((Ppc == (XVIDC_PPC_1)) || (Ppc == (XVIDC_PPC_2)) ||
			(Ppc == (XVIDC_PPC_4)));
	Xil_AssertNonvoid((Bpc == (XVIDC_BPC_8)) || (Bpc == (XVIDC_BPC_10)) ||
			(Bpc == (XVIDC_BPC_12)) || (Bpc == (XVIDC_BPC_16)));
	Xil_AssertNonvoid((ColorFormat == (XVIDC_CSF_RGB)) ||
			(ColorFormat == (XVIDC_CSF_YCRCB_444)) ||
			(ColorFormat == (XVIDC_CSF_YCRCB_422)) ||
			(ColorFormat == (XVIDC_CSF_YCRCB_420)));

	/* Only calculate the QPLL/CPLL parameters when the GT TX and RX are not
	 * coupled. */
	if (!XVphy_IsBonded(InstancePtr, QuadId, ChId)) {
		if (XVphy_IsTxUsingCpll(InstancePtr, QuadId, ChId)) {
			Status = XVphy_HdmiCpllParam(InstancePtr, QuadId, ChId,
					XVPHY_DIR_TX);
		}
		else if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
			Status = XVphy_HdmiQpllParam(InstancePtr, QuadId, ChId,
					XVPHY_DIR_TX);
			/* Update SysClk and PLL Clk registers immediately. */
			XVphy_WriteCfgRefClkSelReg(InstancePtr, QuadId);
		}
		else {
			/* GTP divider calculation is same with CPLL */
			Status = XVphy_HdmiCpllParam(InstancePtr, QuadId, ChId,
			XVPHY_DIR_TX);
		}

		if (Status == XST_FAILURE) {
			return Status;
		}
	}
	/* Bonded mode. */
	else {
		/* Copy reference clock. */
		InstancePtr->HdmiTxRefClkHz = InstancePtr->HdmiRxRefClkHz;

		/* Copy the line rate. */
		if (XVphy_IsRxUsingQpll(InstancePtr, QuadId,
					XVPHY_CHANNEL_ID_CH1)) {
			InstancePtr->Quads[QuadId].Ch1.LineRateHz =
				InstancePtr->Quads[QuadId].Cmn0.LineRateHz;
		}
		else {
			InstancePtr->Quads[QuadId].Cmn0.LineRateHz =
				InstancePtr->Quads[QuadId].Ch1.LineRateHz;
		}

		InstancePtr->HdmiTxSampleRate = 1;

		Status = (XST_SUCCESS);
	}

	/* Is HDMITXSS PPC match with VPHY PPC? */
	if (Ppc == InstancePtr->Config.Ppc) {
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_VD_NOT_SPRTD, 0);
		Status = (XST_SUCCESS);
	}
	else {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_PPC_MSMTCH_ERR, 1);
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_VD_NOT_SPRTD, 1);
		Status = (XST_FAILURE);
	}
	if (Status == (XST_SUCCESS)) {
		/* Calculate TXPLL parameters.
		 * In HDMI the colordepth in YUV422 is always 12 bits,
		 * although on the link itself it is being transmitted as
		 * 8-bits. Therefore if the colorspace is YUV422, then force the
		 * colordepth to 8 bits. */
		if (ColorFormat == XVIDC_CSF_YCRCB_422) {
			Status = XVphy_HdmiCfgCalcMmcmParam(InstancePtr, QuadId,
				ChId, XVPHY_DIR_TX, Ppc, XVIDC_BPC_8);
		}
		/* Other colorspaces. */
		else {
			Status = XVphy_HdmiCfgCalcMmcmParam(InstancePtr, QuadId,
				ChId, XVPHY_DIR_TX, Ppc, Bpc);
		}
	}
	else {
		Status = (XST_FAILURE);
	}

	return Status;
}

/*****************************************************************************/
/**
* This function update/set the HDMI RX parameter.
*
* @param	InstancePtr is a pointer to the Vphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- XST_SUCCESS if RX parameters set/updated.
*		- XST_FAILURE if low resolution video not supported.
*
* @note		None.
*
******************************************************************************/
u32 XVphy_SetHdmiRxParam(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	XVphy_ChannelId ChanId = ChId;
	XVphy_PllType PllType;
	u32 Status;
	u64 DruCenterFreq;
	u8 Id, Id0, Id1;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	if (XVphy_IsRxUsingCpll(InstancePtr, QuadId, ChId)) {
		Status = XVphy_HdmiCpllParam(InstancePtr, QuadId, ChId,
				XVPHY_DIR_RX);
	}
	else if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		Status = XVphy_HdmiQpllParam(InstancePtr, QuadId, ChId,
				XVPHY_DIR_RX);
		/* Update SysClk and PLL Clk registers immediately */
		XVphy_WriteCfgRefClkSelReg(InstancePtr, QuadId);
	}
	else {
		/* GTP divider calculation is same with CPLL */
		Status = XVphy_HdmiCpllParam(InstancePtr, QuadId, ChId,
				XVPHY_DIR_RX);
	}

	if (XVphy_IsBonded(InstancePtr, QuadId, XVPHY_CHANNEL_ID_CH1)) {
		/* Same divisor value for all channels. */
		XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
		for (Id = Id0; Id <= Id1; Id++) {
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(Id)].
				TxOutDiv = InstancePtr->Quads[QuadId].
				Plls[XVPHY_CH2IDX(Id)].RxOutDiv;
		}
	}

	if (InstancePtr->HdmiRxDruIsEnabled) {
		/* Determine PLL type. */
		PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
				XVPHY_CHANNEL_ID_CH1);
		/* Update the ChId */
		ChanId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, PllType);

		DruCenterFreq = XVphy_DruCalcCenterFreqHz(InstancePtr, QuadId,
					ChanId);
		XVphy_DruSetCenterFreqHz(InstancePtr, XVPHY_CHANNEL_ID_CHA,
						DruCenterFreq);
	}

	return Status;
}

/*****************************************************************************/
/**
* This function prints Video PHY debug information related to HDMI.
*
* @param	InstancePtr is a pointer to the Vphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiDebugInfo(XVphy *InstancePtr, u8 QuadId, XVphy_ChannelId ChId)
{
	u32 RegValue;
	XVphy_Channel *ChPtr;
	XVphy_ChannelId CmnId = XVPHY_CHANNEL_ID_CMN0;
	u8 CpllDVal;
	u8 QpllDVal;
	u8 UsesQpll0 = 0;
	u8 TxUsesPll0 = 0;

	ChPtr = &InstancePtr->Quads[QuadId].Plls[0];

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		UsesQpll0 = (FALSE);
		if (ChPtr->TxDataRefClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_PLL0_OUTCLK) {
			TxUsesPll0 = 1;
			xil_printf("TX => PLL0 / ");
		}
		else {
			xil_printf("TX => PLL1 / ");
		}
	}
	else if (XVphy_IsTxUsingCpll(InstancePtr, QuadId, ChId)) {
		xil_printf("TX => CPLL / ");
	}
	else {
		if ((ChPtr->TxDataRefClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK) ||
		    (ChPtr->TxDataRefClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK)) {
			UsesQpll0 = (TRUE);
			CmnId = XVPHY_CHANNEL_ID_CMN0;
		}
		else {
			UsesQpll0 = (FALSE);
			CmnId = XVPHY_CHANNEL_ID_CMN1;
		}
		xil_printf("TX => QPLL%d / ", (UsesQpll0 ? 0 : 1));
	}

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		xil_printf("RX => PLL%d\n\r", (TxUsesPll0 ? 1 : 0));
	}
	else if (XVphy_IsRxUsingCpll(InstancePtr, QuadId, ChId)) {
		xil_printf("RX => CPLL\n\r");
	}
	else {
		if ((ChPtr->RxDataRefClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_QPLL_OUTCLK) ||
		    (ChPtr->RxDataRefClkSel ==
				XVPHY_SYSCLKSELDATA_TYPE_QPLL0_OUTCLK)) {
			UsesQpll0 = (TRUE);
			CmnId = XVPHY_CHANNEL_ID_CMN0;
		}
		else {
			UsesQpll0 = (FALSE);
			CmnId = XVPHY_CHANNEL_ID_CMN1;
		}
		xil_printf("RX => QPLL%d\n\r", (UsesQpll0 ? 0 : 1));
	}

	xil_printf("RX state: ");
	switch (InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)].RxState) {
	case (XVPHY_GT_STATE_IDLE):
		xil_printf("idle\n\r");
		break;
	case (XVPHY_GT_STATE_LOCK):
		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
			xil_printf("PLL%d lock\n\r", (TxUsesPll0 ? 1 : 0));
		}
		else if (XVphy_IsRxUsingCpll(InstancePtr, QuadId, ChId)) {
			xil_printf("CPLL lock\n\r");
		}
		else {
			xil_printf("QPLL%d lock\n\r", (UsesQpll0 ? 0 : 1));
		}
		break;
	case (XVPHY_GT_STATE_RESET):
		xil_printf("GT reset\n\r");
		break;
	case (XVPHY_GT_STATE_READY):
		xil_printf("ready\n\r");
		break;
	default:
		xil_printf("unknown\n\r");
		break;
	}

	xil_printf("TX state: ");
	switch (InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(ChId)].TxState) {
	case (XVPHY_GT_STATE_IDLE):
		xil_printf("idle\n\r");
		break;
	case (XVPHY_GT_STATE_LOCK):
		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
			xil_printf("PLL%d lock\n\r", (TxUsesPll0 ? 0 : 1));
		}
		else if (XVphy_IsTxUsingCpll(InstancePtr, QuadId, ChId)) {
			xil_printf("CPLL lock\n\r");
		}
		else {
			xil_printf("QPLL%d lock\n\r", (UsesQpll0 ? 0 : 1));
		}
		break;
	case (XVPHY_GT_STATE_RESET):
		xil_printf("GT reset\n\r");
		break;
	case (XVPHY_GT_STATE_ALIGN):
		xil_printf("align\n\r");
		break;
	case (XVPHY_GT_STATE_READY):
		xil_printf("ready\n\r");
		break;
	default:
		xil_printf("unknown\n\r");
		break;
	}

	if (XVphy_IsTxUsingCpll(InstancePtr, QuadId, ChId)) {
		QpllDVal = ChPtr->RxOutDiv;
		CpllDVal = ChPtr->TxOutDiv;
	}
	else {
		CpllDVal = ChPtr->RxOutDiv;
		QpllDVal = ChPtr->TxOutDiv;
	}

	xil_printf("\n\r");
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		xil_printf("QPLL%d settings\n\r", (UsesQpll0 ? 0 : 1));
		xil_printf("-------------\n\r");
		xil_printf("M : %d - N : %d - D : %d\n\r",
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.MRefClkDiv,
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.NFbDiv, QpllDVal);
		xil_printf("\n\r");

		xil_printf("CPLL settings\n\r");
		xil_printf("-------------\n\r");
		xil_printf("M : %d - N1 : %d - N2 : %d - D : %d\n\r",
			ChPtr->PllParams.MRefClkDiv,
			ChPtr->PllParams.N1FbDiv, ChPtr->PllParams.N2FbDiv,
			CpllDVal);
		xil_printf("\n\r");
	}
	else {
		CmnId = XVPHY_CHANNEL_ID_CMN0;
		xil_printf("PLL0 settings\n\r");
		xil_printf("-------------\n\r");
		xil_printf("M : %d - N1 : %d - N2 : %d - D : %d\n\r",
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.MRefClkDiv,
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.N1FbDiv,
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.N2FbDiv,
			(TxUsesPll0 ? ChPtr->TxOutDiv : ChPtr->RxOutDiv));
		xil_printf("\n\r");

		CmnId = XVPHY_CHANNEL_ID_CMN1;
		xil_printf("PLL1 settings\n\r");
		xil_printf("-------------\n\r");
		xil_printf("M : %d - N1 : %d - N2 : %d - D : %d\n\r",
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.MRefClkDiv,
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.N1FbDiv,
			InstancePtr->Quads[QuadId].Plls[XVPHY_CH2IDX(CmnId)].
				PllParams.N2FbDiv,
			(TxUsesPll0 ? ChPtr->RxOutDiv : ChPtr->TxOutDiv));
		xil_printf("\n\r");
	}

	xil_printf("RX MMCM settings\n\r");
	xil_printf("-------------\n\r");
	xil_printf("Mult : %d - Div : %d - Clk0Div : %d - Clk1Div : %d - "
		   "Clk2Div : %d\n\r",
		InstancePtr->Quads[QuadId].RxMmcm.ClkFbOutMult,
		InstancePtr->Quads[QuadId].RxMmcm.DivClkDivide,
		InstancePtr->Quads[QuadId].RxMmcm.ClkOut0Div,
		InstancePtr->Quads[QuadId].RxMmcm.ClkOut1Div,
		InstancePtr->Quads[QuadId].RxMmcm.ClkOut2Div);
	xil_printf("\n\r");

	xil_printf("TX MMCM settings\n\r");
	xil_printf("-------------\n\r");
	xil_printf("Mult : %d - Div : %d - Clk0Div : %d - Clk1Div : %d - "
		   "Clk2Div : %d\n\r",
		InstancePtr->Quads[QuadId].TxMmcm.ClkFbOutMult,
		InstancePtr->Quads[QuadId].TxMmcm.DivClkDivide,
		InstancePtr->Quads[QuadId].TxMmcm.ClkOut0Div,
		InstancePtr->Quads[QuadId].TxMmcm.ClkOut1Div,
		InstancePtr->Quads[QuadId].TxMmcm.ClkOut2Div);
	xil_printf("\n\r");

	if (InstancePtr->Config.DruIsPresent) {
		xil_printf("DRU Settings\n\r");
		xil_printf("-------------\n\r");
		RegValue = XVphy_DruGetVersion(InstancePtr);
		xil_printf("Version  : %d\n\r", RegValue);

		if (InstancePtr->HdmiRxDruIsEnabled) {
			RegValue = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
					XVPHY_DRU_GAIN_REG(ChId));

			xil_printf("G1       : %d\n\rG1_P     : %d\n\r"
				   "G2       : %d\n\r",
				((RegValue & XVPHY_DRU_GAIN_G1_MASK)),
				((RegValue & XVPHY_DRU_GAIN_G1_P_MASK) >>
					XVPHY_DRU_GAIN_G1_P_SHIFT),
				((RegValue & XVPHY_DRU_GAIN_G2_MASK) >>
					XVPHY_DRU_GAIN_G2_SHIFT));

			RegValue = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
				XVPHY_DRU_CFREQ_H_REG(ChId));
			xil_printf("Center_F : %x", RegValue);

			RegValue = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
				XVPHY_DRU_CFREQ_L_REG(ChId));
			xil_printf("%x\n\r", RegValue);
		}
		else {
			xil_printf("DRU is disabled\n\r");
		}

		xil_printf(" \n\r");
	}
}

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE3)
static const XVphy_GtHdmiChars Gthe3HdmiChars = {
	.DruLineRate = XVPHY_HDMI_GTHE3_DRU_LRATE,
	.PllScale = XVPHY_HDMI_GTHE3_PLL_SCALE,
	.Qpll0RefClkMin = XVPHY_HDMI_GTHE3_QPLL0_REFCLK_MIN,
	.Qpll1RefClkMin = XVPHY_HDMI_GTHE3_QPLL1_REFCLK_MIN,
	.CpllRefClkMin = XVPHY_HDMI_GTHE3_CPLL_REFCLK_MIN,
	.TxMmcmScale = XVPHY_HDMI_GTHE3_TX_MMCM_SCALE,
	.TxMmcmFvcoMin = XVPHY_HDMI_GTHE3_TX_MMCM_FVCO_MIN,
	.TxMmcmFvcoMax = XVPHY_HDMI_GTHE3_TX_MMCM_FVCO_MAX,
	.RxMmcmScale = XVPHY_HDMI_GTHE3_RX_MMCM_SCALE,
	.RxMmcmFvcoMin = XVPHY_HDMI_GTHE3_RX_MMCM_FVCO_MIN,
	.RxMmcmFvcoMax = XVPHY_HDMI_GTHE3_RX_MMCM_FVCO_MAX,
};
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE4)
static const XVphy_GtHdmiChars Gthe4HdmiChars = {
	.DruLineRate = XVPHY_HDMI_GTHE4_DRU_LRATE,
	.PllScale = XVPHY_HDMI_GTHE4_PLL_SCALE,
	.Qpll0RefClkMin = XVPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN,
	.Qpll1RefClkMin = XVPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN,
	.CpllRefClkMin = XVPHY_HDMI_GTHE4_CPLL_REFCLK_MIN,
	.TxMmcmScale = XVPHY_HDMI_GTHE4_TX_MMCM_SCALE,
	.TxMmcmFvcoMin = XVPHY_HDMI_GTHE4_TX_MMCM_FVCO_MIN,
	.TxMmcmFvcoMax = XVPHY_HDMI_GTHE4_TX_MMCM_FVCO_MAX,
	.RxMmcmScale = XVPHY_HDMI_GTHE4_RX_MMCM_SCALE,
	.RxMmcmFvcoMin = XVPHY_HDMI_GTHE4_RX_MMCM_FVCO_MIN,
	.RxMmcmFvcoMax = XVPHY_HDMI_GTHE4_RX_MMCM_FVCO_MAX,
};
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE2)
static const XVphy_GtHdmiChars Gthe2HdmiChars = {
	.DruLineRate = XVPHY_HDMI_GTHE2_DRU_LRATE,
	.PllScale = XVPHY_HDMI_GTHE2_PLL_SCALE,
	.Qpll0RefClkMin = XVPHY_HDMI_GTHE2_QPLL_REFCLK_MIN,
	.Qpll1RefClkMin = 0,
	.CpllRefClkMin = XVPHY_HDMI_GTHE2_CPLL_REFCLK_MIN,
	.TxMmcmScale = XVPHY_HDMI_GTHE2_TX_MMCM_SCALE,
	.TxMmcmFvcoMin = XVPHY_HDMI_GTHE2_TX_MMCM_FVCO_MIN,
	.TxMmcmFvcoMax = XVPHY_HDMI_GTHE2_TX_MMCM_FVCO_MAX,
	.RxMmcmScale = XVPHY_HDMI_GTHE2_RX_MMCM_SCALE,
	.RxMmcmFvcoMin = XVPHY_HDMI_GTHE2_RX_MMCM_FVCO_MIN,
	.RxMmcmFvcoMax = XVPHY_HDMI_GTHE2_RX_MMCM_FVCO_MAX,
};
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTPE2)
static const XVphy_GtHdmiChars Gtpe2HdmiChars = {
	.DruLineRate = XVPHY_HDMI_GTPE2_DRU_LRATE,
	.PllScale = XVPHY_HDMI_GTPE2_PLL_SCALE,
	.Qpll0RefClkMin = XVPHY_HDMI_GTPE2_QPLL_REFCLK_MIN,
	.Qpll1RefClkMin = 0,
	.CpllRefClkMin = XVPHY_HDMI_GTPE2_CPLL_REFCLK_MIN,
	.TxMmcmScale = XVPHY_HDMI_GTPE2_TX_MMCM_SCALE,
	.TxMmcmFvcoMin = XVPHY_HDMI_GTPE2_TX_MMCM_FVCO_MIN,
	.TxMmcmFvcoMax = XVPHY_HDMI_GTPE2_TX_MMCM_FVCO_MAX,
	.RxMmcmScale = XVPHY_HDMI_GTPE2_RX_MMCM_SCALE,
	.RxMmcmFvcoMin = XVPHY_HDMI_GTPE2_RX_MMCM_FVCO_MIN,
	.RxMmcmFvcoMax = XVPHY_HDMI_GTPE2_RX_MMCM_FVCO_MAX,
};
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
static const XVphy_GtHdmiChars Gtxe2HdmiChars = {
	.DruLineRate = XVPHY_HDMI_GTXE2_DRU_LRATE,
	.PllScale = XVPHY_HDMI_GTXE2_PLL_SCALE,
	.Qpll0RefClkMin = XVPHY_HDMI_GTXE2_QPLL_REFCLK_MIN,
	.Qpll1RefClkMin = 0,
	.CpllRefClkMin = XVPHY_HDMI_GTXE2_CPLL_REFCLK_MIN,
	.TxMmcmScale = XVPHY_HDMI_GTXE2_TX_MMCM_SCALE,
	.TxMmcmFvcoMin = XVPHY_HDMI_GTXE2_TX_MMCM_FVCO_MIN,
	.TxMmcmFvcoMax = XVPHY_HDMI_GTXE2_TX_MMCM_FVCO_MAX,
	.RxMmcmScale = XVPHY_HDMI_GTXE2_RX_MMCM_SCALE,
	.RxMmcmFvcoMin = XVPHY_HDMI_GTXE2_RX_MMCM_FVCO_MIN,
	.RxMmcmFvcoMax = XVPHY_HDMI_GTXE2_RX_MMCM_FVCO_MAX,
};
#endif

/*****************************************************************************/
/**
* This function returns a pointer to the HDMI parameters based on the GT type.
*
* @param	InstancePtr is a pointer to the Vphy core instance.
* @param	QuadId is the GT quad ID to operate on.
* @param	ChId is the channel ID to operate on.
*
* @return
*		- A pointer to the HDMI GT characteristics.
*		- NULL if the GT type is unsupported.
*
* @note		None.
*
******************************************************************************/
static const XVphy_GtHdmiChars *GetGtHdmiPtr(XVphy *InstancePtr)
{
	/* Suppress Warning Messages */
	InstancePtr = InstancePtr;

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
	return &Gtxe2HdmiChars;
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE2)
	return &Gthe2HdmiChars;
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTPE2)
	return &Gtpe2HdmiChars;
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE3)
	return &Gthe3HdmiChars;
#elif (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTHE4)
	return &Gthe4HdmiChars;
#endif

	return NULL;
}


#endif
