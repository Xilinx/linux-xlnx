/* $Id: */
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
*       (c) Copyright 2005-2006 Xilinx Inc.
*       All rights reserved.
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xtemac_l.c
*
* This file contains low-level functions to send and receive Ethernet frames.
*
* @note
*
* This API cannot be used when device is configured in SGDMA mode.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- ------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Modified to match HW 3.00a
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac_l.h"
#include "xpacket_fifo_l_v2_00_a.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/*****************************************************************************/
/**
*
* Reset and enable the transmitter and receiver. The contents of the Rx and Tx
* control registers are preserved.
*
* @param BaseAddress is the base address of the device
*
* @return
*
* None.
*
* @note
*
* If hardware is not behaving properly, then this function may never return.
*
******************************************************************************/
void XTemac_Enable(u32 BaseAddress)
{
	u32 CR_save0;
	u32 CR_save1;
	volatile u32 CR;

	/* Save contents of the Rx control registers, then reset the receiver */
	CR_save0 = XTemac_mReadHostReg(BaseAddress, XTE_RXC0_OFFSET);
	CR_save1 = XTemac_mReadHostReg(BaseAddress, XTE_RXC1_OFFSET);
	XTemac_mWriteHostReg(BaseAddress, XTE_RXC1_OFFSET, XTE_RXC1_RXRST_MASK);

	/* Wait for the receiver to finish reset */
	do {
		CR = XTemac_mReadHostReg(BaseAddress, XTE_RXC1_OFFSET);
	} while (CR & XTE_RXC1_RXRST_MASK);

	/* Restore contents of Rx control registers, enable receiver */
	XTemac_mWriteHostReg(BaseAddress, XTE_RXC0_OFFSET, CR_save0);
	XTemac_mWriteHostReg(BaseAddress, XTE_RXC1_OFFSET,
			     CR_save1 | XTE_RXC1_RXEN_MASK);

	/* Save contents of the Tx control register, then reset the transmitter */
	CR_save0 = XTemac_mReadHostReg(BaseAddress, XTE_TXC_OFFSET);
	XTemac_mWriteHostReg(BaseAddress, XTE_TXC_OFFSET, XTE_TXC_TXRST_MASK);

	/* Wait for the transmitter to finish reset */
	do {
		CR = XTemac_mReadHostReg(BaseAddress, XTE_TXC_OFFSET);
	} while (CR & XTE_TXC_TXRST_MASK);

	/* Restore contents of Tx control register, enable transmitter */
	XTemac_mWriteHostReg(BaseAddress, XTE_TXC_OFFSET,
			     CR_save0 | XTE_TXC_TXEN_MASK);
}


/*****************************************************************************/
/**
*
* Disable the transmitter and receiver.
*
* @param BaseAddress is the base address of the device
*
* @return
*
* None.
*
* @note
*
******************************************************************************/
void XTemac_Disable(u32 BaseAddress)
{
	u32 CR;

	/* Disable the receiver */
	CR = XTemac_mReadHostReg(BaseAddress, XTE_RXC1_OFFSET);
	XTemac_mWriteHostReg(BaseAddress, XTE_RXC1_OFFSET,
			     CR & ~XTE_RXC1_RXEN_MASK);

	/* Disable the transmitter */
	CR = XTemac_mReadHostReg(BaseAddress, XTE_TXC_OFFSET);
	XTemac_mWriteHostReg(BaseAddress, XTE_TXC_OFFSET,
			     CR & ~XTE_TXC_TXEN_MASK);
}


/*****************************************************************************/
/**
*
* Send an Ethernet frame. This size is the total frame size, including header.
* This function will return immediately upon dispatching of the frame to the
* transmit FIFO. Upon return, the provided frame buffer can be reused. To
* monitor the transmit status, use XTemac_mIsTxDone(). If desired, the
* transmit status register (XTE_TSR_OFFSET) can be read to obtain the outcome
* of the transaction. This function can be used only when the device is
* configured for FIFO direct mode.
*
* @param BaseAddress is the base address of the device
* @param FramePtr is a pointer to a 32-bit aligned frame
* @param Size is the size, in bytes, of the frame
*
* @return
*
* - Size of the frame sent (Size parameter)
* - 0 if the frame will not fit in the data FIFO.
*
* @note
*
* A transmit length FIFO overrun (XTE_IPXR_XMIT_LFIFO_OVER_MASK) condition may
* occur if too many frames are pending transmit. This situation can happen when
* many small frames are being sent. To prevent this condition, pause sending
* when transmit length FIFO full (XTE_IPXR_XMIT_LFIFO_FULL_MASK) is indicated in
* the XTE_XTE_IPISR_OFFSET register.
*
******************************************************************************/
int XTemac_SendFrame(u32 BaseAddress, void *FramePtr, int Size)
{
	int Result;

	/* Clear the status so it can be checked by the caller
	 * Must handle toggle-on-write for status bits...unfortunately
	 */
	if (XTemac_mReadReg(BaseAddress, XTE_IPISR_OFFSET) &
	    XTE_IPXR_XMIT_DONE_MASK) {
		XTemac_mWriteReg(BaseAddress, XTE_IPISR_OFFSET,
				 XTE_IPXR_XMIT_DONE_MASK);
	}

	/* Use the packet fifo driver write the FIFO */
	Result = XPacketFifoV200a_L0Write(BaseAddress + XTE_PFIFO_TXREG_OFFSET,
					  BaseAddress + XTE_PFIFO_TXDATA_OFFSET,
					  (u8 *) FramePtr, Size);

	/* No room in the FIFO */
	if (Result != XST_SUCCESS) {
		return (0);
	}

	/* The frame is in the Fifo, now send it */
	XIo_Out32(BaseAddress + XTE_TPLR_OFFSET, Size);

	return (Size);
}


/*****************************************************************************/
/**
*
* Receive a frame. This function can be used only when the device is
* configured for FIFO direct mode.
*
* @param BaseAddress is the base address of the device
* @param FramePtr is a pointer to a 32-bit aligned buffer where the frame will
*        be stored
*
* @return
*
* The size, in bytes, of the frame received.
* 0 if no frame has been received.
*
* @note
*
* None.
*
******************************************************************************/
int XTemac_RecvFrame(u32 BaseAddress, void *FramePtr)
{
	int Length;

	/* Is there a received frame present */
	if (XTemac_mIsRxEmpty(BaseAddress)) {
		return (0);
	}

	/* Get the length of the frame that arrived */
	Length = XTemac_mReadReg(BaseAddress, XTE_RPLR_OFFSET);

	/* Clear the status now that the length is read so we're ready again
	 * next time
	 */
	XTemac_mWriteReg(BaseAddress, XTE_IPISR_OFFSET,
			 XTE_IPXR_RECV_DONE_MASK);

	/* Use the packet fifo driver to read the FIFO. We assume the Length is
	 * valid and there is enough data in the FIFO - so we ignore the return
	 * code.
	 */
	(void) XPacketFifoV200a_L0Read(BaseAddress + XTE_PFIFO_RXREG_OFFSET,
				       BaseAddress + XTE_PFIFO_RXDATA_OFFSET,
				       (u8 *) FramePtr, Length);
	return (Length);
}
