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
 * @file xvphy_intr.c
 *
 * This file contains functions related to XVphy interrupt handling.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   als  10/19/15 Initial release.
 * 1.4   gm   29/11/16 Added XVphy_CfgErrIntr for ERR_IRQ impl
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include "xvphy.h"
#include "xvphy_i.h"

/**************************** Function Definitions ****************************/

/******************************************************************************/
/**
 * This function enables interrupts associated with the specified interrupt type.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 * @param	Intr is the interrupt type/mask to enable.
 *
 * @return	None.
 *
 * @note	None.
*
*******************************************************************************/
void XVphy_IntrEnable(XVphy *InstancePtr, XVphy_IntrHandlerType Intr)
{
	u32 RegVal;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_EN_REG);
	RegVal |= Intr;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_EN_REG, RegVal);
}

/******************************************************************************/
/**
 * This function disabled interrupts associated with the specified interrupt
 * type.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 * @param	Intr is the interrupt type/mask to disable.
 *
 * @return	None.
 *
 * @note	None.
 *
*******************************************************************************/
void XVphy_IntrDisable(XVphy *InstancePtr, XVphy_IntrHandlerType Intr)
{
	u32 RegVal;

	RegVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_INTR_DIS_REG);
	RegVal |= Intr;
	XVphy_WriteReg(InstancePtr->Config.BaseAddr, XVPHY_INTR_DIS_REG,
			RegVal);
}

/******************************************************************************/
/**
 * This function installs a callback function for the specified handler type.
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
 * @note	None.
 *
*******************************************************************************/
void XVphy_SetIntrHandler(XVphy *InstancePtr, XVphy_IntrHandlerType HandlerType,
			XVphy_IntrHandler CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid((HandlerType == XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_QPLL_LOCK) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK) ||
		(HandlerType ==
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE) ||
		(HandlerType ==
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT) ||
		(HandlerType == XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT));
	Xil_AssertVoid(CallbackFunc != NULL);
	Xil_AssertVoid(CallbackRef != NULL);

	switch (HandlerType) {
	case XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE:
		InstancePtr->IntrTxResetDoneHandler = CallbackFunc;
		InstancePtr->IntrTxResetDoneCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE:
		InstancePtr->IntrRxResetDoneHandler = CallbackFunc;
		InstancePtr->IntrRxResetDoneCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK:
		InstancePtr->IntrCpllLockHandler = CallbackFunc;
		InstancePtr->IntrCpllLockCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_QPLL_LOCK:
		InstancePtr->IntrQpllLockHandler = CallbackFunc;
		InstancePtr->IntrQpllLockCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE:
		InstancePtr->IntrTxAlignDoneHandler = CallbackFunc;
		InstancePtr->IntrTxAlignDoneCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK:
		InstancePtr->IntrQpll1LockHandler = CallbackFunc;
		InstancePtr->IntrQpll1LockCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE:
		InstancePtr->IntrTxClkDetFreqChangeHandler =
							CallbackFunc;
		InstancePtr->IntrTxClkDetFreqChangeCallbackRef =
							CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE:
		InstancePtr->IntrRxClkDetFreqChangeHandler =
							CallbackFunc;
		InstancePtr->IntrRxClkDetFreqChangeCallbackRef =
							CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT:
		InstancePtr->IntrTxTmrTimeoutHandler = CallbackFunc;
		InstancePtr->IntrTxTmrTimeoutCallbackRef = CallbackRef;
		break;
	case XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT:
		InstancePtr->IntrRxTmrTimeoutHandler = CallbackFunc;
		InstancePtr->IntrRxTmrTimeoutCallbackRef = CallbackRef;
		break;
	default:
		break;
	}
}

/******************************************************************************/
/**
 * This function is the interrupt handler for the XVphy driver. It will detect
 * what kind of interrupt has happened, and will invoke the appropriate callback
 * function.
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 *
 * @return	None.
 *
 * @note	None.
 *
*******************************************************************************/
void XVphy_InterruptHandler(XVphy *InstancePtr)
{
	u32 IntrStatus;

	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	/* Determine what kind of interrupts have occurred. */
	IntrStatus = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_INTR_STS_REG);

	if (IntrStatus & XVPHY_INTR_CPLL_LOCK_MASK) {
		InstancePtr->IntrCpllLockHandler(
				InstancePtr->IntrCpllLockCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_QPLL_LOCK_MASK) {
		InstancePtr->IntrQpllLockHandler(
				InstancePtr->IntrQpllLockCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_QPLL1_LOCK_MASK) {
		InstancePtr->IntrQpll1LockHandler(
				InstancePtr->IntrQpll1LockCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_TXRESETDONE_MASK) {
		InstancePtr->IntrTxResetDoneHandler(
				InstancePtr->IntrTxResetDoneCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_TXALIGNDONE_MASK) {
		InstancePtr->IntrTxAlignDoneHandler(
				InstancePtr->IntrTxAlignDoneCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_RXRESETDONE_MASK) {
		InstancePtr->IntrRxResetDoneHandler(
				InstancePtr->IntrRxResetDoneCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_TXCLKDETFREQCHANGE_MASK) {
		InstancePtr->IntrTxClkDetFreqChangeHandler(
				InstancePtr->IntrTxClkDetFreqChangeCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_RXCLKDETFREQCHANGE_MASK) {
		InstancePtr->IntrRxClkDetFreqChangeHandler(
				InstancePtr->IntrRxClkDetFreqChangeCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_TXTMRTIMEOUT_MASK) {
		InstancePtr->IntrTxTmrTimeoutHandler(
				InstancePtr->IntrTxTmrTimeoutCallbackRef);
	}
	if (IntrStatus & XVPHY_INTR_RXTMRTIMEOUT_MASK) {
		InstancePtr->IntrRxTmrTimeoutHandler(
				InstancePtr->IntrRxTmrTimeoutCallbackRef);
	}
}

/******************************************************************************/
/**
 * This function configures the error IRQ register based on the condition
 * to generate an ERR_IRQ event
 *
 * @param	InstancePtr is a pointer to the XVphy instance.
 *          ErrIrq is the IRQ type as define in XVphy_ErrIrqType
 *          Set is the flag to set or clear the ErrIrq param
 *
 * @return	None.
 *
 * @note	None.
 *
*******************************************************************************/
void XVphy_CfgErrIntr(XVphy *InstancePtr, XVphy_ErrIrqType ErrIrq, u8 Set)
{
	u32 ErrIrqVal;
	u32 WriteVal;

	ErrIrqVal = XVphy_ReadReg(InstancePtr->Config.BaseAddr,
			XVPHY_ERR_IRQ);

	WriteVal = (u32)ErrIrq;

	if (Set) {
		ErrIrqVal |= WriteVal;
	}
	else {
		ErrIrqVal &= ~WriteVal;
	}

	XVphy_WriteReg(InstancePtr->Config.BaseAddr,
				XVPHY_ERR_IRQ, ErrIrqVal);
}
