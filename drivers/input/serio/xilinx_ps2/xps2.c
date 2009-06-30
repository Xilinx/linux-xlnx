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
*     (c) Copyright 2002-2005 Xilinx Inc.
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
* @file xps2.c
*
* This file contains the required functions for the PS/2 driver.
* Refer to the header file xps2.h for more detailed information.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ch   06/18/02 First release
* 1.00a rmm  05/14/03 Fixed diab compiler warnings relating to asserts.
* 1.01a jvb  12/13/05 I changed Initialize() into CfgInitialize(), and made
*                     CfgInitialize() take a pointer to a config structure
*                     instead of a device id. I moved Initialize() into
*                     xgpio_sinit.c, and had Initialize() call CfgInitialize()
*                     after it retrieved the config structure using the device
*                     id. I removed include of xparameters.h along with any
*                     dependencies on xparameters.h and the _g.c config table.
*
* </pre>
*
*****************************************************************************/

/***************************** Include Files ********************************/

#include "xstatus.h"
#include "xps2.h"
#include "xps2_i.h"
#include "xps2_l.h"
#include "xio.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Variable Definitions ****************************/

/************************** Function Prototypes *****************************/

static void XPs2_StubHandler(void *CallBackRef, u32 Event,
			     unsigned int ByteCount);

/****************************************************************************/
/**
*
* Initializes a specific PS/2 instance such that it is ready to be used.
* The default operating mode of the driver is polled mode.
*
* @param InstancePtr is a pointer to the XPs2 instance to be worked on.
* @param Config is a reference to a structure containing information about
*        a specific PS2 device. This function initializes an InstancePtr object
*        for a specific device specified by the contents of Config. This
*        function can initialize multiple instance objects with the use of
*        multiple calls giving different Config information on each call.
* @param EffectiveAddr is the device base address in the virtual memory address
*        space. The caller is responsible for keeping the address mapping
*        from EffectiveAddr to the device physical base address unchanged
*        once this function is invoked. Unexpected errors may occur if the
*        address mapping changes after this function is called. If address
*        translation is not used, use Config->BaseAddress for this parameters,
*        passing the physical address instead.
*
* @return
*
* - XST_SUCCESS if initialization was successful
*
* @note
*
* The Config pointer argument is not used by this function, but is provided
* to keep the function signature consistent with other drivers.
*
*****************************************************************************/
int XPs2_CfgInitialize(XPs2 * InstancePtr, XPs2_Config * Config,
			   u32 EffectiveAddr)
{
	/*
	 * Assert validates the input arguments
	 */
	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Setup the data that is from the configuration information
	 */
	InstancePtr->BaseAddress = EffectiveAddr;

	/*
	 * Initialize the instance data to some default values and setup a default
	 * handler
	 */
	InstancePtr->Handler = XPs2_StubHandler;

	InstancePtr->SendBuffer.NextBytePtr = NULL;
	InstancePtr->SendBuffer.RemainingBytes = 0;
	InstancePtr->SendBuffer.RequestedBytes = 0;

	InstancePtr->ReceiveBuffer.NextBytePtr = NULL;
	InstancePtr->ReceiveBuffer.RemainingBytes = 0;
	InstancePtr->ReceiveBuffer.RequestedBytes = 0;

	/*
	 * Reset the PS/2 Hardware
	 */
	XPs2_mReset(InstancePtr->BaseAddress);

	/*
	 * Disable all PS/2 interrupts
	 */
	XPs2_mDisableIntr(InstancePtr->BaseAddress, XPS2_INT_ALL);

	/*
	 * Indicate the instance is now ready to use, initialized without error
	 */
	InstancePtr->IsReady = XCOMPONENT_IS_READY;

	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* This functions sends the specified buffer of data to the PS/2 port in either
* polled or interrupt driven modes. This function is non-blocking such that it
* will return before the data has been sent thorugh PS/2. If the port is busy
* sending data, it will return and indicate zero bytes were sent.
*
* In a polled mode, this function will only send 1 byte which is as much data
* as the transmitter can buffer. The application may need to call it
* repeatedly to send a buffer.
*
* In interrupt mode, this function will start sending the specified buffer and
* then the interrupt handler of the driver will continue sending data until the
* buffer has been sent. A callback function, as specified by the application,
* will be called to indicate the completion of sending the buffer.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
* @param    BufferPtr is pointer to a buffer of data to be sent.
* @param    NumBytes contains the number of bytes to be sent. A value of zero
*           will stop a previous send operation that is in progress in interrupt
*           mode. Any data that was already put into the transmit FIFO will be
*           sent.
*
* @return
*
* The number of bytes actually sent.
*
* @note
*
* The number of bytes is not asserted so that this function may be called with
* a value of zero to stop an operation that is already in progress.
* <br><br>
* This function modifies shared data such that there may be a need for mutual
* exclusion in a multithreaded environment
*
*****************************************************************************/
unsigned int XPs2_Send(XPs2 * InstancePtr, u8 * BufferPtr,
		       unsigned int NumBytes)
{
	unsigned int BytesSent;

	/*
	 * Assert validates the input arguments
	 */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Enter a critical region by disabling the PS/2 transmit interrupts to
	 * allow this call to stop a previous operation that may be interrupt
	 * driven, only stop the transmit interrupt since this critical region is
	 * not really exited in the normal manner
	 */
	XPs2_mDisableIntr(InstancePtr->BaseAddress, XPS2_INT_TX_ALL);

	/*
	 * Setup the specified buffer to be sent by setting the instance
	 * variables so it can be sent with polled or interrupt mode
	 */
	InstancePtr->SendBuffer.RequestedBytes = NumBytes;
	InstancePtr->SendBuffer.RemainingBytes = NumBytes;
	InstancePtr->SendBuffer.NextBytePtr = BufferPtr;

	/*
	 * Send the buffer and return the number of bytes sent
	 */
	BytesSent = XPs2_SendBuffer(InstancePtr);

	/*
	 * The critical region is not exited in this function because of the way
	 * the transmit interrupts work. The other function called enables the
	 * transmit interrupt such that this function can't restore a value to the
	 * interrupt enable register and does not need to exit the critical region
	 */
	return BytesSent;
}

/****************************************************************************/
/**
*
* This function will attempt to receive a specified number of bytes of data
* from PS/2 and store it into the specified buffer. This function is
* designed for either polled or interrupt driven modes. It is non-blocking
* such that it will return if no data has already received by the PS/2 port.
*
* In a polled mode, this function will only receive 1 byte which is as much
* data as the receiver can buffer. The application may need to call it
* repeatedly to receive a buffer. Polled mode is the default mode of
* operation for the driver.
*
* In interrupt mode, this function will start receiving and then the interrupt
* handler of the driver will continue receiving data until the buffer has been
* received. A callback function, as specified by the application, will be called
* to indicate the completion of receiving the buffer or when any receive errors
* or timeouts occur. Interrupt mode must be enabled.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
* @param    BufferPtr is pointer to buffer for data to be received into
* @param    NumBytes is the number of bytes to be received. A value of zero will
*           stop a previous receive operation that is in progress in interrupt mode.
*
* @return
*
* The number of bytes received.
*
* @note
*
* The number of bytes is not asserted so that this function may be called with
* a value of zero to stop an operation that is already in progress.
*
*****************************************************************************/
unsigned int XPs2_Recv(XPs2 * InstancePtr, u8 * BufferPtr,
		       unsigned int NumBytes)
{
	unsigned int ReceivedCount;

	/*
	 * Assert validates the input arguments
	 */
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Setup the specified buffer to be sent by setting the instance
	 * variables so it can be sent with polled or interrupt mode
	 */
	InstancePtr->ReceiveBuffer.RequestedBytes = NumBytes;
	InstancePtr->ReceiveBuffer.RemainingBytes = NumBytes;
	InstancePtr->ReceiveBuffer.NextBytePtr = BufferPtr;

	/*
	 * Receive the data from PS/2 and return the number of bytes
	 * received
	 */
	ReceivedCount = XPs2_ReceiveBuffer(InstancePtr);

	return ReceivedCount;
}

/****************************************************************************/
/**
*
* This function sends a buffer that has been previously specified by setting
* up the instance variables of the instance. This function is designed to be
* an internal function for the XPs2 component such that it may be called
* from a shell function that sets up the buffer or from an interrupt handler.
*
* This function sends the specified buffer of data to the PS/2 port in either
* polled or interrupt driven modes. This function is non-blocking such that
* it will return before the data has been sent.
*
* In a polled mode, this function will only send 1 byte which is as much data
* transmitter can buffer. The application may need to call it repeatedly to
* send a buffer.
*
* In interrupt mode, this function will start sending the specified buffer and
* then the interrupt handler of the driver will continue until the buffer
* has been sent. A callback function, as specified by the application, will
* be called to indicate the completion of sending the buffer.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* NumBytes is the number of bytes actually sent
*
* @note
*
* None.
*
*****************************************************************************/
unsigned int XPs2_SendBuffer(XPs2 * InstancePtr)
{
	unsigned int SentCount = 0;

	/*
	 * If the transmitter is empty send one byte of data
	 */
	if (!XPs2_mIsTransmitFull(InstancePtr->BaseAddress)) {
		XPs2_SendByte(InstancePtr->BaseAddress,
			      InstancePtr->SendBuffer.NextBytePtr[SentCount]);

		SentCount = 1;
	}
	/*
	 * Update the buffer to reflect the bytes that were sent
	 * from it
	 */
	InstancePtr->SendBuffer.NextBytePtr += SentCount;
	InstancePtr->SendBuffer.RemainingBytes -= SentCount;

	/*
	 * If interrupts are enabled as indicated by the receive interrupt, then
	 * enable the transmit interrupt
	 */
	if (XPs2_mIsIntrEnabled((InstancePtr->BaseAddress), XPS2_INT_RX_FULL)) {
		XPs2_mEnableIntr(InstancePtr->BaseAddress, XPS2_INT_TX_ALL |
				 XPS2_INT_WDT_TOUT);
	}

	return SentCount;
}

/****************************************************************************/
/**
*
* This function receives a buffer that has been previously specified by setting
* up the instance variables of the instance. This function is designed to be
* an internal function for the XPs2 component such that it may be called
* from a shell function that sets up the buffer or from an interrupt handler.
*
* This function will attempt to receive a specified number of bytes of data
* from PS/2 and store it into the specified buffer. This function is
* designed for either polled or interrupt driven modes. It is non-blocking
* such that it will return if there is no data has already received.
*
* In a polled mode, this function will only receive 1 byte which is as much
* data as the receiver can buffer. The application may need to call it
* repeatedly to receive a buffer. Polled mode is the default mode of operation
* for the driver.
*
* In interrupt mode, this function will start receiving and then the interrupt
* handler of the driver will continue until the buffer has been received. A
* callback function, as specified by the application, will be called to indicate
* the completion of receiving the buffer or when any receive errors or timeouts
* occur. Interrupt mode must be enabled using the SetOptions function.
*
* @param    InstancePtr is a pointer to the XPs2 instance to be worked on.
*
* @return
*
* The number of bytes received.
*
* @note
*
* None.
*
*****************************************************************************/
unsigned int XPs2_ReceiveBuffer(XPs2 * InstancePtr)
{
	unsigned int ReceivedCount = 0;

	/*
	 * Loop until there is no more date buffered by the PS/2 receiver or the
	 * specified number of bytes has been received
	 */
	while (ReceivedCount < InstancePtr->ReceiveBuffer.RemainingBytes) {
		/*
		 * If there is data ready to be read , then put the next byte
		 * read into the specified buffer
		 */
		if (!XPs2_mIsReceiveEmpty(InstancePtr->BaseAddress)) {
			InstancePtr->ReceiveBuffer.
			    NextBytePtr[ReceivedCount++] =
			    XPs2_RecvByte(InstancePtr->BaseAddress);
		}

		/*
		 * There is no more data buffered, so exit such that this function does
		 * not block waiting for data
		 */
		else {
			break;
		}
	}

	/*
	 * Update the receive buffer to reflect the number of bytes that was
	 * received
	 */
	InstancePtr->ReceiveBuffer.NextBytePtr += ReceivedCount;
	InstancePtr->ReceiveBuffer.RemainingBytes -= ReceivedCount;

	return ReceivedCount;
}

/****************************************************************************/
/**
*
* This function is a stub handler that is the default handler such that if the
* application has not set the handler when interrupts are enabled, this
* function will be called. The function interface has to match the interface
* specified for a handler even though none of the arguments are used.
*
* @param    CallBackRef is unused by this function.
* @param    Event is unused by this function.
* @param    ByteCount is unused by this function.
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
static void XPs2_StubHandler(void *CallBackRef, u32 Event,
			     unsigned int ByteCount)
{
	/*
	 * Assert alway occurs since this is a stub and should never be called
	 */
	XASSERT_VOID_ALWAYS();
}
