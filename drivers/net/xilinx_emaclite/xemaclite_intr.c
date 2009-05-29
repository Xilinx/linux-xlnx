/* $Id: xemaclite_intr.c,v 1.1.2.1 2007/03/13 17:26:08 akondratenko Exp $ */
/******************************************************************************
*
*       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
*       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
*       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
*       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
*       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
*       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
*       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
*       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
*       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
*       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
*       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
*       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*       FOR A PARTICULAR PURPOSE.
*
*       (c) Copyright 2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemaclite_intr.c
*
* Functions in this file are for the interrupt driven processing functionality.
* See xemaclite.h for a detailed description of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- --------------------------------------------------------
* 1.01a ecm  03/31/04 First release
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xstatus.h"
#include "xemaclite_l.h"
#include "xemaclite_i.h"
#include "xio.h"
#include "xemaclite.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/
/*****************************************************************************/
/**
*
* Enable the EmacLite Interrupts.
*
* This function must be called before other functions to send or receive data
* in interrupt driven mode. The user should have connected the
* interrupt handler of the driver to an interrupt source such as an interrupt
* controller or the processor interrupt prior to this function being called.
*
* @param InstancePtr is a pointer to the XEmacLite instance to be worked on.
*
* @return
*
* - XST_SUCCESS if the device interrupts were enabled successfully.
* - XST_NO_CALLBACK if the callbacks were not set.
*
* @note
*
* None
*
******************************************************************************/
int XEmacLite_EnableInterrupts(XEmacLite * InstancePtr)
{
	u32 Register;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Verify that the handlers are in place
	 */

	if ((InstancePtr->RecvHandler == (XEmacLite_Handler) StubHandler) ||
	    (InstancePtr->SendHandler == (XEmacLite_Handler) StubHandler)) {
		return XST_NO_CALLBACK;
	}


	/* Enable TX and RX interrupts */

	Register = XIo_In32(InstancePtr->BaseAddress + XEL_TSR_OFFSET);
	Register |= XEL_TSR_XMIT_IE_MASK;
	XIo_Out32(InstancePtr->BaseAddress + XEL_TSR_OFFSET, Register);
	XIo_Out32(InstancePtr->BaseAddress + XEL_TSR_OFFSET +
		  XEL_BUFFER_OFFSET, Register);

	Register = XIo_In32(InstancePtr->BaseAddress + XEL_RSR_OFFSET);
	Register |= XEL_RSR_RECV_IE_MASK;
	XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET, Register);
	XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET +
		  XEL_BUFFER_OFFSET, Register);

	/* Enable the global interrupt output. */

	XIo_Out32(InstancePtr->BaseAddress + XEL_GIER_OFFSET,
		  XEL_GIER_GIE_MASK);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Disables the interrupts from the device(the higher layer software is
* responsible for disabling interrupts at the interrupt controller).
*
* To start using the device again, _EnableInterrupts must be called.
*
* @param InstancePtr is a pointer to the XEmacLite instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
void XEmacLite_DisableInterrupts(XEmacLite * InstancePtr)
{
	u32 Register;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);


	/* Disable the global interrupt output. */

	XIo_Out32(InstancePtr->BaseAddress + XEL_GIER_OFFSET, 0);

	/* Disable TX and RX interrupts */

	Register = XIo_In32(InstancePtr->BaseAddress + XEL_TSR_OFFSET);
	Register &= ~XEL_TSR_XMIT_IE_MASK;
	XIo_Out32(InstancePtr->BaseAddress + XEL_TSR_OFFSET, Register);
	XIo_Out32(InstancePtr->BaseAddress + XEL_TSR_OFFSET +
		  XEL_BUFFER_OFFSET, Register);

	Register = XIo_In32(InstancePtr->BaseAddress + XEL_RSR_OFFSET);
	Register &= ~XEL_RSR_RECV_IE_MASK;
	XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET, Register);
	XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET +
		  XEL_BUFFER_OFFSET, Register);

}

/*****************************************************************************/
/**
*
* Interrupt handler for the EMACLite driver. It performs the following
* processing:
*
* - Get the interrupt status from the registers to determine the source of the
*   interrupt.
*
* - Call the appropriate handler based on the source of the interrupt.
*
* @param    InstancePtr contains a pointer to the EMACLite device instance for
*           the interrupt.
*
* @return
*
* None.
*
* @note
*
*
******************************************************************************/
void XEmacLite_InterruptHandler(void *InstancePtr)
{

	XEmacLite *EmacLitePtr;
	u32 Register = 0;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_VOID(InstancePtr != NULL);

	/*
	 * Convert the non-typed pointer to an EmacLite instance pointer
	 * such that there is access to the device.
	 */
	EmacLitePtr = (XEmacLite *) InstancePtr;

	if ((XEmacLite_mIsRxEmpty(EmacLitePtr->BaseAddress) != TRUE) ||
	    (XEmacLite_mIsRxEmpty(EmacLitePtr->BaseAddress + XEL_BUFFER_OFFSET)
	     != TRUE)) {
		/*
		 * Call the RX callback.
		 */

		EmacLitePtr->RecvHandler(EmacLitePtr->RecvRef);

	}
	if ((XEmacLite_mIsTxDone(EmacLitePtr->BaseAddress) == TRUE) &&
	    ((XEmacLite_mGetTxActive(EmacLitePtr->BaseAddress) &
	      XEL_TSR_XMIT_ACTIVE_MASK) != 0)) {

		/*
		 * Clear the Active bit
		 */
		Register = XEmacLite_mGetTxActive(EmacLitePtr->BaseAddress);
		Register &= ~XEL_TSR_XMIT_ACTIVE_MASK;
		XEmacLite_mSetTxActive(EmacLitePtr->BaseAddress, Register);
	}
	if ((XEmacLite_mIsTxDone(EmacLitePtr->BaseAddress + XEL_BUFFER_OFFSET)
	     == TRUE) &&
	    ((XEmacLite_mGetTxActive
	      (EmacLitePtr->BaseAddress + XEL_BUFFER_OFFSET)
	      & XEL_TSR_XMIT_ACTIVE_MASK) != 0)) {

		/*
		 * Clear the Active bit
		 */
		Register =
			XEmacLite_mGetTxActive(EmacLitePtr->BaseAddress +
					       XEL_BUFFER_OFFSET);
		Register &= ~XEL_TSR_XMIT_ACTIVE_MASK;
		XEmacLite_mSetTxActive(EmacLitePtr->BaseAddress +
				       XEL_BUFFER_OFFSET, Register);
	}

	/*
	 * If there was a TX interrupt, call the callback
	 */
	if (Register != 0) {

		/*
		 * Call the TX callback.
		 */

		EmacLitePtr->SendHandler(EmacLitePtr->SendRef);

	}
}

/*****************************************************************************/
/**
*
* Sets the callback function for handling received frames in interrupt mode.
* The upper layer software should call this function during initialization.
* The callback is called when a frame is received. The callback function
* should communicate the data to a thread such that the processing is not
* performed in an interrupt context.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. If there are other potentially slow operations
* within the callback, these should be done at task-level.
*
* @param    InstancePtr is a pointer to the XEmacLite instance to be worked on.
* @param    CallBackRef is a reference pointer to be passed back to the
*           application in the callback. This helps the application correlate
*           the callback to a particular driver.
* @param    FuncPtr is the pointer to the callback function.
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
void XEmacLite_SetRecvHandler(XEmacLite * InstancePtr, void *CallBackRef,
			      XEmacLite_Handler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->RecvHandler = FuncPtr;
	InstancePtr->RecvRef = CallBackRef;
}


/*****************************************************************************/
/**
*
* Sets the callback function for handling transmitted frames in interrupt mode.
* The upper layer software should call this function during initialization.
* The callback is called when a frame is transmitted. The callback function
* should communicate the data to a thread such that the processing is not
* performed in an interrupt context.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. If there are other potentially slow operations
* within the callback, these should be done at task-level.
*
* @param    InstancePtr is a pointer to the XEmacLite instance to be worked on.
* @param    CallBackRef is a reference pointer to be passed back to the
*           application in the callback. This helps the application correlate
*           the callback to a particular driver.
* @param    FuncPtr is the pointer to the callback function.
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
void XEmacLite_SetSendHandler(XEmacLite * InstancePtr, void *CallBackRef,
			      XEmacLite_Handler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->SendHandler = FuncPtr;
	InstancePtr->SendRef = CallBackRef;
}
