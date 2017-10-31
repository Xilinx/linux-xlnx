/******************************************************************************
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
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xhdcp1x_cipher_intr.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This file contains interrupt related functions for Xilinx HDCP core.
* Please see xhdcp1x_cipher.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 4.1   yas    08/03/17 Updated the XHdcp1x_CipherHandleInterrupt function to
*                       not mask the interrupts, as it is being done in
*                       hardware now.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xhdcp1x_hw.h"
#include "xhdcp1x_cipher.h"
#include "xil_types.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function installs an asynchronous callback function for the given
* HandlerType:
*
* <pre>
* HandlerType                             Callback Function Type
* -----------------------------------     -----------------------------------
* (XHDCP1X_CIPHER_HANDLER_LINK_FAILURE)   LinkFailCallback
* (XHDCP1X_CIPHER_HANDLER_Ri_UPDATE)      RiUpdateCallback
* </pre>
*
* @param	InstancePtr is a pointer to the HDCP cipher core instance.
* @param	HandlerType specifies the type of handler.
* @param	CallbackFunc is the address of the callback function.
* @param	CallbackRef is a user data item that will be passed to the
*		callback function when it is invoked.
*
* @return
*		- XST_SUCCESS if callback function installed successfully.
*		- XST_INVALID_PARAM when HandlerType is invalid.
*
* @note		Invoking this function for a handler that already has been
*		installed replaces it with the new handler.
*
******************************************************************************/
int XHdcp1x_CipherSetCallback(XHdcp1x *InstancePtr, u32 HandlerType,
                XHdcp1x_Callback CallbackFunc, void *CallbackRef)
{
	u32 Status = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType >= XHDCP1X_CIPHER_HANDLER_LINK_FAILURE);
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	/* Check for handler type */
	switch (HandlerType) {
		/* Link Failure Callback */
		case (XHDCP1X_CIPHER_HANDLER_LINK_FAILURE):
			InstancePtr->Cipher.LinkFailCallback = CallbackFunc;
			InstancePtr->Cipher.LinkFailRef = CallbackRef;
			InstancePtr->Cipher.IsLinkFailCallbackSet = (TRUE);
			break;

		/* Ri Update Callback */
		case (XHDCP1X_CIPHER_HANDLER_Ri_UPDATE):
			InstancePtr->Cipher.RiUpdateCallback = CallbackFunc;
			InstancePtr->Cipher.RiUpdateRef = CallbackRef;
			InstancePtr->Cipher.IsRiUpdateCallbackSet = (TRUE);
			break;

		default:
			Status = (XST_INVALID_PARAM);
			break;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function enables/disables the reporting of link check state changes.
*
* @param	InstancePtr is the cipher core instance.
* @param	IsEnabled enables/disables link state change notifications.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetLinkStateCheck(XHdcp1x *InstancePtr, int IsEnabled)
{
	int Status = XST_SUCCESS;

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check DP receive */
	if (XHdcp1x_IsDP(InstancePtr) && XHdcp1x_IsRX(InstancePtr)) {
		u32 Val = 0;

		/* Clear any pending link state failure interrupt */
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL);

		/* Update it */
		Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK);
		if (IsEnabled) {
			Val &= ~XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL;
		}
		else {
			Val |= XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL;
		}
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK, Val);
	}
	else {
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function enables/disables the reporting of Ri update notifications.
*
* @param	InstancePtr is the cipher core instance.
* @param	IsEnabled enables/disables Ri update notifications.
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
int XHdcp1x_CipherSetRiUpdate(XHdcp1x *InstancePtr, int IsEnabled)
{
	int Status = XST_SUCCESS;

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check HDMI receive */
	if (XHdcp1x_IsHDMI(InstancePtr)) {
		u32 Val = 0;

		/* Clear any pending link state failure interrupt */
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_STATUS,
			XHDCP1X_CIPHER_BITMASK_INTERRUPT_Ri_UPDATE);

		/* Update theDevice */
		Val = XHdcp1x_ReadReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK);
		if (IsEnabled) {
			Val &= ~XHDCP1X_CIPHER_BITMASK_INTERRUPT_Ri_UPDATE;
		}
		else {
			Val |= XHDCP1X_CIPHER_BITMASK_INTERRUPT_Ri_UPDATE;
		}
		XHdcp1x_WriteReg(InstancePtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK, Val);
	}
	else {
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function is the interrupt handler for the cipher core driver.
*
* @param	InstancePtr is the cipher core instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp1x_CipherHandleInterrupt(void *InstancePtr)
{
	XHdcp1x *HdcpPtr = InstancePtr;
	u32 Pending = 0;

	/* Verify arguments */
	Xil_AssertVoid(HdcpPtr != NULL);
	Xil_AssertVoid(HdcpPtr->IsReady == XIL_COMPONENT_IS_READY);

	/* Determine Pending */
	Pending = XHdcp1x_ReadReg(HdcpPtr->Config.BaseAddress,
		XHDCP1X_CIPHER_REG_INTERRUPT_STATUS);

	/* Check for pending */
	if (Pending != 0) {
		/* Clear Pending */
		XHdcp1x_WriteReg(HdcpPtr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_STATUS, Pending);

		/* Update statistics */
		HdcpPtr->Cipher.Stats.IntCount++;

		/* Check for link integrity failure */
		if (Pending & XHDCP1X_CIPHER_BITMASK_INTERRUPT_LINK_FAIL) {
			/* Invoke callback if set */
			if (HdcpPtr->Cipher.IsLinkFailCallbackSet) {
				(*HdcpPtr->Cipher.LinkFailCallback)(
						HdcpPtr->Cipher.LinkFailRef);
			}
		}

		/* Check for change to Ri register */
		if (Pending & XHDCP1X_CIPHER_BITMASK_INTERRUPT_Ri_UPDATE) {
			/* Invoke callback if set */
			if (HdcpPtr->Cipher.IsRiUpdateCallbackSet) {
				(*HdcpPtr->Cipher.RiUpdateCallback)(
						HdcpPtr->Cipher.RiUpdateRef);
			}
		}
	}
}
/** @} */
