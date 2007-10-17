/* $Id: xemaclite.c,v 1.1.2.1 2007/03/13 17:26:07 akondratenko Exp $ */
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
* @file xemaclite.c
*
* Functions in this file are the minimum required functions for the EMAC Lite
* driver. See xemaclite.h for a detailed description of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- --------------------------------------------------------
* 1.01a ecm  01/31/04 First release
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xstatus.h"
#include "xio.h"
//#include <asm/delay.h>
#include "xemaclite.h"
#include "xemaclite_l.h"
#include "xemaclite_i.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/
/****************************************************************************
*
* Return the length of the data in the Receive Buffer.
*
* @param    InstancePtr is the pointer to the instance of the driver to
*           be worked on
*
* @note
* This macro returns the length of the received data..
*
*****************************************************************************/
#define XEmacLite_mGetReceiveDataLength(BaseAddress)                        \
           ((XIo_In32((BaseAddress) + XEL_HEADER_OFFSET + XEL_RXBUFF_OFFSET) >> \
            XEL_HEADER_SHIFT) &                                             \
           (XEL_RPLR_LENGTH_MASK_HI | XEL_RPLR_LENGTH_MASK_LO))

/************************** Function Prototypes ******************************/
/************************** Variable Definitions *****************************/

int XEmacLite_CfgInitialize(XEmacLite * InstancePtr, XEmacLite_Config * ConfigPtr,
			u32 VirtualAddress)
{
	/*
	 * Set some default values for instance data, don't indicate the device
	 * is ready to use until everything has been initialized successfully
	 */

	if (0 != VirtualAddress) {
		InstancePtr->BaseAddress = VirtualAddress;
	}
	else {
		InstancePtr->BaseAddress = ConfigPtr->BaseAddress;
	}
	InstancePtr->PhysAddress = ConfigPtr->BaseAddress;
	InstancePtr->ConfigPtr = ConfigPtr;

	InstancePtr->RecvHandler = (XEmacLite_Handler) StubHandler;
	InstancePtr->SendHandler = (XEmacLite_Handler) StubHandler;


	/*
	 * Clear the TX CSR's in case this is a restart
	 */

	XIo_Out32(InstancePtr->BaseAddress + XEL_TSR_OFFSET, 0);
	XIo_Out32(InstancePtr->BaseAddress + XEL_BUFFER_OFFSET + XEL_TSR_OFFSET,
		  0);

	/*
	 * Since there were no failures, indicate the device is ready to use.
	 */

	InstancePtr->IsReady = XCOMPONENT_IS_READY;

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Send an Ethernet frame. The ByteCount is the total frame size, including
* header.
*
* @param InstancePtr is a pointer to the XEmacLite instance to be worked on.
* @param FramePtr is a pointer to frame. For optimal performance, a 32-bit
*        aligned buffer should be used but it is not required, the function
*        will align the data if necessary.
* @param ByteCount is the size, in bytes, of the frame
*
* @return
*
*   - XST_SUCCESS if data was transmitted.
*   - XST_FAILURE if buffer(s) was (were) full and no valid data was
*     transmitted.
*
* @note
*
* This function call is not blocking in nature, i.e. it will not wait until the
* frame is transmitted.
*
******************************************************************************/
int XEmacLite_Send(XEmacLite * InstancePtr, u8 *FramePtr, unsigned ByteCount)
{
	u32 Register;
	u32 BaseAddress;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Determine the expected TX buffer address
	 */

	BaseAddress = XEmacLite_mNextTransmitAddr(InstancePtr);

	/*
	 * Check the Length, of too large, truncate
	 */

	if (ByteCount > XEL_MAX_FRAME_SIZE) {

		ByteCount = XEL_MAX_FRAME_SIZE;
	}
	/*
	 * Determine if the expected buffer address is empty
	 */

	Register = XIo_In32(BaseAddress + XEL_TSR_OFFSET);

	/*
	 * If the expected buffer is available, fill it with the provided data
	 * Align if necessary.
	 */


	if (((Register & XEL_TSR_XMIT_BUSY_MASK) == 0) &&
	    ((XEmacLite_mGetTxActive(BaseAddress) & XEL_TSR_XMIT_ACTIVE_MASK) ==
	     0)) {

		/*
		 * Switch to next buffer if configured
		 */

		if (InstancePtr->ConfigPtr->TxPingPong != 0) {
			InstancePtr->NextTxBufferToUse ^= XEL_BUFFER_OFFSET;
		}

		/*
		 * Write the frame to the buffer.
		 */
		XEmacLite_AlignedWrite(FramePtr, (u32 *) BaseAddress,
				       ByteCount);


		/*
		 * The frame is in the buffer, now send it
		 */
		XIo_Out32(BaseAddress + XEL_TPLR_OFFSET, (ByteCount &
							  (XEL_TPLR_LENGTH_MASK_HI
							   |
							   XEL_TPLR_LENGTH_MASK_LO)));

		Register = XIo_In32(BaseAddress + XEL_TSR_OFFSET);

		Register |= XEL_TSR_XMIT_BUSY_MASK;

		if ((Register & XEL_TSR_XMIT_IE_MASK) != 0) {
			Register |= XEL_TSR_XMIT_ACTIVE_MASK;
		}

		XIo_Out32(BaseAddress + XEL_TSR_OFFSET, Register);

		return XST_SUCCESS;
	}

	/*
	 * If the expected buffer was full, try the other buffer if configured
	 */

	if (InstancePtr->ConfigPtr->TxPingPong != 0) {

		BaseAddress ^= XEL_BUFFER_OFFSET;

		/*
		 * Determine if the expected buffer address is empty
		 */

		Register = XIo_In32(BaseAddress + XEL_TSR_OFFSET);

		/*
		 * If the next buffer is available, fill it with the provided data
		 */

		if (((Register & XEL_TSR_XMIT_BUSY_MASK) == 0) &&
		    ((XEmacLite_mGetTxActive(BaseAddress) &
		      XEL_TSR_XMIT_ACTIVE_MASK) == 0)) {

			/*
			 * Write the frame to the buffer.
			 */
			XEmacLite_AlignedWrite(FramePtr, (u32 *) BaseAddress,
					       ByteCount);

			/*
			 * The frame is in the buffer, now send it
			 */
			XIo_Out32(BaseAddress + XEL_TPLR_OFFSET, (ByteCount &
								  (XEL_TPLR_LENGTH_MASK_HI
								   |
								   XEL_TPLR_LENGTH_MASK_LO)));

			Register = XIo_In32(BaseAddress + XEL_TSR_OFFSET);

			Register |= XEL_TSR_XMIT_BUSY_MASK;

			if ((Register & XEL_TSR_XMIT_IE_MASK) != 0) {
				Register |= XEL_TSR_XMIT_ACTIVE_MASK;
			}

			XIo_Out32(BaseAddress + XEL_TSR_OFFSET, Register);

			/*
			 * Do not switch to next buffer, there is a sync problem and
			 * the expected buffer should not change.
			 */

			return XST_SUCCESS;
		}
	}


	/*
	 * Buffer(s) was(were) full, return failure to allow for polling usage
	 */

	return XST_FAILURE;
}

/*****************************************************************************/
/**
*
* Receive a frame. Intended to be called from the interrupt context or
* with a wrapper which waits for the receive frame to be available.
*
* @param InstancePtr is a pointer to the XEmacLite instance to be worked on.
* @param FramePtr is a pointer to a buffer where the frame will
*        be stored. The buffer must be at least XEL_MAX_FRAME_SIZE bytes.
*        For optimal performance, a 32-bit aligned buffer should be used but
*        it is not required, the function will align the data if necessary.
*
* @return
*
* The type/length field of the frame received.  When the type/length field
* contains the type, XEL_MAX_FRAME_SIZE bytes will be copied out of the
* buffer and it is up to the higher layers to sort out the frame.
* Function returns 0 if there is no data waiting in the receive buffer or
* the pong buffer if configured.
*
* @note
*
* This function call is not blocking in nature, i.e. it will not wait until
* a frame arrives.
*
******************************************************************************/
u16 XEmacLite_Recv(XEmacLite * InstancePtr, u8 *FramePtr)
{
	u16 LengthType;
	u16 Length;
	u32 Register;
	u32 BaseAddress;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Determine the expected buffer address
	 */

	BaseAddress = XEmacLite_mNextReceiveAddr(InstancePtr);

	/*
	 * Verify which buffer has valid data
	 */

	Register = XIo_In32(BaseAddress + XEL_RSR_OFFSET);

	if ((Register & XEL_RSR_RECV_DONE_MASK) == XEL_RSR_RECV_DONE_MASK) {

		/*
		 * The driver is in sync, update the next expected buffer if configured
		 */

		if (InstancePtr->ConfigPtr->RxPingPong != 0) {
			InstancePtr->NextRxBufferToUse ^= XEL_BUFFER_OFFSET;
		}
	}
	else {
		/*
		 * The instance is out of sync, try other buffer if other
		 * buffer is configured, return 0 otherwise. If the instance is
		 * out of syne, do not update the 'NextRxBufferToUse' since it
		 * will ce correct on subsequent calls.
		 */
		if (InstancePtr->ConfigPtr->RxPingPong != 0) {
			BaseAddress ^= XEL_BUFFER_OFFSET;
		}
		else {
			return 0;	/* No data was available */
		}
		/*
		 * Verify that buffer has valid data
		 */

		Register = XIo_In32(BaseAddress + XEL_RSR_OFFSET);

		if ((Register & XEL_RSR_RECV_DONE_MASK) !=
		    XEL_RSR_RECV_DONE_MASK) {
			return 0;	/* No data was available */
		}
	}

	/*
	 * Get the length of the frame that arrived
	 */
	LengthType = XEmacLite_mGetReceiveDataLength(BaseAddress);

	/* Check if length is valid */

	if (LengthType > XEL_MAX_FRAME_SIZE) {
		/* Field contains type, use max frame size and let user parse it */
		Length = XEL_MAX_FRAME_SIZE;
	}
	else {
		/* Use the length in the frame, plus the header and trailer */
		Length = LengthType + XEL_HEADER_SIZE + XEL_FCS_SIZE;
	}

	/*
	 * Read from the EMAC Lite
	 */
	XEmacLite_AlignedRead(((u32 *) (BaseAddress + XEL_RXBUFF_OFFSET)),
			      FramePtr, Length);

	/*
	 * Acknowledge the frame
	 */

	Register = XIo_In32(BaseAddress + XEL_RSR_OFFSET);

	Register &= ~XEL_RSR_RECV_DONE_MASK;

	XIo_Out32(BaseAddress + XEL_RSR_OFFSET, Register);


	return Length;
}

/*****************************************************************************/
/**
*
* Set the MAC address for this device.  The address is a 48-bit value.
*
* @param InstancePtr is a pointer to the XEmacLite instance to be worked on.
* @param AddressPtr is a pointer to a 6-byte MAC address.
*        the format of the MAC address is major octet to minor octet
*
* @return
*
* None.
*
* @note
*
* TX must be idle and RX should be idle for deterministic results.
*
* Function will not return if hardware is absent or not functioning
* properly.
*
******************************************************************************/
void XEmacLite_SetMacAddress(XEmacLite * InstancePtr, u8 *AddressPtr)
{
	u32 BaseAddress;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_VOID(InstancePtr != NULL);

	BaseAddress =
		InstancePtr->BaseAddress + InstancePtr->NextTxBufferToUse +
		XEL_TXBUFF_OFFSET;

	/*
	 * Copy the MAC address to the Transmit buffer
	 */
	XEmacLite_AlignedWrite(AddressPtr, (u32 *) BaseAddress,
			       XEL_MAC_ADDR_SIZE);

	/*
	 * Set the length
	 */

	XIo_Out32(BaseAddress + XEL_TPLR_OFFSET, XEL_MAC_ADDR_SIZE);

	/*
	 * Update the MAC address in the EMAC Lite
	 */

	XIo_Out32(BaseAddress + XEL_TSR_OFFSET, XEL_TSR_PROG_MAC_ADDR);


	/*
	 * Wait for EMAC Lite to finish with the MAC address update
	 */

	while ((XIo_In32(BaseAddress + XEL_TSR_OFFSET) &
		XEL_TSR_PROG_MAC_ADDR) != 0);

	/*
	 * Switch to next buffer if configured
	 */

	if (InstancePtr->ConfigPtr->TxPingPong != 0) {
		InstancePtr->NextTxBufferToUse ^= XEL_BUFFER_OFFSET;
	}

}


/******************************************************************************/
/**
*
* This is a stub for the send and recv callbacks. The stub
* is here in case the upper layers forget to set the handlers.
*
* @param CallBackRef is a pointer to the upper layer callback reference
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
void StubHandler(void *CallBackRef)
{
	XASSERT_VOID_ALWAYS();
}


/****************************************************************************/
/**
*
* Determine if there is a transmit buffer available.
*
* @param    InstancePtr is the pointer to the instance of the driver to
*           be worked on
*
* @return   TRUE if there is a TX buffer available for data to be written into,
*           FALSE otherwise.
*
* @note
*
*****************************************************************************/
u32 XEmacLite_TxBufferAvailable(XEmacLite * InstancePtr)
{

	u32 Register;
	u32 TxPingBusy;
	u32 TxPongBusy;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Read the current buffer register and determine if the buffer is available
	 */

	Register = XIo_In32(InstancePtr->BaseAddress +
			    InstancePtr->NextTxBufferToUse + XEL_TXBUFF_OFFSET);

	TxPingBusy =
		((Register & XEL_TSR_XMIT_BUSY_MASK) == XEL_TSR_XMIT_BUSY_MASK);

	/*
	 * Read the other buffer register and determine if the other buffer is available
	 */

	Register = XIo_In32(InstancePtr->BaseAddress +
			    (InstancePtr->NextTxBufferToUse ^ XEL_TSR_OFFSET) +
			    XEL_TXBUFF_OFFSET);

	TxPongBusy =
		((Register & XEL_TSR_XMIT_BUSY_MASK) == XEL_TSR_XMIT_BUSY_MASK);

	return (!(TxPingBusy && TxPongBusy));

}

/****************************************************************************/
/**
*
*
* Flush the Receive buffers. All data will be lost.
*
* @param    InstancePtr is the pointer to the instance of the driver to
*           be worked on
*
* @return   None.
*
* @note
*
*****************************************************************************/
void XEmacLite_FlushReceive(XEmacLite * InstancePtr)
{

	u32 Register;

	/*
	 * Verify that each of the inputs are valid.
	 */

	XASSERT_VOID(InstancePtr != NULL);

	/*
	 * Read the current buffer register and determine if the buffer is available
	 */

	Register = XIo_In32(InstancePtr->BaseAddress + XEL_RSR_OFFSET);

	/*
	 * Preserve the IE bit
	 */
	Register &= XEL_RSR_RECV_IE_MASK;

	/*
	 * Write out the value to flush the RX buffer
	 */

	XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET, Register);

	/*
	 * If the pong buffer is available, flush it also
	 */

	if (InstancePtr->ConfigPtr->RxPingPong != 0) {
		/*
		 * Read the current buffer register and determine if the buffer is
		 * available
		 */

		Register = XIo_In32(InstancePtr->BaseAddress + XEL_RSR_OFFSET +
				    XEL_BUFFER_OFFSET);

		/*
		 * Preserve the IE bit
		 */
		Register &= XEL_RSR_RECV_IE_MASK;

		/*
		 * Write out the value to flush the RX buffer
		 */

		XIo_Out32(InstancePtr->BaseAddress + XEL_RSR_OFFSET +
			  XEL_BUFFER_OFFSET, Register);

	}

}
