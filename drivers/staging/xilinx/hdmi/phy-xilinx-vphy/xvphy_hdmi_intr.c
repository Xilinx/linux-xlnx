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
 * @file xvphy_hdmi_intr.c
 *
 * This file contains video PHY functionality specific to the HDMI protocol
 * related to interrupts.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm   10/19/15 Initial release.
 * 1.1   gm   02/01/16 Added GTPE2 and GTHE4 support
 *                     Added XVphy_HdmiGtpPllLockHandler for GTPE2
 * 1.2   gm            Replaced xil_printf with log events for debugging
 * 1.3   gm   01/11/16 Fixed rounding of RX refclk frequency
 *                     Fixed race condition in
 *                       XVphy_HdmiRxClkDetFreqChangeHandler when  storing
 *                       RxRefClkHz value
 * 1.4   gm   29/11/16 Added preprocessor directives for sw footprint reduction
 *                     Incorporated AXIlite Freq auto extraction
 *                     Added logging and register access for ERR_IRQ impl
 *                     Added mechanism to re-trigger GT TX reset when TX align
 *                       get stuck
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
#include "xvphy_gt.h"

/************************** Function Prototypes ******************************/

extern void XVphy_Ch2Ids(XVphy *InstancePtr, XVphy_ChannelId ChId,
		u8 *Id0, u8 *Id1);

static void XVphy_HdmiGtHandler(XVphy *InstancePtr);
static void XVphy_ClkDetHandler(XVphy *InstancePtr);

/**************************** Function Definitions ****************************/

/******************************************************************************/
/**
* This function installs an HDMI callback function for the specified handler
* type
*
* @param	InstancePtr is a pointer to the XVPhy instance.
* @param	HandlerType is the interrupt handler type which specifies which
*		interrupt event to attach the callback for.
* @param	CallbackFunc is the address to the callback function.
* @param	CallbackRef is the user data item that will be passed to the
*		callback function when it is invoked.
*
* @return	None.
*
* @note		None.
*
*******************************************************************************/
void XVphy_SetHdmiCallback(XVphy *InstancePtr,
		XVphy_HdmiHandlerType HandlerType,
		void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid((HandlerType == XVPHY_HDMI_HANDLER_TXINIT) ||
			(HandlerType == XVPHY_HDMI_HANDLER_TXREADY) ||
			(HandlerType == XVPHY_HDMI_HANDLER_RXINIT) ||
			(HandlerType == XVPHY_HDMI_HANDLER_RXREADY));
	Xil_AssertVoid(CallbackFunc != NULL);
	Xil_AssertVoid(CallbackRef != NULL);

	switch (HandlerType) {
	case XVPHY_HDMI_HANDLER_TXINIT:
		InstancePtr->HdmiTxInitCallback = (XVphy_Callback)CallbackFunc;
		InstancePtr->HdmiTxInitRef = CallbackRef;
		break;

	case XVPHY_HDMI_HANDLER_TXREADY:
		InstancePtr->HdmiTxReadyCallback = (XVphy_Callback)CallbackFunc;
		InstancePtr->HdmiTxReadyRef = CallbackRef;
		break;

	case XVPHY_HDMI_HANDLER_RXINIT:
		InstancePtr->HdmiRxInitCallback = (XVphy_Callback)CallbackFunc;
		InstancePtr->HdmiRxInitRef = CallbackRef;
		break;

	case XVPHY_HDMI_HANDLER_RXREADY:
		InstancePtr->HdmiRxReadyCallback = (XVphy_Callback)CallbackFunc;
		InstancePtr->HdmiRxReadyRef = CallbackRef;
		break;

	default:
		break;
	}
}

/*****************************************************************************/
/**
* This function sets the appropriate HDMI interupt handlers.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiIntrHandlerCallbackInit(XVphy *InstancePtr)
{
	/* GT Interrupts */
	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL_LOCK,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK,
			(XVphy_IntrHandler)XVphy_HdmiGtHandler, InstancePtr);

	/* Clock Detector Interrupts */
	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE,
			(XVphy_IntrHandler)XVphy_ClkDetHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE,
			(XVphy_IntrHandler)XVphy_ClkDetHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT,
			(XVphy_IntrHandler)XVphy_ClkDetHandler, InstancePtr);

	XVphy_SetIntrHandler(InstancePtr,
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT,
			(XVphy_IntrHandler)XVphy_ClkDetHandler, InstancePtr);
}

#if (XPAR_VPHY_0_TRANSCEIVER != XVPHY_GTPE2)
/*****************************************************************************/
/**
* This function is the handler for events triggered by QPLL lock done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiQpllLockHandler(XVphy *InstancePtr)
{
	XVphy_PllType TxPllType;
	XVphy_PllType RxPllType;
	u8 Id, Id0, Id1;
	XVphy_ChannelId ChId;

	/* Determine PLL type. */
	TxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
		XVPHY_CHANNEL_ID_CH1);
	RxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
		XVPHY_CHANNEL_ID_CH1);

	/* RX is using QPLL. */
	if ((RxPllType == XVPHY_PLL_TYPE_QPLL) ||
			(RxPllType == XVPHY_PLL_TYPE_QPLL0) ||
			(RxPllType == XVPHY_PLL_TYPE_QPLL1) ||
			(RxPllType == XVPHY_PLL_TYPE_PLL0)  ||
			(RxPllType == XVPHY_PLL_TYPE_PLL1)) {

		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, RxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_QPLL_LOCK, 1);

			/* GT RX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_RX, FALSE);

			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0,
					&Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					RxState = XVPHY_GT_STATE_RESET;
			}

			/* If the GT TX and RX are coupled, then update the GT
			 * TX state as well. */
			if (XVphy_IsBonded(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CH1)) {
				/* GT TX reset. */
				XVphy_ResetGtTxRx(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CHA,
						XVPHY_DIR_TX, TRUE);

				XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
						&Id0, &Id1);
				for (Id = Id0; Id <= Id1; Id++) {
					InstancePtr->Quads[0].Plls[
						XVPHY_CH2IDX(Id)].
						TxState = XVPHY_GT_STATE_RESET;
				}
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_QPLL_LOCK, 0);
		}
	}
	/* TX is using QPLL. */
	else {
		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_TX, TxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_QPLL_LOCK, 1);

			/* GT TX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_TX, FALSE);

			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0,
					&Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					TxState = XVPHY_GT_STATE_RESET;
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_QPLL_LOCK, 0);
		}
	}
}
#endif

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTPE2)
/*****************************************************************************/
/**
* This function is the handler for events triggered by GTP PLL0/1 lock done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiGtpPllLockHandler(XVphy *InstancePtr, u8 Pll)
{
	XVphy_PllType TxPllType;
	XVphy_PllType RxPllType;
	u8 Id, Id0, Id1;
	XVphy_ChannelId ChId;

	/* Determine PLL type. */
	TxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
			XVPHY_CHANNEL_ID_CH1);
	RxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
			XVPHY_CHANNEL_ID_CH1);

	/* RX is using QPLL. */
	if (((RxPllType == XVPHY_PLL_TYPE_PLL0) && (Pll == 0)) ||
	    ((RxPllType == XVPHY_PLL_TYPE_PLL1) && (Pll == 1))) {

		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, RxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, (Pll == 0) ?
				XVPHY_LOG_EVT_PLL0_LOCK :
				XVPHY_LOG_EVT_PLL1_LOCK, 1);

			/* GT RX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_RX, FALSE);

			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0,
					&Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					RxState = XVPHY_GT_STATE_RESET;
			}

			/* If the GT TX and RX are coupled, then update the GT
			 * TX state as well. */
			if (XVphy_IsBonded(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CH1)) {
				/* GT TX reset. */
				XVphy_ResetGtTxRx(InstancePtr, 0,
					XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_TX, TRUE);

				XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA,
						&Id0, &Id1);
				for (Id = Id0; Id <= Id1; Id++) {
					InstancePtr->Quads[0].Plls[
						XVPHY_CH2IDX(Id)].TxState =
							XVPHY_GT_STATE_RESET;
				}
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, (Pll == 0) ?
					XVPHY_LOG_EVT_PLL0_LOCK :
					XVPHY_LOG_EVT_PLL1_LOCK, 0);
		}
	}
	/* TX is using QPLL. */
	else {
		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_TX, TxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, (Pll == 0) ?
					XVPHY_LOG_EVT_PLL0_LOCK :
					XVPHY_LOG_EVT_PLL1_LOCK, 1);

			/* GT TX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_TX, FALSE);

			XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0,
					&Id1);
			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					TxState = XVPHY_GT_STATE_RESET;
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, (Pll == 0) ?
					XVPHY_LOG_EVT_PLL0_LOCK :
					XVPHY_LOG_EVT_PLL1_LOCK, 0);
		}
	}
}
#endif

#if (XPAR_VPHY_0_TRANSCEIVER != XVPHY_GTPE2)
/*****************************************************************************/
/**
* This function is the handler for events triggered by CPLL lock done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiCpllLockHandler(XVphy *InstancePtr)
{
	XVphy_PllType TxPllType;
	XVphy_PllType RxPllType;
	u8 Id, Id0, Id1;
	XVphy_ChannelId ChId;

	/* Determine PLL type. */
	TxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
			XVPHY_CHANNEL_ID_CH1);
	RxPllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
			XVPHY_CHANNEL_ID_CH1);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);

	/* RX is using CPLL. */
	if (RxPllType == XVPHY_PLL_TYPE_CPLL) {
		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, RxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_CPLL_LOCK, 1);
			/* GT RX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_RX, FALSE);

			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					RxState = XVPHY_GT_STATE_RESET;
			}

			/* If the GT TX and RX are coupled, then update the GT
			 * TX state as well. */
			if (XVphy_IsBonded(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CH1)) {
				/* GT TX reset. */
				XVphy_ResetGtTxRx(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CHA,
						XVPHY_DIR_TX, TRUE);

				for (Id = Id0; Id <= Id1; Id++) {
					InstancePtr->Quads[0].Plls[
						XVPHY_CH2IDX(Id)].
						TxState = XVPHY_GT_STATE_RESET;
				}
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_CPLL_LOCK, 0);
		}
	}
	/* TX is using CPLL. */
	else {
		/* Determine which channel(s) to operate on. */
		ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_TX, TxPllType);

		if (XVphy_IsPllLocked(InstancePtr, 0, ChId) == XST_SUCCESS) {
			/* Log, lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_CPLL_LOCK, 1);
			/* GT TX reset. */
			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
					XVPHY_DIR_TX, FALSE);

			for (Id = Id0; Id <= Id1; Id++) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					TxState = XVPHY_GT_STATE_RESET;
			}
		}
		else {
			/* Log, Lost lock */
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_CPLL_LOCK, 0);
		}
	}
}
#endif

/*****************************************************************************/
/**
* This function is the handler for events triggered by GT TX reset lock done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiGtTxResetDoneLockHandler(XVphy *InstancePtr)
{
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_TX_RST_DONE, 0);

	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) ||
            (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE4)) {
		XVphy_TxAlignReset(InstancePtr, XVPHY_CHANNEL_ID_CHA, TRUE);
		XVphy_TxAlignReset(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);
	}

	/* GT alignment. */
	XVphy_TxAlignStart(InstancePtr, XVPHY_CHANNEL_ID_CHA, TRUE);
	XVphy_TxAlignStart(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
			InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
				XVPHY_GT_STATE_READY;

			/* TX ready callback. */
			if (InstancePtr->HdmiTxReadyCallback) {
				InstancePtr->HdmiTxReadyCallback(
						InstancePtr->HdmiTxReadyRef);
			}
		} else {
#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
			XVphy_ClkDetTimerLoad(InstancePtr, 0, XVPHY_DIR_TX,
							InstancePtr->Config.AxiLiteClkFreq/100);
#endif

			InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
				XVPHY_GT_STATE_ALIGN;
		}
	}
}

/*****************************************************************************/
/**
* This function is the handler for events triggered by GT TX alignment done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiGtTxAlignDoneLockHandler(XVphy *InstancePtr)
{
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_TX_ALIGN, 1);

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
	/* Clear TX timer. */
	XVphy_ClkDetTimerClear(InstancePtr, 0, XVPHY_DIR_TX);
#endif


	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
			XVPHY_GT_STATE_READY;
	}

	/* TX ready callback. */
	if (InstancePtr->HdmiTxReadyCallback) {
		InstancePtr->HdmiTxReadyCallback(InstancePtr->HdmiTxReadyRef);
	}
}

/*****************************************************************************/
/**
* This function is the handler for events triggered by GT RX reset lock done.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiGtRxResetDoneLockHandler(XVphy *InstancePtr)
{
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_RX_RST_DONE, 0);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].RxState =
			XVPHY_GT_STATE_READY;
	}

	/* If DRU is use/d, release its reset. */
	if (InstancePtr->HdmiRxDruIsEnabled) {
		XVphy_DruReset(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);
	}

	/* RX ready callback. */
	if (InstancePtr->HdmiRxReadyCallback) {
		InstancePtr->HdmiRxReadyCallback(InstancePtr->HdmiRxReadyRef);
	}

	/* If the GT TX and RX are coupled, then update the GT TX state
	 * as well. */
	if (XVphy_IsBonded(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1)) {
		/* GT TX reset. */
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, FALSE);

		for (Id = Id0; Id <= Id1; Id++) {
			InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
				XVPHY_GT_STATE_RESET;
		}
	}
}

/*****************************************************************************/
/**
* This function is the handler for events triggered by a change in TX frequency
* as detected by the HDMI clock detector logic.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiTxClkDetFreqChangeHandler(XVphy *InstancePtr)
{
	XVphy_PllType PllType;
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_TX_FREQ, 0);

	/* Determine PLL type. */
	PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
			XVPHY_CHANNEL_ID_CH1);

	/* Assert GT TX reset. */
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, TRUE);
	}

	/* If the TX frequency has changed, the PLL is always disabled. */
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_PowerDownGtPll(InstancePtr, 0,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_CHANNEL_ID_CHA : XVPHY_CHANNEL_ID_CMNA, TRUE);
	}
	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX,
			TRUE);
	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
		XVphy_GtUserRdyEnable(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
		XVPHY_DIR_TX, FALSE);
	}

	/* Mask the MMCM Lock */
	XVphy_MmcmLockedMaskEnable(InstancePtr, 0, XVPHY_DIR_TX, TRUE);

	/* Disable TX MMCM. */
	//XVphy_MmcmPowerDown(InstancePtr, 0, XVPHY_DIR_TX, TRUE);

	/* Clear TX timer. */
	XVphy_ClkDetTimerClear(InstancePtr, 0, XVPHY_DIR_TX);

	/* Clear GT alignment. */
	XVphy_TxAlignStart(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);

	/* De-assert GT TX reset. */
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, FALSE);
	}

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
			XVPHY_GT_STATE_IDLE;
	}

	/* If there is no reference clock, load TX timer in usec. */
	if (XVphy_ClkDetGetRefClkFreqHz(InstancePtr, XVPHY_DIR_TX)) {
		XVphy_ClkDetTimerLoad(InstancePtr, 0, XVPHY_DIR_TX,
						InstancePtr->Config.AxiLiteClkFreq/1000);
	}

	/* Callback to re-initialize. */
	if (InstancePtr->HdmiTxInitCallback) {
		InstancePtr->HdmiTxInitCallback(InstancePtr->HdmiTxInitRef);
	}
}

/*****************************************************************************/
/**
* This function is the handler for events triggered by a change in RX frequency
* as detected by the HDMI clock detector logic.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiRxClkDetFreqChangeHandler(XVphy *InstancePtr)
{
	XVphy_PllType PllType;
	u32 RxRefClkHz;
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_RX_FREQ, 0);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].RxState =
			XVPHY_GT_STATE_IDLE;
	}

	/* Mask the MMCM Lock */
	XVphy_MmcmLockedMaskEnable(InstancePtr, 0, XVPHY_DIR_RX, TRUE);

	/* Determine PLL type and RX reference clock selection. */
	PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
			XVPHY_CHANNEL_ID_CH1);

	/* Fetch New RX Reference Clock Frequency */
	RxRefClkHz = XVphy_ClkDetGetRefClkFreqHz(InstancePtr, XVPHY_DIR_RX);

	/* Round input frequency to 10 kHz. */
	RxRefClkHz = (RxRefClkHz+5000) / 10000;
	RxRefClkHz = RxRefClkHz * 10000;

	/* Store RX reference clock. */
	InstancePtr->HdmiRxRefClkHz = RxRefClkHz;

	/* If the RX frequency has changed, the PLL is always disabled. */
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_PowerDownGtPll(InstancePtr, 0,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_CHANNEL_ID_CHA : XVPHY_CHANNEL_ID_CMNA, TRUE);
	}

	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_RX,
			TRUE);

	/* When the GT TX and RX are coupled, then disable the QPLL. */
	if (XVphy_IsBonded(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1)) {
		XVphy_PowerDownGtPll(InstancePtr, 0,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_CHANNEL_ID_CMNA : XVPHY_CHANNEL_ID_CHA, TRUE);
		XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX, 1);
//		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
//			XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
//					XVPHY_DIR_TX, TRUE);
//		}

	}

	/* Disable RX MMCM */
	//XVphy_MmcmPowerDown(InstancePtr, 0, XVPHY_DIR_RX, TRUE);
	/* When the GT TX and RX are coupled, then disable the TX MMCM. */
	//if (XVphy_IsBonded(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1)) {
	//	XVphy_MmcmPowerDown(InstancePtr, 0, XVPHY_DIR_TX, TRUE);
	//}

	/* Assert GT RX reset */
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_RX, FALSE);
	}

	/* If DRU is present, disable it and assert reset. */
	if (InstancePtr->Config.DruIsPresent) {
		XVphy_DruReset(InstancePtr, XVPHY_CHANNEL_ID_CHA, TRUE);
		XVphy_DruEnable(InstancePtr, XVPHY_CHANNEL_ID_CHA, FALSE);
	}

	/* Clear RX timer. */
	XVphy_ClkDetTimerClear(InstancePtr, 0, XVPHY_DIR_RX);

	/* If there is reference clock, load RX timer in usec.
	 * The reference clock should be larger than 25Mhz. We are using a 20Mhz
	 * instead to keep some margin for errors. */
	if (RxRefClkHz > 20000000) {
		XVphy_ClkDetTimerLoad(InstancePtr, 0, XVPHY_DIR_RX,
						InstancePtr->Config.AxiLiteClkFreq/1000);

		/* Callback to re-initialize. */
		if (InstancePtr->HdmiRxInitCallback) {
			InstancePtr->HdmiRxInitCallback(InstancePtr->HdmiRxInitRef);
		}
	}
}

/*****************************************************************************/
/**
* This function is the handler for TX timer timeout events.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiTxTimerTimeoutHandler(XVphy *InstancePtr)
{
	XVphy_ChannelId ChId;
	XVphy_PllType PllType;
	u8 Id, Id0, Id1;

#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTXE2)
	/* Check if timer timed out while waiting for TX Alignment
	 * If yes, reset the GT TX
	 */
	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	if (InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id0)].TxState ==
			XVPHY_GT_STATE_ALIGN) {
		XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_TX_ALIGN_TMOUT, 1);
		/* GT TX reset. */
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, FALSE);

		XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0,
				&Id1);
		for (Id = Id0; Id <= Id1; Id++) {
			InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
				TxState = XVPHY_GT_STATE_RESET;
		}
		return;
	}
#endif

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_TX_TMR, 1);

	/* Determine PLL type. */
	PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_TX,
			XVPHY_CHANNEL_ID_CH1);
	/* Determine which channel(s) to operate on. */
	ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_TX, PllType);

	/* Start TX MMCM. */
	XVphy_MmcmStart(InstancePtr, 0, XVPHY_DIR_TX);

	/* Enable PLL. */
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_PowerDownGtPll(InstancePtr, 0,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_CHANNEL_ID_CHA : XVPHY_CHANNEL_ID_CMNA, FALSE);
	}

	if (PllType != XVPHY_PLL_TYPE_CPLL) {
		/* Set QPLL Selection in PIO. */
		XVphy_WriteCfgRefClkSelReg(InstancePtr, 0);
	}

	XVphy_ClkReconfig(InstancePtr, 0, ChId);
	XVphy_OutDivReconfig(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX);
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE4)) {
		XVphy_SetBufgGtDiv(InstancePtr, XVPHY_DIR_TX,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			InstancePtr->Quads[0].Plls[0].TxOutDiv :
			InstancePtr->Quads[0].Plls[0].TxOutDiv / 2);
	}
	XVphy_DirReconfig(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_TX);

	/* Assert PLL reset. */
	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX, TRUE);

	/* Assert GT TX reset. */
	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2)) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
							XVPHY_DIR_TX, TRUE);
	}

	/* De-assert PLL reset. */
	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_TX, FALSE);

	if ((InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE3) ||
	    (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTHE4)) {
		/* Clear GT alignment. */
		XVphy_TxAlignStart(InstancePtr, ChId, FALSE);
	}

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].TxState =
			XVPHY_GT_STATE_LOCK;
	}
}

/*****************************************************************************/
/**
* This function is the handler for RX timer timeout events.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiRxTimerTimeoutHandler(XVphy *InstancePtr)
{
	XVphy_ChannelId ChId;
	XVphy_PllType PllType;
	u32 Status;
	u8 Id, Id0, Id1;

	XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_RX_TMR, 1);

	/* Determine PLL type. */
	PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
			XVPHY_CHANNEL_ID_CH1);
	/* Determine which channel(s) to operate on. */
	ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, PllType);

	XVphy_Ch2Ids(InstancePtr, XVPHY_CHANNEL_ID_CHA, &Id0, &Id1);

	/* Set RX parameters. */
	Status = XVphy_SetHdmiRxParam(InstancePtr, 0, ChId);
	if (Status != XST_SUCCESS) {
		if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_GT_PLL_LAYOUT, 1);
			XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_PLL_LAYOUT, 1);
		}

		for (Id = Id0; Id <= Id1; Id++) {
			InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].RxState =
				XVPHY_GT_STATE_IDLE;
			if (XVphy_IsBonded(InstancePtr, 0,
						XVPHY_CHANNEL_ID_CH1)) {
				InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].
					TxState = XVPHY_GT_STATE_IDLE;
			}
		}

		return;
	}
	else {
		XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_PLL_LAYOUT, 0);
	}

	/* Enable PLL. */
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_PowerDownGtPll(InstancePtr, 0,
			(PllType == XVPHY_PLL_TYPE_CPLL) ?
			XVPHY_CHANNEL_ID_CHA : XVPHY_CHANNEL_ID_CMNA, FALSE);
	}

	/* Enable DRU to set the clock muxes. */
	XVphy_DruEnable(InstancePtr, XVPHY_CHANNEL_ID_CHA,
			InstancePtr->HdmiRxDruIsEnabled);

	/* Update reference clock election. */
	if (InstancePtr->Config.XcvrType != XVPHY_GT_TYPE_GTPE2) {
		XVphy_CfgPllRefClkSel(InstancePtr, 0,
			((PllType == XVPHY_PLL_TYPE_CPLL) ?
				XVPHY_CHANNEL_ID_CHA : XVPHY_CHANNEL_ID_CMNA),
			((InstancePtr->HdmiRxDruIsEnabled) ?
				InstancePtr->Config.DruRefClkSel :
				InstancePtr->Config.RxRefClkSel));
	}
	/* GTP */
	else {
		XVphy_CfgPllRefClkSel(InstancePtr, 0,
			((PllType == XVPHY_PLL_TYPE_PLL0) ?
				XVPHY_CHANNEL_ID_CMN0 : XVPHY_CHANNEL_ID_CMN1),
			((InstancePtr->HdmiRxDruIsEnabled) ?
				InstancePtr->Config.DruRefClkSel :
				InstancePtr->Config.RxRefClkSel));
	}

	/* Update GT DRU mode. */
	XVphy_HdmiGtDruModeEnable(InstancePtr, InstancePtr->HdmiRxDruIsEnabled);

	/* Update RefClk selection. */
	XVphy_WriteCfgRefClkSelReg(InstancePtr, 0);

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTPE2) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_RX, TRUE);
        /* GTP Wizard locks the DRP access to its internal FSM during reset */
		/* Wait for reset sequence to release DRP port. */
		XVphy_WaitUs(InstancePtr, 5000);
	}

	/* Determine PLL type. */
	PllType = XVphy_GetPllType(InstancePtr, 0, XVPHY_DIR_RX,
			XVPHY_CHANNEL_ID_CH1);
	/* Determine which channel(s) to operate on. */
	ChId = XVphy_GetRcfgChId(InstancePtr, 0, XVPHY_DIR_RX, PllType);

	XVphy_ClkReconfig(InstancePtr, 0, ChId);
	XVphy_OutDivReconfig(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_RX);
	if (XVphy_IsBonded(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1)) {
		XVphy_OutDivReconfig(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX);
	}

	XVphy_DirReconfig(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_RX);

	/* Assert RX PLL reset. */
	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA, XVPHY_DIR_RX,
			TRUE);

	if (InstancePtr->Config.XcvrType == XVPHY_GT_TYPE_GTXE2) {
		XVphy_ResetGtTxRx(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_RX, TRUE);
	}

	/* De-assert RX PLL reset. */
	XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
			XVPHY_DIR_RX, FALSE);

	/* When the TX and RX are coupled, clear GT alignment. */
	if (XVphy_IsBonded(InstancePtr, 0, XVPHY_CHANNEL_ID_CH1)) {
		if (InstancePtr->HdmiRxDruIsEnabled) {
			XVphy_LogWrite(InstancePtr, XVPHY_LOG_EVT_GT_UNBONDED, 1);
			XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_PLL_LAYOUT, 1);
		}
		else {
			XVphy_CfgErrIntr(InstancePtr, XVPHY_ERRIRQ_PLL_LAYOUT, 0);
		}
		XVphy_ResetGtPll(InstancePtr, 0, XVPHY_CHANNEL_ID_CHA,
				XVPHY_DIR_TX, 0);
		XVphy_TxAlignStart(InstancePtr, ChId, FALSE);
	}
	for (Id = Id0; Id <= Id1; Id++) {
		InstancePtr->Quads[0].Plls[XVPHY_CH2IDX(Id)].RxState =
			XVPHY_GT_STATE_LOCK;
	}
}

/*****************************************************************************/
/**
* This function is the interrupt handler for the GT events.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_HdmiGtHandler(XVphy *InstancePtr)
{
	u32 Event;
	u32 EventMask;
	u32 EventAck;
	XVphy_GtState *TxStatePtr;
	XVphy_GtState *RxStatePtr;

	EventMask = XVPHY_INTR_QPLL0_LOCK_MASK | XVPHY_INTR_CPLL_LOCK_MASK |
		XVPHY_INTR_QPLL1_LOCK_MASK | XVPHY_INTR_TXRESETDONE_MASK |
		XVPHY_INTR_TXALIGNDONE_MASK | XVPHY_INTR_RXRESETDONE_MASK;

	u8 QuadId = 0;

	/* Read Interrupt Status register */
	Event = XVphy_ReadReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_STS_REG);

	EventAck = EventMask & Event;

	/* Read States for Quad=0 Ch1 */
	TxStatePtr = &InstancePtr->Quads[QuadId].Ch1.TxState;
	RxStatePtr = &InstancePtr->Quads[QuadId].Ch1.RxState;

	if ((Event & XVPHY_INTR_QPLL0_LOCK_MASK) ||
	    (Event & XVPHY_INTR_QPLL1_LOCK_MASK)) {
#if (XPAR_VPHY_0_TRANSCEIVER == XVPHY_GTPE2)
		if (Event & XVPHY_INTR_QPLL0_LOCK_MASK) { /* PLL0. */
			XVphy_HdmiGtpPllLockHandler(InstancePtr, 0);
		}
		if (Event & XVPHY_INTR_QPLL1_LOCK_MASK) { /* PLL1. */
			XVphy_HdmiGtpPllLockHandler(InstancePtr, 1);
		}
#else
		XVphy_HdmiQpllLockHandler(InstancePtr);
#endif
	}
#if (XPAR_VPHY_0_TRANSCEIVER != XVPHY_GTPE2)
	if (Event & XVPHY_INTR_CPLL_LOCK_MASK) {
		XVphy_HdmiCpllLockHandler(InstancePtr);
	}
#endif
	if ((Event & XVPHY_INTR_TXRESETDONE_MASK)
			&& (*TxStatePtr == XVPHY_GT_STATE_RESET)) {
		XVphy_HdmiGtTxResetDoneLockHandler(InstancePtr);
	}
	if ((Event & XVPHY_INTR_TXALIGNDONE_MASK)
			&& (*TxStatePtr == XVPHY_GT_STATE_ALIGN)) {
		XVphy_HdmiGtTxAlignDoneLockHandler(InstancePtr);
	}
	if ((Event & XVPHY_INTR_RXRESETDONE_MASK)
			&& (*RxStatePtr == XVPHY_GT_STATE_RESET)) {
		XVphy_HdmiGtRxResetDoneLockHandler(InstancePtr);
	}

	/* Clear event flags by writing to Interrupt Status register */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_STS_REG,
			EventAck);
}

/*****************************************************************************/
/**
* This function is the interrupt handler for the clock detector events.
*
* @param	InstancePtr is a pointer to the VPHY instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XVphy_ClkDetHandler(XVphy *InstancePtr)
{
	u32 Event;
	u32 EventMask;
	u32 EventAck;

	EventMask = XVPHY_INTR_TXCLKDETFREQCHANGE_MASK |
				XVPHY_INTR_RXCLKDETFREQCHANGE_MASK |
				XVPHY_INTR_TXTMRTIMEOUT_MASK |
				XVPHY_INTR_RXTMRTIMEOUT_MASK;

	/* Read Interrupt Status register */
	Event = XVphy_ReadReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_STS_REG);

	EventAck = EventMask & Event;

	if (Event & XVPHY_INTR_TXCLKDETFREQCHANGE_MASK) {
		XVphy_HdmiTxClkDetFreqChangeHandler(InstancePtr);
	}
	if (Event & XVPHY_INTR_RXCLKDETFREQCHANGE_MASK) {
		XVphy_HdmiRxClkDetFreqChangeHandler(InstancePtr);
	}
	if (Event & XVPHY_INTR_TXTMRTIMEOUT_MASK) {
		XVphy_HdmiTxTimerTimeoutHandler(InstancePtr);
	}
	if (Event & XVPHY_INTR_RXTMRTIMEOUT_MASK) {
		XVphy_HdmiRxTimerTimeoutHandler(InstancePtr);
	}

	/* Clear event flags by writing to Interrupt Status register */
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_STS_REG,
			EventAck);
}
#endif
