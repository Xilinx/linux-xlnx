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
* Please see xhdcp_cipher.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* 3.0   yas    02/13/16 Added function XHdcp1x_SetCallBack.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"
#include "xhdcp1x_port.h"
#include "xil_types.h"
#include "xhdcp1x_tx.h"
#include "xhdcp1x_rx.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function installs callback functions for the given HandlerType.
*
* @param	InstancePtr is a pointer to the HDCP core instance.
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
int XHdcp1x_SetCallback(XHdcp1x *InstancePtr, XHdcp1x_HandlerType HandlerType,
		void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType > (XHDCP1X_HANDLER_UNDEFINED));
	Xil_AssertNonvoid(HandlerType < (XHDCP1X_HANDLER_INVALID));
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	u32 Status;

	/* Check for TX */
	if (!InstancePtr->Config.IsRx) {
		Status = XHdcp1x_TxSetCallback(InstancePtr, HandlerType,
			CallbackFunc, CallbackRef);
	}
	/* Check for RX */
	else if (InstancePtr->Config.IsRx) {
		Status = XHdcp1x_RxSetCallback(InstancePtr, HandlerType,
			CallbackFunc, CallbackRef);
	}
	else {
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
* This function is the cipher interrupt handler for the HDCP module.
*
* @param	InstancePtr is the device instance that just interrupted.
*
* @return	None.
*
* @note		This function just forwards the interrupt along to the
*		corresponding cipher core.
*
******************************************************************************/
void XHdcp1x_CipherIntrHandler(void *InstancePtr)
{
	XHdcp1x *HdcpPtr = InstancePtr;

	/* Verify arguments */
	Xil_AssertVoid(HdcpPtr != NULL);
	Xil_AssertVoid(HdcpPtr->IsReady == XIL_COMPONENT_IS_READY);

	/* Dispatch it to the corresponding cipher */
	XHdcp1x_CipherHandleInterrupt(HdcpPtr);
}

/*****************************************************************************/
/**
* This function is the port interrupt handler for the HDCP module.
*
* @param	InstancePtr is the device instance that just interrupted.
* @param	IntCause is the interrupt cause bit map.
*
* @return	None.
*
* @note		This function just forwards the interrupt along to the
*		corresponding cipher core.
*
******************************************************************************/
void XHdcp1x_PortIntrHandler(void *InstancePtr, u32 IntCause)
{
	XHdcp1x *HdcpPtr = InstancePtr;

	/* Verify arguments */
	Xil_AssertVoid(HdcpPtr != NULL);
	Xil_AssertVoid(HdcpPtr->IsReady == XIL_COMPONENT_IS_READY);

	/* Dispatch it to the corresponding port */
	XHdcp1x_PortHandleInterrupt(HdcpPtr, IntCause);
}

/** @} */
