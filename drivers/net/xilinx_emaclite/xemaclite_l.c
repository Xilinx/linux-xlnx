/* $Id: xemaclite_l.c,v 1.1.2.1 2007/03/13 17:26:08 akondratenko Exp $ */
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
* @file xemaclite_l.c
*
* This file contains the minimal, polled functions to send and receive Ethernet
* frames.
*
* Refer to xemaclite.h for more details.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ecm  06/01/02 First release
* 1.01a ecm  03/31/04 Additional functionality and the _AlignedRead and
*                     _AlignedWrite functions.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xemaclite_l.h"
#include "xemaclite_i.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
void XEmacLite_AlignedWrite(void *SrcPtr, u32 *DestPtr, unsigned ByteCount);
void XEmacLite_AlignedRead(u32 *SrcPtr, void *DestPtr, unsigned ByteCount);

/************************** Variable Definitions *****************************/

/*****************************************************************************/
/**
*
* Send an Ethernet frame. The size is the total frame size, including header.
* This function blocks waiting for the frame to be transmitted.
*
* @param BaseAddress is the base address of the device
* @param FramePtr is a pointer to frame
* @param ByteCount is the size, in bytes, of the frame
*
* @return
*
* None.
*
* @note
*
* This function call is blocking in nature, i.e. it will wait until the
* frame is transmitted. This function can hang and not exit if the
* hardware is not configured properly.
*
* If the ping buffer is the destination of the data, the argument should be
* DeviceAddress + XEL_TXBUFF_OFFSET.
* If the pong buffer is the destination of the data, the argument should be
* DeviceAddress + XEL_TXBUFF_OFFSET + XEL_BUFFER_OFFSET.
* The function does not take the different buffers into consideration.
******************************************************************************/
void XEmacLite_SendFrame(u32 BaseAddress, u8 *FramePtr, unsigned ByteCount)
{
	u32 Register;

	/*
	 * Write data to the EMAC Lite
	 */
	XEmacLite_AlignedWrite(FramePtr, (u32 *) (BaseAddress), ByteCount);

	/*
	 * The frame is in the buffer, now send it
	 */
	XIo_Out32(BaseAddress + XEL_TPLR_OFFSET,
		  (ByteCount &
		   (XEL_TPLR_LENGTH_MASK_HI | XEL_TPLR_LENGTH_MASK_LO)));


	Register = XIo_In32(BaseAddress + XEL_TSR_OFFSET);
	XIo_Out32(BaseAddress + XEL_TSR_OFFSET,
		  (Register | XEL_TSR_XMIT_BUSY_MASK));

	/*
	 * Loop on the status waiting for the transmit to be complete.
	 */
//    while (!XEmacLite_mIsTxDone(BaseAddress));

}


/*****************************************************************************/
/**
*
* Receive a frame. Wait for a frame to arrive.
*
* @param BaseAddress is the base address of the device
* @param FramePtr is a pointer to a buffer where the frame will
*        be stored.
*
* @return
*
* The type/length field of the frame received.  When the type/length field
* contains the type , XEL_MAX_FRAME_SIZE bytes will be copied out of the
* buffer and it is up to the higher layers to sort out the frame.
*
* @note
*
* This function call is blocking in nature, i.e. it will wait until a
* frame arrives.
*
* If the ping buffer is the source of the data, the argument should be
* DeviceAddress + XEL_RXBUFF_OFFSET.
* If the pong buffer is the source of the data, the argument should be
* DeviceAddress + XEL_RXBUFF_OFFSET + XEL_BUFFER_OFFSET.
* The function does not take the different buffers into consideration.
******************************************************************************/
u16 XEmacLite_RecvFrame(u32 BaseAddress, u8 *FramePtr)
{
	u16 LengthType;
	u16 Length;
	u32 Register;

	/*
	 * Wait for a frame to arrive - this is a blocking call
	 */

	while (XEmacLite_mIsRxEmpty(BaseAddress));

	/*
	 * Get the length of the frame that arrived
	 */
	LengthType = XIo_In32(BaseAddress + XEL_RPLR_OFFSET);
	LengthType &= (XEL_RPLR_LENGTH_MASK_HI | XEL_RPLR_LENGTH_MASK_LO);

	/* check if length is valid */

	if (LengthType > XEL_MAX_FRAME_SIZE) {
		/* Field contain type, use max frame size and let user parse it */
		Length = XEL_MAX_FRAME_SIZE;
	}
	else {
		/* Use the length in the frame, plus the header and trailer */
		Length = LengthType + XEL_HEADER_SIZE + XEL_FCS_SIZE;
	}

	/*
	 * Read each byte from the EMAC Lite
	 */
	XEmacLite_AlignedRead((u32 *) (BaseAddress + XEL_RXBUFF_OFFSET),
			      FramePtr, Length);

	/*
	 * Acknowledge the frame
	 */

	Register = XIo_In32(BaseAddress + XEL_RSR_OFFSET);
	Register &= ~XEL_RSR_RECV_DONE_MASK;
	XIo_Out32(BaseAddress + XEL_RSR_OFFSET, Register);

	return LengthType;
}

/******************************************************************************/
/**
*
* This function aligns the incoming data and writes it out to a 32-bit
* aligned destination address range.
*
* @param SrcPtr is a pointer to incoming data of any alignment.
* @param DestPtr is a pointer to outgoing data of 32-bit alignment.
* @param ByteCount is the number of bytes to write.
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
void XEmacLite_AlignedWrite(void *SrcPtr, u32 *DestPtr, unsigned ByteCount)
{
	unsigned i;
	unsigned Length = ByteCount;
	u32 AlignBuffer;
	u32 *To32Ptr;
	u32 *From32Ptr;
	u16 *To16Ptr;
	u16 *From16Ptr;
	u8 *To8Ptr;
	u8 *From8Ptr;

	To32Ptr = DestPtr;

	if ((((u32) SrcPtr) & 0x00000003) == 0) {

		/*
		 * Word aligned buffer, no correction needed.
		 */
//      printk("noaligned\n");

		From32Ptr = (u32 *) SrcPtr;

		while (Length > 3) {
			/*
			 * Output each word destination.
			 */

			*To32Ptr++ = *From32Ptr++;

			/*
			 * Adjust length accordingly
			 */

			Length -= 4;
		}

		/*
		 * Set up to output the remaining data, zero the temp buffer first.
		 */

		AlignBuffer = 0;
		To8Ptr = (u8 *) &AlignBuffer;
		From8Ptr = (u8 *) From32Ptr;

	}
	else if ((((u32) SrcPtr) & 0x00000001) != 0) {
		/*
		 * Byte aligned buffer, correct.
		 */

		AlignBuffer = 0;
		To8Ptr = (u8 *) &AlignBuffer;
		From8Ptr = (u8 *) SrcPtr;

//      printk("aligned8\n");

		while (Length > 3) {
			/*
			 * Copy each byte into the temporary buffer.
			 */

			for (i = 0; i < 4; i++) {
				*To8Ptr++ = *From8Ptr++;
			}

			/*
			 * Output the buffer
			 */

			*To32Ptr++ = AlignBuffer;

			/*.
			 * Reset the temporary buffer pointer and adjust length.
			 */

			To8Ptr = (u8 *) &AlignBuffer;
			Length -= 4;
		}

		/*
		 * Set up to output the remaining data, zero the temp buffer first.
		 */

		AlignBuffer = 0;
		To8Ptr = (u8 *) &AlignBuffer;

	}
	else {
		/*
		 * Half-Word aligned buffer, correct.
		 */

		AlignBuffer = 0;
		To16Ptr = (u16 *) &AlignBuffer;
		From16Ptr = (u16 *) SrcPtr;

//      printk("aligned16\n");

		while (Length > 3) {
			/*
			 * Copy each half word into the temporary buffer.
			 */

			for (i = 0; i < 2; i++) {
				*To16Ptr++ = *From16Ptr++;
			}

			/*
			 * Output the buffer.
			 */

			*To32Ptr++ = AlignBuffer;

			/*
			 * Reset the temporary buffer pointer and adjust length.
			 */

			To16Ptr = (u16 *) &AlignBuffer;
			Length -= 4;
		}

		/*
		 * Set up to output the remaining data, zero the temp buffer first.
		 */

		AlignBuffer = 0;
		To8Ptr = (u8 *) &AlignBuffer;
		From8Ptr = (u8 *) From16Ptr;
	}

	/*
	 * Output the remaining data, zero the temp buffer first.
	 */
	for (i = 0; i < Length; i++) {
		*To8Ptr++ = *From8Ptr++;
	}

	*To32Ptr++ = AlignBuffer;

}

/******************************************************************************/
/**
*
* This function reads from a 32-bit aligned source address range and aligns
* the writes to the provided destination pointer alignment.
*
* @param SrcPtr is a pointer to incoming data of 32-bit alignment.
* @param DestPtr is a pointer to outgoing data of any alignment.
* @param ByteCount is the number of bytes to read.
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
void XEmacLite_AlignedRead(u32 *SrcPtr, void *DestPtr, unsigned ByteCount)
{
	unsigned i;
	unsigned Length = ByteCount;
	u32 AlignBuffer;
	u32 *To32Ptr;
	u32 *From32Ptr;
	u16 *To16Ptr;
	u16 *From16Ptr;
	u8 *To8Ptr;
	u8 *From8Ptr;

	From32Ptr = (u32 *) SrcPtr;

	if ((((u32) DestPtr) & 0x00000003) == 0) {

		/*
		 * Word aligned buffer, no correction needed.
		 */

		To32Ptr = (u32 *) DestPtr;

		while (Length > 3) {
			/*
			 * Output each word.
			 */

			*To32Ptr++ = *From32Ptr++;

			/*
			 * Adjust length accordingly.
			 */
			Length -= 4;
		}

		/*
		 * Set up to read the remaining data.
		 */

		To8Ptr = (u8 *) To32Ptr;

	}
	else if ((((u32) DestPtr) & 0x00000001) != 0) {
		/*
		 * Byte aligned buffer, correct.
		 */

		To8Ptr = (u8 *) DestPtr;

		while (Length > 3) {
			/*
			 * Copy each word into the temporary buffer.
			 */

			AlignBuffer = *From32Ptr++;
			From8Ptr = (u8 *) &AlignBuffer;

			/*
			 * Write data to destination.
			 */

			for (i = 0; i < 4; i++) {
				*To8Ptr++ = *From8Ptr++;
			}

			/*
			 * Adjust length
			 */

			Length -= 4;
		}

	}
	else {
		/*
		 * Half-Word aligned buffer, correct.
		 */

		To16Ptr = (u16 *) DestPtr;

		while (Length > 3) {
			/*
			 * Copy each word into the temporary buffer.
			 */

			AlignBuffer = *From32Ptr++;
			From16Ptr = (u16 *) &AlignBuffer;

			/*
			 * Write data to destination.
			 */

			for (i = 0; i < 2; i++) {
				*To16Ptr++ = *From16Ptr++;
			}

			/*
			 * Adjust length.
			 */

			Length -= 4;
		}

		/*
		 * Set up to read the remaining data.
		 */

		To8Ptr = (u8 *) To16Ptr;
	}

	/*
	 * Read the remaining data.
	 */

	AlignBuffer = *From32Ptr++;
	From8Ptr = (u8 *) &AlignBuffer;

	for (i = 0; i < Length; i++) {
		*To8Ptr++ = *From8Ptr++;
	}
}
