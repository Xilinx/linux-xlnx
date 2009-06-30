/******************************************************************************
*
*     Author: Xilinx, Inc.
*
*
*     This program is free software; you can redistribute it and/or modify it
*     under the terms of the GNU General Public License as published by the
*     Free Software Foundation; either version 2 of the License, or (at your
*     option) any later version.
*
*
*     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
*     COURTESY TO YOU. BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
*     ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD,
*     XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE
*     FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING
*     ANY THIRD PARTY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
*     XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
*     THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY
*     WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM
*     CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND
*     FITNESS FOR A PARTICULAR PURPOSE.
*
*
*     Xilinx hardware products are not intended for use in life support
*     appliances, devices, or systems. Use in such applications is
*     expressly prohibited.
*
*
*     (c) Copyright 2002 Xilinx Inc.
*     All rights reserved.
*
*
*     You should have received a copy of the GNU General Public License along
*     with this program; if not, write to the Free Software Foundation, Inc.,
*     675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/
/****************************************************************************/
/**
*
* @file xps2_intr.c
*
* This file contains the functions that are related to interrupt processing
* for the PS/2 driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ch   06/18/02 First release
* </pre>
*
*****************************************************************************/
/***************************** Include Files ********************************/

#include "xps2.h"
#include "xps2_i.h"
#include "xio.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Variable Definitions ****************************/

typedef void (*Handler) (XPs2 * InstancePtr);

/************************** Function Prototypes *****************************/

static void ReceiveDataHandler(XPs2 * InstancePtr);
static void ReceiveErrorHandler(XPs2 * InstancePtr);
static void ReceiveOverflowHandler(XPs2 * InstancePtr);
static void SendDataHandler(XPs2 * InstancePtr);
static void SendErrorHandler(XPs2 * InstancePtr);
static void TimeoutHandler(XPs2 * InstancePtr);

/****************************************************************************/
/**
*
* This function sets the handler that will be called when an event (interrupt)
* occurs in the driver. The purpose of the handler is to allow application
* specific processing to be performed.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
* @param    FuncPtr is the pointer to the callback function.
* @param    CallBackRef is the upper layer callback reference passed back when
*           the callback function is invoked.
*
* @return
*
* None.
*
* @notes
*
* There is no assert on the CallBackRef since the driver doesn't know what it
* is (nor should it)
*
*****************************************************************************/
void XPs2_SetHandler(XPs2 * InstancePtr, XPs2_Handler FuncPtr,
		     void *CallBackRef)
{
	/*
	 * Assert validates the input arguments
	 * CallBackRef not checked, no way to know what is valid
	 */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->Handler = FuncPtr;
	InstancePtr->CallBackRef = CallBackRef;
}

/****************************************************************************/
/**
*
* This function is the interrupt handler for the PS/2 driver.
* It must be connected to an interrupt system by the user such that it is
* called when an interrupt for any PS/2 port occurs. This function does 
* not save or restore the processor context such that the user must
* ensure this occurs.
*
* @param    InstancePtr contains a pointer to the instance of the PS/2 port
*           that the interrupt is for.
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
void XPs2_InterruptHandler(XPs2 * InstancePtr)
{
	u8 IntrStatus;

	XASSERT_VOID(InstancePtr != NULL);

	/*
	 * Read the interrupt status register to determine which
	 * interrupt is active
	 */
	IntrStatus = XPs2_mGetIntrStatus(InstancePtr->BaseAddress);

	if (IntrStatus & XPS2_INT_WDT_TOUT) {
		TimeoutHandler(InstancePtr);
	}

	if (IntrStatus & XPS2_INT_RX_ERR) {
		ReceiveErrorHandler(InstancePtr);
	}

	if (IntrStatus & XPS2_INT_RX_OVF) {
		ReceiveOverflowHandler(InstancePtr);
	}

	if (IntrStatus & XPS2_INT_TX_NOACK) {
		SendErrorHandler(InstancePtr);
	}

	if (IntrStatus & XPS2_INT_RX_FULL) {
		ReceiveDataHandler(InstancePtr);
	}

	if (IntrStatus & XPS2_INT_TX_ACK) {
		SendDataHandler(InstancePtr);
	}
}

/****************************************************************************/
/**
*
* This function enables the PS/2 interrupts.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
void XPs2_EnableInterrupt(XPs2 * InstancePtr)
{
	/*
	 * ASSERT the arguments
	 */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Enable all receiver interrupts (RX_FULL, RX_ERR, RX_OVF)
	 * transmitter interrupts are enabled when sending data.
	 */
	XPs2_mEnableIntr(InstancePtr->BaseAddress, XPS2_INT_RX_ALL);
}

/****************************************************************************/
/**void XPs2_DisableInterrupt
*
* This function disables the PS/2 interrupts.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
void XPs2_DisableInterrupt(XPs2 * InstancePtr)
{
	/*
	 * ASSERT the arguments
	 */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Disable all interrupts.
	 */
	XPs2_mDisableIntr(InstancePtr->BaseAddress, XPS2_INT_ALL);
}

/****************************************************************************/
/**
*
* This function handles the interrupt when data is received.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void ReceiveDataHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_RX_FULL);

	/*
	 * If there are bytes still to be received in the specified buffer
	 * go ahead and receive them
	 */
	if (InstancePtr->ReceiveBuffer.RemainingBytes != 0) {
		XPs2_ReceiveBuffer(InstancePtr);
	}

	/*
	 * If the last byte of a message was received then call the application
	 * handler, this code should not use an else from the previous check of
	 * the number of bytes to receive because the call to receive the buffer
	 * updates the bytes to receive
	 */
	if (InstancePtr->ReceiveBuffer.RemainingBytes == 0) {
		InstancePtr->Handler(InstancePtr->CallBackRef,
				     XPS2_EVENT_RECV_DATA,
				     InstancePtr->ReceiveBuffer.RequestedBytes -
				     InstancePtr->ReceiveBuffer.RemainingBytes);
	}

	/*
	 * Update the receive stats to reflect the receive interrupt
	 */
	InstancePtr->Stats.ReceiveInterrupts++;
}

/****************************************************************************/
/**
*
* This function handles the receive error interrupt.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void ReceiveErrorHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_RX_ERR);

	/*
	 * Call the application handler with an error code
	 */
	InstancePtr->Handler(InstancePtr->CallBackRef, XPS2_EVENT_RECV_ERROR,
			     InstancePtr->ReceiveBuffer.RequestedBytes -
			     InstancePtr->ReceiveBuffer.RemainingBytes);

	/*
	 * Update the LastError variable
	 */
	InstancePtr->LastErrors |= XPS2_ERROR_RX_ERR_MASK;

	/*
	 * Update the receive stats to reflect the receive error interrupt
	 */
	InstancePtr->Stats.ReceiveErrors++;
}

/****************************************************************************/
/**
*
* This function handles the receive overflow interrupt.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void ReceiveOverflowHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_RX_OVF);

	/*
	 * Call the application handler with an error code
	 */
	InstancePtr->Handler(InstancePtr->CallBackRef, XPS2_EVENT_RECV_OVF,
			     InstancePtr->ReceiveBuffer.RequestedBytes -
			     InstancePtr->ReceiveBuffer.RemainingBytes);

	/*
	 * Update the LastError variable
	 */
	InstancePtr->LastErrors |= XPS2_ERROR_RX_OVF_MASK;

	/*
	 * Update the receive stats to reflect the receive interrupt
	 */
	InstancePtr->Stats.ReceiveOverflowErrors++;
}

/****************************************************************************/
/**
*
* This function handles the interrupt when data has been sent, the transmit
* transmitter holding register is empty.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void SendDataHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_TX_ACK);

	/*
	 * If there are no bytes to be sent from the specified buffer then disable
	 * the transmit interrupt
	 */
	if (InstancePtr->SendBuffer.RemainingBytes == 0) {
		XPs2_mDisableIntr(InstancePtr->BaseAddress, XPS2_INT_TX_ALL);

		/*
		 * Call the application handler to indicate the data has been sent
		 */
		InstancePtr->Handler(InstancePtr->CallBackRef,
				     XPS2_EVENT_SENT_DATA,
				     InstancePtr->SendBuffer.RequestedBytes -
				     InstancePtr->SendBuffer.RemainingBytes);
	}

	/*
	 * Otherwise there is still more data to send in the specified buffer
	 * so go ahead and send it
	 */
	else {
		XPs2_SendBuffer(InstancePtr);
	}

	/*
	 * Update the transmit stats to reflect the transmit interrupt
	 */
	InstancePtr->Stats.TransmitInterrupts++;
}

/****************************************************************************/
/**
*
* This function handles the interrupt when a transmit is not acknowledged
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void SendErrorHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_TX_NOACK);

	/*
	 * Call the application handler
	 */
	InstancePtr->Handler(InstancePtr->CallBackRef, XPS2_EVENT_SENT_NOACK,
			     InstancePtr->SendBuffer.RequestedBytes -
			     InstancePtr->SendBuffer.RemainingBytes);

	/*
	 * Update the LastError variable
	 */
	InstancePtr->LastErrors |= XPS2_ERROR_TX_NOACK_MASK;

	/*
	 * Update the transmit stats to reflect the transmit interrupt
	 */
	InstancePtr->Stats.TransmitErrors++;
}

/****************************************************************************/
/**
*
* This function handles the interrupt when timeout occurrs
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* None.
*
*****************************************************************************/
static void TimeoutHandler(XPs2 * InstancePtr)
{
	XPs2_mClearIntr(InstancePtr->BaseAddress, XPS2_INT_WDT_TOUT);

	/*
	 * Call the application handler
	 */
	InstancePtr->Handler(InstancePtr->CallBackRef, XPS2_EVENT_TIMEOUT,
			     InstancePtr->SendBuffer.RequestedBytes -
			     InstancePtr->SendBuffer.RemainingBytes);

	/*
	 * Update the LastError variable
	 */
	InstancePtr->LastErrors |= XPS2_ERROR_WDT_TOUT_MASK;

	/*
	 * Update the transmit stats to reflect the transmit interrupt
	 */
	InstancePtr->Stats.TransmitErrors++;
}
