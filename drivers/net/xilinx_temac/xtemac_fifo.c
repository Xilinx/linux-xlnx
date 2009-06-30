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
* @file xtemac_fifo.c
*
* Functions in this file implement FIFO direct and Simple DMA frame transfer
* mode. See xtemac.h for a detailed description of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 1.00b rmm  09/23/05 Fixed void* arithmetic usage, added XST_FIFO_ERROR
*                     return code to send/recv query functions.
* 2.00a rmm  11/21/05 Removed XST_FAILURE return code for XTemac_FifoQuery-
*                     SendStatus, removed simple dma code
*       rmm  06/22/06 Fixed C++ compiler warnings
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac.h"
#include "xtemac_i.h"
#include "xio.h"

/************************** Constant Definitions *****************************/

#define PFIFO_64BIT_WIDTH_BYTES 8

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/*******************************************************************************
 * Primitives that modify the hold structure for XTemac_PacketFifo. All F
 * parameters refer to a pointer to XTemac_PacketFifo.
 *
 * mHold_GetIndex(F) - Get the ByteIndex of Hold
 * mHold_SetIndex(F,D) - Set the ByteIndex of Hold to D
 * mHold_Advance(F,D) - Advance the ByteIndex of Hold by D bytes
 * mHold_CopyIn(F,I,D) - Set Hold[I] to D
 * mHold_CopyOut(F,I,D) - Set D to Hold[I]
 * mHoldS_IsFull(F) - Is a write channel Hold full of data
 * mHoldS_IsEmpty(F) - Is a write channel Hold empty
 * mHoldS_SetEmpty(F) - Set a write channel Hold empty
 * mHoldR_IsFull(F) - Is a read channel Hold full of data
 * mHoldR_IsEmpty(F) - Is a read channel Hold empty
 * mHoldR_SetEmpty(F) - Set a read channel Hold empty
 *
 * @param F - Address to a XTemac_PacketFifo structure
 * @param SrcPtr - Source data address aligned on 4 byte boundary
 *
 ******************************************************************************/
#define mHold_GetIndex(F)      ((F)->ByteIndex)
#define mHold_SetIndex(F, D)   ((F)->ByteIndex = (D))
#define mHold_Advance(F, D)    ((F)->ByteIndex += (D))
#define mHold_CopyIn(F, I, D)  (*(u8*)(((u8*)(&(F)->Hold[0])) + (I)) = (D))
#define mHold_CopyOut(F, I, D) ((D) = (*(u8*)(((u8*)(&(F)->Hold[0])) + (I))))

#define mHoldS_IsFull(F)    ((F)->ByteIndex >= (F)->Width)
#define mHoldS_IsEmpty(F)   ((F)->ByteIndex == 0)
#define mHoldS_SetEmpty(F)  ((F)->ByteIndex = 0)

#define mHoldR_IsFull(F)    ((F)->ByteIndex == 0)
#define mHoldR_IsEmpty(F)   ((F)->ByteIndex >= (F)->Width)
#define mHoldR_SetEmpty(F)  ((F)->ByteIndex = (F)->Width)

/*******************************************************************************
 * Primitive write to 64 bit FIFO. Use two 32-bit wide I/O accesses.
 *
 * @param F - Address to a XTemac_PacketFifo structure
 * @param SrcPtr - Source data address aligned on 4 byte boundary
 *
 ******************************************************************************/
#define mWriteFifo64(F, SrcPtr)                                \
    {                                                          \
        register u32 Faddr = F->Fifo.DataBaseAddress;      \
        XIo_Out32(Faddr, (SrcPtr)[0]);                         \
        XIo_Out32(Faddr + 4, (SrcPtr)[1]);                     \
    }

/*******************************************************************************
 * Primitive read from 64 bit FIFO. Use two 32-bit wide I/O accesses.
 *
 * @param F - Address to a XTemac_PacketFifo structure
 * @param DestPtr - Destination data address aligned on 4 byte boundary
 *
 ******************************************************************************/
#define mReadFifo64(F, DestPtr)                                \
    (DestPtr)[0] = XIo_In32(F->Fifo.DataBaseAddress);          \
    (DestPtr)[1] = XIo_In32(F->Fifo.DataBaseAddress + 4);

/*******************************************************************************
 * Primitive to transfer the holding data to the FIFO 64 bits at a time
 *
 * @param F - Address to a XTemac_PacketFifo structure
 *
 ******************************************************************************/
#define mPush64(F) mWriteFifo64(F, &F->Hold[0])

/*******************************************************************************
 * Primitive to tranfer FIFO contents into the holding data 64 bits at a time
 *
 * @param F - Address to a XTemac_PacketFifo structure
 *
 ******************************************************************************/
#define mPop64(F) mReadFifo64(F, &F->Hold[0])


/************************** Function Prototypes ******************************/

/* The following functions will be attached to the FifoRead and FifoWrite
 * attribute of an instance by XTemac_ConfigureFifoAccess
 */
static int Write_64(XTemac_PacketFifo *Fptr, void *BufPtr,
		    u32 ByteCount, int Eop);
static int Read_64(XTemac_PacketFifo *Fptr, void *BufPtr,
		   u32 ByteCount, int Eop);

/* 64 bit wide FIFO support functions */
static void Write64_Unaligned(XTemac_PacketFifo *F, void *BufPtr,
			      u32 ByteCount);
static void Write64_Aligned(XTemac_PacketFifo *F, u32 *BufPtr, u32 ByteCount);
static void Read64_Unaligned(XTemac_PacketFifo *F, void *BufPtr, u32 ByteCount);
static void Read64_Aligned(XTemac_PacketFifo *F, u32 *BufPtr, u32 ByteCount);


/*******************************************************************************
 * Select the best method for accessing the read and write FIFOs for FIFO direct
 * frame transfer mode. On the write (transmit) side, the choices are DRE or via
 * the holding structure. Both methods allow unaligned transfers. On the read
 * (receive) side, the only choice is the holding structure.
 *
 * This function should be called only from XTemac_Initialize().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return XST_SUCCESS or XST_FAILURE if an error was detected
 *
 ******************************************************************************/
int XTemac_ConfigureFifoAccess(XTemac *InstancePtr)
{
	int Result;

	/* Initialize the packet FIFOs */
	Result = XPacketFifoV200a_Initialize(&InstancePtr->RecvFifo.Fifo,
					     InstancePtr->BaseAddress +
					     XTE_PFIFO_RXREG_OFFSET,
					     InstancePtr->BaseAddress +
					     XTE_PFIFO_RXDATA_OFFSET);
	if (Result != XST_SUCCESS) {
		return (XST_FAILURE);
	}

	Result = XPacketFifoV200a_Initialize(&InstancePtr->SendFifo.Fifo,
					     InstancePtr->BaseAddress +
					     XTE_PFIFO_TXREG_OFFSET,
					     InstancePtr->BaseAddress +
					     XTE_PFIFO_TXDATA_OFFSET);

	if (Result != XST_SUCCESS) {
		return (XST_FAILURE);
	}

	/* Choose an access algorithm.
	 * Note: 64-bit wide FIFO is the only width supported at this time
	 */
	InstancePtr->RecvFifo.Width = PFIFO_64BIT_WIDTH_BYTES;
	InstancePtr->RecvFifo.XferFn = Read_64;
	InstancePtr->SendFifo.Width = PFIFO_64BIT_WIDTH_BYTES;
	InstancePtr->SendFifo.XferFn = Write_64;

	/* Initialize the holds */
	mHoldS_SetEmpty(&InstancePtr->SendFifo);
	mHoldR_SetEmpty(&InstancePtr->RecvFifo);

	return (XST_SUCCESS);
}

/******************************************************************************/
/**
 * Copy data from a user buffer to the transmit packet FIFO. The data copied
 * may comprise of single, multiple, or partial packets. The data is not
 * transmitted until XTemac_FifoSend() is called.
 *
 * If the user buffer contains multiple packets, then extra care must be taken.
 * In this special situation, the end of one packet and the beginning of a new
 * packet is specified within the user buffer. The beginning of each NEW packet
 * must begin on a 4 byte alignment. The user is responsible for adding filler
 * data between packets to acheive this alignment. The amount of filler data
 * depends on what byte the end of the previous packet falls on. When calling
 * XTemac_FifoSend() to transmit the packets, DO NOT specify the filler bytes
 * in the TxByteCount parameter. For example, if a user buffer contains two
 * complete packets of 15 bytes each with 1 byte of filler between them, then
 * XTemac_FifoWrite() is called once to write all 31 bytes to the FIFO.
 * XTemac_FifoSend() is called twice specifying 15 bytes each time to transmit
 * the packets (the 1 byte of filler data is ignored by the TEMAC). Of course
 * you could also just call XTemac_FifoWrite() once for each packet. This way,
 * the driver will manage the filler data.
 *
 * If the user's buffer is not aligned on a 4 byte boundary, then the transfer
 * may take longer to complete.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param BufPtr is the buffer containing user data that will be transferred
 *        into the transmit FIFO. The buffer may be on any alignment.
 * @param ByteCount is the number of bytes to transfer from 1 to the number
 *        of bytes available in the FIFO at the time of invocation. See usage
 *        note for situations when a value of 0 is legal.
 * @param Eop specifies whether the last byte of BufPtr marks the End Of Packet.
 *        If set to XTE_END_OF_PACKET, then any partial bytes being buffered by
 *        the driver are flushed into the packet FIFO. If set to
 *        XTE_PARTIAL_PACKET, then more packet data is expected to be written
 *        through more calls to this function. Failure to use XTE_END_OF_PACKET
 *        prior to calling XTemac_FifoSend() may cause a packet FIFO underrun.
 *
 * @return
 * - XST_SUCCESS if the data was transferred to the FIFO.
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 * - XST_PFIFO_ERROR if there was a packet FIFO overflow during the transfer.
 *   This is a fatal condition. If this value is returned in polled mode, then
 *   the device must be reset. For interrupt driven modes, an interrupt will be
 *   asserted resulting in a call to the registered error handler which should
 *   handle reset of the device.
 * - XST_IPIF_ERROR if a data or bus error occurred within the TEMAC's IPIF.
 *   Like the PFIFO error, this is a fatal condition and should be handled
 *   in the same manner.
 *
 * @note
 * Calling this function with ByteCount = 0 will not result in the transfer of
 * data from BufPtr to the FIFO. However, if at the same time Eop is set to
 * XTE_END_OF_PACKET, then all data previously written with this function is
 * guaranteed to be flushed into the packet FIFO and available for transmission
 * with XTemac_FifoSend().
 ******************************************************************************/
int XTemac_FifoWrite(XTemac *InstancePtr, void *BufPtr, u32 ByteCount, int Eop)
{
	u32 RegDISR;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(!
			((Eop != XTE_END_OF_PACKET) &&
			 (Eop != XTE_PARTIAL_PACKET)));

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Transfer the data using the best/fastest method */
	InstancePtr->SendFifo.XferFn(&InstancePtr->SendFifo, BufPtr, ByteCount,
				     Eop);

	/* Make sure the packet FIFO didn't report an error */
	RegDISR = XTemac_mGetIpifReg(XTE_DISR_OFFSET);
	if (RegDISR & XTE_DXR_SEND_FIFO_MASK) {
		/* Only bump stats in polled mode. For interrupt driven mode, this stat
		 * is bumped in XTemac_IntrFifoHandler()
		 */
		if (InstancePtr->Options & XTE_POLLED_OPTION) {
			XTemac_mBumpStats(TxPktFifoErrors, 1);
		}
		return (XST_PFIFO_ERROR);
	}

	/* Verify no IPIF errors */
	if (RegDISR & (XTE_DXR_DPTO_MASK | XTE_DXR_TERR_MASK)) {
		/* Only bump stats in polled mode. For interrupt driven mode, this stat
		 * is bumped in XTemac_IntrFifoHandler()
		 */
		if (InstancePtr->Options & XTE_POLLED_OPTION) {
			XTemac_mBumpStats(IpifErrors, 1);
		}
		return (XST_IPIF_ERROR);
	}

	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Initiate a transmit of one packet of data previously written with
 * XTemac_FifoWrite(). The given length in bytes is written to the transmit
 * length FIFO. There should be at least this many bytes in the packet FIFO
 * ready for transmit.
 *
 * If FIFO interrupts are enabled (see XTemac_IntrFifoEnable()), then upon
 * completion of the transmit, the registered XTemac_FifoSendHandler() is
 * invoked.
 *
 * If more bytes that are in the packet FIFO are specified in the TxByteCount
 * parameter, then a packet FIFO underrun error will result.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param TxByteCount is the number of bytes to transmit. Range is 1 to the
 *        total number of bytes available in the packet FIFO to be transmitted.
 *
 * @return
 * - XST_SUCCESS if transmit was initiated.
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 * - XST_FIFO_NO_ROOM if the transmit was not initiated because the transmit
 *   length FIFO was full. This is not a fatal condition. The user may need to
 *   wait for other packets to transmit before this condition clears itself.
 *
 ******************************************************************************/
int XTemac_FifoSend(XTemac *InstancePtr, u32 TxByteCount)
{
	u32 RegIPISR;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(TxByteCount != 0);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* See if transmit length FIFO is full. If it is, try to clear the
	 * status. If it the status remains, then return an error
	 */
	RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	if (RegIPISR & XTE_IPXR_XMIT_LFIFO_FULL_MASK) {
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
				   XTE_IPXR_XMIT_LFIFO_FULL_MASK);

		RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
		if (RegIPISR & XTE_IPXR_XMIT_LFIFO_FULL_MASK) {
			XTemac_mBumpStats(FifoErrors, 1);
			return (XST_FIFO_NO_ROOM);
		}
	}

	/* Start transmit */
	XTemac_mSetIpifReg(XTE_TPLR_OFFSET, TxByteCount);

	/* Return sucess */
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Return the length of a received packet. If a packet is waiting in the
 * receive packet FIFO, then it may be copied to a user buffer with
 * XTemac_FifoRead().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param ByteCountPtr is the length of the next received packet if the return
 *        status is XST_SUCCESS.
 *
 * @return
 * - XST_SUCCESS if a packet has been received and a value has been written to
 *   ByteCountPtr.
 * - XST_DEVICE_IS_STOPPED if the device has been stopped.
 * - XST_NO_DATA if no packet length is available. ByteCountPtr is not modified.
 *
 ******************************************************************************/
int XTemac_FifoRecv(XTemac *InstancePtr, u32 *ByteCountPtr)
{
	u32 RegIPISR;
	volatile u32 RegRSR;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(ByteCountPtr != NULL);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* If the receive length FIFO is empty, then there's no packet waiting */
	RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	if (!(RegIPISR & XTE_IPXR_RECV_DONE_MASK)) {
		return (XST_NO_DATA);
	}

	/* Get the length */
	*ByteCountPtr = XTemac_mGetIpifReg(XTE_RPLR_OFFSET);

	/* The IPXR_RECV_DONE_MASK status bit is tied to the RSR register. To clear
	 * this condition, read from the RSR (which has no information) then write
	 * to the IPISR register to ack the status.
	 */
	RegRSR = XTemac_mGetIpifReg(XTE_RSR_OFFSET);
	XTemac_mSetIpifReg(XTE_IPISR_OFFSET, XTE_IPXR_RECV_DONE_MASK);

	/* Return sucess */
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Copy data from the receive packet FIFO into a user buffer. The number of
 * bytes to copy is derived from XTemac_FifoRecv(). The packet data may be
 * copied out of the FIFO all at once or with multiple calls to this function.
 * The latter method supports systems that keep packet data in non-contiguous
 * memory regions. For example:
 * <pre>
 *    if (XTemac_FifoRecv(Tptr, &PacketLength) == XST_SUCCESS)
 *    {
 *       if (PacketLength > 14)
 *       {
 *          HeaderLength = 14;
 *          PayloadLength = PacketLength - HeaderLength;
 *
 *          Status =  XTemac_FifoRead(Tptr, UserHeaderBuf, HeaderLength,
 *                                    XTE_PARTIAL_PACKET);
 *          Status |= XTemac_FifoRead(Tptr, UserPayloadBuf, PayloadLength,
 *                                    XTE_END_OF_PACKET);
 *
 *          if (Status != XST_SUCCESS)
 *          {
 *             // handle error
 *          }
 *       }
 *    }
 * </pre>
 *
 * If the user's buffer is not aligned on a 4 byte boundary, then the transfer
 * may take longer to complete.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param BufPtr is the user buffer that will recieve packet data from the FIFO.
 *        The buffer may be on any alignment.
 * @param ByteCount is the number of bytes to transfer
 * @param Eop specifies whether the last byte read is the last byte of a packet.
 *        If set to XTE_END_OF_PACKET, then any partial bytes being buffered by
 *        the driver at the end of the transfer are discarded. These discarded
 *        bytes are filler provided by the hardware and have no meaning. If set
 *        to XTE_PARTIAL_PACKET, then more packet data is expected to be read
 *        through more calls to this function. Failure to use this parameter
 *        properly will result in undefined filler bytes being copied into
 *        BufPtr.
 *
 * @return
 * - XST_SUCCESS if the data was transferred to the user buffer
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 * - XST_NO_DATA if there was not enough data in the packet FIFO to satisfy the
 *   request.
 *
 * @note
 * Do not attempt to read more than one packets worth of data at a time with
 * this function.
 ******************************************************************************/
int XTemac_FifoRead(XTemac *InstancePtr, void *BufPtr, u32 ByteCount, int Eop)
{
	int Status;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(!
			((Eop != XTE_END_OF_PACKET) &&
			 (Eop != XTE_PARTIAL_PACKET)));

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Transfer the data using the best/fastest method */
	Status = InstancePtr->RecvFifo.XferFn(&InstancePtr->RecvFifo, BufPtr,
					      ByteCount, Eop);

	/* Return correct status */
	if (Status == XST_NO_DATA) {
		return (XST_NO_DATA);
	}
	else {
		return (XST_SUCCESS);
	}
}


/******************************************************************************/
/**
 * Retrieve the number of free bytes in the packet FIFOs.
 *
 * For the transmit packet FIFO, the number returned is the number of bytes
 * that can be written by XTemac_FifoWrite(). If a non-zero number is returned,
 * then at least 1 packet of that size can be transmitted.
 *
 * For the receive packet FIFO, the number returned is the number of bytes that
 * can arrive from an external Ethernet device. This number does not reflect
 * the state of the receive length FIFO. If this FIFO is full, then arriving
 * packets will get dropped by the HW if there is no place to store the length.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction selects which packet FIFO to examine. If XTE_SEND, then
 *        the transmit packet FIFO is selected. If XTE_RECV, then the receive
 *        packet FIFO is selected.
 *
 * @return
 * Number of bytes available in the selected packet FIFO.
 *
 ******************************************************************************/
u32 XTemac_FifoGetFreeBytes(XTemac *InstancePtr, u32 Direction)
{
	u32 RegIPISR;
	u32 Count;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(!(Direction & ~(XTE_SEND | XTE_RECV)));

	/* For the send direction, even though there may be room in the
	 * packet FIFO, the length FIFO may be full. When this is the case,
	 * another packet cannot be transmiited so return 0.
	 */
	if (Direction == XTE_SEND) {
		/* Check length FIFO */
		RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
		if (RegIPISR & XTE_IPXR_XMIT_LFIFO_FULL_MASK) {
			return (0);
		}

		/* Get FIFO entries */
		Count = XPF_V200A_GET_COUNT(&InstancePtr->SendFifo.Fifo);
	}

	/* Handle receive direction */
	else {
		Count = XPF_V200A_COUNT_MASK -
			XPF_V200A_GET_COUNT(&InstancePtr->RecvFifo.Fifo);
	}

	/* Multiply free entries by the width of the packet FIFO to arrive at
	 * bytes
	 */
	return (Count * InstancePtr->RecvFifo.Width);
}


/******************************************************************************/
/**
 * Query the device for the latest transmit status for FIFO direct frame
 * transfer mode. This function should be used for polled mode operation only.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param SendStatusPtr is the contents of the XTE_TSR_OFFSET register when the
 *        return code is XST_FAILURE. Otherwise 0 is returned.
 *
 * @return
 * - XST_NO_DATA if a transmit status is not currently available.
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 * - XST_NOT_POLLED if the device has not been set to polled mode.
 * - XST_SUCCESS if a transmit status was found and indicates that there was
 *   no error.
 * - XST_FIFO_ERROR if the transmit length or transmit status FIFOs error has
 *   been detected. If this error is returned, then the device must be reset
 *   before this function will return a valid transmit status indication.
 * - XST_PFIFO_ERROR if the transmit packet FIFO is deadlocked. If this error
 *   is returned, then the device must be reset before this function will
 *   return a valid transmit status indication
 * - XST_IPIF_ERROR if there has been a data phase timeout or transaction error
 *   in the IPIF. This is a fatal error.
 *
 * @note
 * When XST_FAILURE is returned with the XTE_TSR_PFIFOU_MASK bit set in the
 * SendStatusPtr parameter, then an attempt was made to transmit more data than
 * was present in the packet FIFO. No reset is required in this situation.
 *
 ******************************************************************************/
int XTemac_FifoQuerySendStatus(XTemac *InstancePtr, u32 *SendStatusPtr)
{
	u32 RegDISR;
	u32 RegIPISR;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID(SendStatusPtr != NULL);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Have to be in polled mode to use this function */
	if (!(InstancePtr->Options & XTE_POLLED_OPTION)) {
		return (XST_NOT_POLLED);
	}

	/* Make sure send packet FIFO isn't deadlocked */
	RegDISR = XTemac_mGetIpifReg(XTE_DISR_OFFSET);
	if (RegDISR & XTE_DXR_SEND_FIFO_MASK) {
		XTemac_mBumpStats(TxPktFifoErrors, 1);
		return (XST_PFIFO_ERROR);
	}

	/* Make sure no IPIF errors are present */
	if (RegDISR & (XTE_DXR_TERR_MASK | XTE_DXR_DPTO_MASK)) {
		XTemac_mBumpStats(IpifErrors, 1);
		return (XST_IPIF_ERROR);
	}

	/* Read the IPISR
	 * If any errors are detetected, try to clear and return error
	 */
	RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
	if (RegIPISR & XTE_IPXR_XMIT_ERROR_MASK) {
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
				   RegIPISR & XTE_IPXR_XMIT_ERROR_MASK);
		XTemac_mBumpStats(FifoErrors, 1);
		return (XST_FIFO_ERROR);
	}

	/* No FIFO errors, so see of a transmit has completed */
	if (!(RegIPISR & XTE_IPXR_XMIT_DONE_MASK)) {
		return (XST_NO_DATA);
	}

	/* Transmit has completed, get the status, ack the condition */
	*SendStatusPtr = XTemac_mGetIpifReg(XTE_TSR_OFFSET);
	XTemac_mSetIpifReg(XTE_IPISR_OFFSET, XTE_IPXR_XMIT_DONE_MASK);

	/* no errors to report */
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Query the device for the latest receive status for FIFO direct frame
 * transfer mode. This function should be used for polled mode operation only.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return
 * - XST_SUCCESS if a frame has been received and no receive error was detected.
 * - XST_DEVICE_IS_STOPPED if the device has not been started.
 * - XST_NO_DATA if no frame has been received and no receive related error has
 *   been detected.
 * - XST_NOT_POLLED if the device has not been set to polled mode.
 * - XST_DATA_LOST if the device reports that it dropped a receive frame. This
 *   is not a serious problem but may indicate that frames are arriving faster
 *   than the system can process them.
 * - XST_FIFO_ERROR if an error was detected with the receive length FIFO. If
 *   this error is returned, then the device must be reset before any new frame
 *   can be received.
 * - XST_PFIFO_ERROR if the receive packet FIFO is deadlocked. If this error is
 *   returned, then the device must be reset before any new frame can be
 *   received.
 * - XST_IPIF_ERROR if there has been a data phase timeout or transaction error
 *   in the IPIF. This is a fatal error.
 *
 * @note
 * In situations where simultaneously a frame has been received for which an
 * XST_SUCCESS can be returned and a dropped frame for which an XST_DATA_LOST
 * can be returned, then this function will give priority to XST_SUCCESS so the
 * user can receive the frame.
 ******************************************************************************/
int XTemac_FifoQueryRecvStatus(XTemac *InstancePtr)
{
	u32 RegDISR;
	u32 RegIPISR;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STOPPED);
	}

	/* Have to be in polled mode to use this function */
	if (!(InstancePtr->Options & XTE_POLLED_OPTION)) {
		return (XST_NOT_POLLED);
	}

	/* Read the DISR */
	RegDISR = XTemac_mGetIpifReg(XTE_DISR_OFFSET);

	/* Make sure recv packet FIFO isn't deadlocked */
	if (RegDISR & XTE_DXR_RECV_FIFO_MASK) {
		XTemac_mBumpStats(RxPktFifoErrors, 1);
		return (XST_PFIFO_ERROR);
	}

	/* Make sure no IPIF errors are present */
	if (RegDISR & (XTE_DXR_TERR_MASK | XTE_DXR_DPTO_MASK)) {
		XTemac_mBumpStats(IpifErrors, 1);
		return (XST_IPIF_ERROR);
	}

	/* Read the IPISR */
	RegIPISR = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);

	/* Check for other recv related FIFO errors */
	if (RegIPISR & (XTE_IPXR_RECV_ERROR_MASK - XTE_IPXR_RECV_DROPPED_MASK)) {
		XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
				   RegIPISR & XTE_IPXR_RECV_ERROR_MASK);
		XTemac_mBumpStats(FifoErrors, 1);
		return (XST_FIFO_ERROR);
	}

	/* See if a frame has been received */
	if (RegIPISR & XTE_IPXR_RECV_DONE_MASK) {
		return (XST_SUCCESS);
	}

	/* If option to detect recv reject errors is set, check for rejected
	 * receive frames. If one is detected, clear it and return error.
	 */
	if (InstancePtr->Options & XTE_REPORT_RXERR_OPTION) {
		if (RegIPISR & XTE_IPXR_RECV_DROPPED_MASK) {
			XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
					   RegIPISR &
					   XTE_IPXR_RECV_DROPPED_MASK);
			return (XST_DATA_LOST);
		}
	}

	/* No frame has been received and no errors detected */
	return (XST_NO_DATA);
}


/*******************************************************************************
* Algorithm to write to a 64 bit wide transmit packet FIFO through the holding
* buffer.
*
* @param FPtr is a pointer to a Temac FIFO instance to worked on.
* @param BufPtr is the source buffer address on any alignment
* @param ByteCount is the number of bytes to transfer
* @param Eop specifies whether the last byte written is the last byte of the
*        packet.
*
* @return XST_SUCCESS
*******************************************************************************/
static int Write_64(XTemac_PacketFifo *Fptr, void *BufPtr,
		    u32 ByteCount, int Eop)
{
	unsigned BufAlignment = (unsigned) BufPtr & 3;
	unsigned PartialBytes;
	unsigned HoldAlignment = mHold_GetIndex(Fptr);

	/* Case 1: Buffer aligned on 4-byte boundary and Hold is empty
	 *
	 *   1. Write all bytes using the fastest transfer method
	 */
	if ((BufAlignment == 0) && (mHoldS_IsEmpty(Fptr))) {
		Write64_Aligned(Fptr, (u32 *) BufPtr, ByteCount);
	}

	/* Case 2: Buffer and Hold are byte aligned with each other
	 *
	 *   1. Transfer enough bytes from the buffer to the Hold to trigger a flush
	 *      to the FIFO.
	 *
	 *   2. The state of the buffer and Hold are as described by Case 1 so
	 *      write remaining bytes using the fastest transfer method
	 */
	else if (BufAlignment == (HoldAlignment % PFIFO_64BIT_WIDTH_BYTES)) {
		PartialBytes = PFIFO_64BIT_WIDTH_BYTES - HoldAlignment;

		if (ByteCount < PartialBytes) {
			PartialBytes = ByteCount;
		}

		Write64_Unaligned(Fptr, BufPtr, PartialBytes);
		Write64_Aligned(Fptr, (u32 *) ((u32) BufPtr + PartialBytes),
				ByteCount - PartialBytes);
	}

	/* Case 3: No alignment to take advantage of
	 *
	 *    1. Read FIFOs using the slower method.
	 */
	else {
		Write64_Unaligned(Fptr, BufPtr, ByteCount);
	}

	/* If TxBytes is non-zero then the caller wants to transmit data from the
	 * FIFO
	 */
	if (Eop == XTE_END_OF_PACKET) {
		/* Push the hold to the FIFO if data is present */
		if (!mHoldS_IsEmpty(Fptr)) {
			mPush64(Fptr);
			mHoldS_SetEmpty(Fptr);
		}
	}

	return (XST_SUCCESS);
}


/*******************************************************************************
* Algorithm to read from a 64 bit wide receive packet FIFO with through the
* holding buffer.
*
* @param Fptr is a pointer to a Temac FIFO instance to worked on.
* @param BufPtr is the destination address on any alignment
* @param ByteCount is the number of bytes to transfer
*
* @return XST_SUCCESS if transfer completed or XST_NO_DATA if the amount of
*         data being buffered by the driver plus the amount of data in the
*         packet FIFO is not enough to satisfy the number of bytes requested
*         by the ByteCount parameter.
*******************************************************************************/
static int Read_64(XTemac_PacketFifo *Fptr, void *BufPtr,
		   u32 ByteCount, int Eop)
{
	unsigned BufAlignment = (unsigned) BufPtr & 3;
	unsigned PartialBytes;
	unsigned MaxBytes;
	unsigned HoldAlignment = mHold_GetIndex(Fptr);

	/* Determine how many bytes can be read from the packet FIFO */
	MaxBytes = XPF_V200A_COUNT_MASK & XPF_V200A_GET_COUNT(&Fptr->Fifo);
	MaxBytes *= PFIFO_64BIT_WIDTH_BYTES;

	/* Case 1: Buffer aligned on 4-byte boundary and Hold is empty
	 *
	 *   1. Read all bytes using the fastest transfer method
	 */
	if ((BufAlignment == 0) && (mHoldR_IsEmpty(Fptr))) {
		/* Enough data in fifo? */
		if (ByteCount > MaxBytes) {
			return (XST_NO_DATA);
		}

		Read64_Aligned(Fptr, (u32 *) BufPtr, ByteCount);
	}

	/* Case 2: Buffer and Hold are byte aligned with each other
	 *
	 *   1. Transfer enough bytes from the Hold to the buffer to trigger a
	 *      read from the FIFO.
	 *
	 *   2. The state of the buffer and Hold are now as described by Case 1 so
	 *      read remaining bytes using the fastest transfer method
	 */
	else if (BufAlignment == (HoldAlignment % PFIFO_64BIT_WIDTH_BYTES)) {
		PartialBytes = PFIFO_64BIT_WIDTH_BYTES - HoldAlignment;

		if (ByteCount < PartialBytes) {
			PartialBytes = ByteCount;
		}

		/* Enough data in fifo? Must account for the number of bytes the driver
		 * is currently buffering
		 */
		if (ByteCount > (MaxBytes + PartialBytes)) {
			return (XST_NO_DATA);
		}

		Read64_Unaligned(Fptr, BufPtr, PartialBytes);
		Read64_Aligned(Fptr, (u32 *) ((u32) BufPtr + PartialBytes),
			       ByteCount - PartialBytes);
	}

	/* Case 3: No alignment to take advantage of
	 *
	 *    1. Read FIFOs using the slower method.
	 */
	else {
		/* Enough data in fifo? Must account for the number of bytes the driver
		 * is currently buffering
		 */
		PartialBytes = PFIFO_64BIT_WIDTH_BYTES - HoldAlignment;
		if (ByteCount > (MaxBytes + PartialBytes)) {
			return (XST_NO_DATA);
		}

		Read64_Unaligned(Fptr, BufPtr, ByteCount);
	}

	/* If this marks the end of packet, then dump any remaining data in the
	 * hold. The dumped data in this context is meaningless.
	 */
	if (Eop == XTE_END_OF_PACKET) {
		mHoldR_SetEmpty(Fptr);
	}

	return (XST_SUCCESS);
}


/*******************************************************************************
* Write to the 64 bit holding buffer. Each time it becomes full, then it is
* pushed to the transmit FIFO.
*
* @param F is a pointer to the packet FIFO instance to be worked on.
* @param BufPtr is the source buffer address on any alignment
* @param ByteCount is the number of bytes to transfer
*
*******************************************************************************/
static void Write64_Unaligned(XTemac_PacketFifo *F, void *BufPtr, u32 ByteCount)
{
	u8 *SrcPtr = (u8 *) BufPtr;
	unsigned FifoTransfersLeft;
	unsigned PartialBytes;
	unsigned BytesLeft;
	unsigned i;

	/* Stage 1: The hold may be partially full. Write enough bytes to it to
	 * cause a push to the FIFO
	 */

	/* Calculate the number of bytes needed to trigger a push, if not enough
	 * bytes have been specified to cause a push, then adjust accordingly
	 */
	i = mHold_GetIndex(F);
	PartialBytes = PFIFO_64BIT_WIDTH_BYTES - i;
	if (PartialBytes > ByteCount) {
		PartialBytes = ByteCount;
	}

	/* Calculate the number of bytes remaining after the first push */
	BytesLeft = ByteCount - PartialBytes;

	/* Write to the hold and advance its index */
	mHold_Advance(F, PartialBytes);

	while (PartialBytes--) {
		mHold_CopyIn(F, i, *SrcPtr);
		SrcPtr++;
		i++;
	}

	/* Push to fifo if needed */
	if (mHoldS_IsFull(F)) {
		mPush64(F);
		mHoldS_SetEmpty(F);
	}

	/* No more data to process */
	if (!BytesLeft) {
		return;
	}

	/* Stage 2: The hold is empty now, if any more bytes are left to process, then
	 * it will begin with nothing in the hold. Use the hold as a temporary storage
	 * area to contain the data.
	 *
	 * The hold is filled then pushed out to the FIFOs a number of times based on
	 * how many bytes are left to process.
	 */

	/* Calculate the number of times a push will need to occur */
	FifoTransfersLeft = BytesLeft / PFIFO_64BIT_WIDTH_BYTES;

	/* Calculate the number of partial bytes left after this stage */
	PartialBytes =
		BytesLeft - (FifoTransfersLeft * PFIFO_64BIT_WIDTH_BYTES);

	/* Write to the hold and push data to the FIFO */
	while (FifoTransfersLeft--) {
		for (i = 0; i < PFIFO_64BIT_WIDTH_BYTES; i++) {
			mHold_CopyIn(F, i, *SrcPtr);
			SrcPtr++;
		}
		mPush64(F);
	}

	/* No more data to process
	 * HoldIndex was left at 0 by stage 1, at this point, that is
	 * still the correct value.
	 */
	if (!PartialBytes) {
		return;
	}

	/* Stage 3: All that is left is to fill the hold with the remaining data
	 * to be processed. There will be no push to the FIFO because there is not
	 * enough data left to cause one.
	 */

	/* Write to the hold and push data to the FIFO */
	for (i = 0; i < PartialBytes; i++) {
		mHold_CopyIn(F, i, *SrcPtr);
		SrcPtr++;
	}

	/* Set the hold's index to its final correct value */
	mHold_SetIndex(F, PartialBytes);
}


/*******************************************************************************
* Write directly to the 64 bit wide transmit FIFO from an aligned source
* buffer. Leftover bytes are written to the holding buffer.
*
* @param F is a pointer to the packet FIFO instance to be worked on.
* @param BufPtr is the source buffer address on 32-bit alignment
* @param ByteCount is the number of bytes to transfer
*
*******************************************************************************/
static void Write64_Aligned(XTemac_PacketFifo *F, u32 *BufPtr, u32 ByteCount)
{
	unsigned FifoTransfersLeft = ByteCount / PFIFO_64BIT_WIDTH_BYTES;
	unsigned PartialBytes = ByteCount & (PFIFO_64BIT_WIDTH_BYTES - 1);

	/* Direct transfer */
	while (FifoTransfersLeft--) {
		mWriteFifo64(F, BufPtr);
		BufPtr += 2;
	}

	/* Leftover bytes are left in the holding area */
	if (PartialBytes) {
		Write64_Unaligned(F, BufPtr, PartialBytes);
	}
}


/*******************************************************************************
* Read into the 64 bit holding buffer from the receive packet FIFO.
* Each time the holding buffer becomes full, then it is flushed to the
* provided buffer.
*
* @param F is a pointer to the packet FIFO instance to be worked on.
* @param BufPtr is the destination buffer address on any alignment
* @param ByteCount is the number of bytes to transfer
*
*******************************************************************************/
static void Read64_Unaligned(XTemac_PacketFifo *F, void *BufPtr, u32 ByteCount)
{
	u8 *DestPtr = (u8 *) BufPtr;
	unsigned FifoTransfersLeft;
	unsigned PartialBytes;
	unsigned BytesLeft;
	unsigned i;

	/* Stage 1: The hold may have some residual bytes that must be flushed
	 * to the buffer before anything is read from the FIFO
	 */

	/* Calculate the number of bytes to flush to the buffer from the hold.
	 * If the number of bytes to flush is greater than the "Bytes" requested,
	 * then adjust accordingly.
	 */
	i = mHold_GetIndex(F);
	PartialBytes = PFIFO_64BIT_WIDTH_BYTES - i;

	if (PartialBytes > ByteCount) {
		PartialBytes = ByteCount;
	}

	/* Calculate the number of bytes remaining after flushing to the buffer */
	BytesLeft = ByteCount - PartialBytes;

	/* Move the hold's index forward */
	mHold_Advance(F, PartialBytes);

	/* Copy bytes */
	while (PartialBytes--) {
		mHold_CopyOut(F, i, *DestPtr);
		i++;
		DestPtr++;
	}

	/* No more data to process */
	if (!BytesLeft) {
		return;
	}

	/* Stage 2: The hold is empty now, if any more bytes are left to process, then
	 * it will begin with nothing in the hold. Use the hold as a temporary storage
	 * area to contain the data.
	 *
	 * The hold is filled with FIFO data, then that data is written to the buffer.
	 * Do this FifoTransfersLeft times
	 */

	/* Calculate the number of times a push will need to occur */
	FifoTransfersLeft = BytesLeft / PFIFO_64BIT_WIDTH_BYTES;

	/* Calculate the number of partial bytes left after this stage */
	PartialBytes =
		BytesLeft - (FifoTransfersLeft * PFIFO_64BIT_WIDTH_BYTES);

	/* Write to the hold and push data to the FIFO */
	while (FifoTransfersLeft--) {
		/* Load the hold with the next data set from the FIFO */
		mPop64(F);

		/* Write hold to buffer */
		for (i = 0; i < PFIFO_64BIT_WIDTH_BYTES; i++) {
			mHold_CopyOut(F, i, *DestPtr);
			DestPtr++;
		}
	}

	/* No more data to process
	 * After processing full FIFO chunks of data, the hold is empty at this
	 * point
	 */
	if (!PartialBytes) {
		return;
	}

	/* Stage 3: All that is left is to fill the hold one more time with FIFO
	 * data, then write the remaining requested bytes to the buffer
	 */

	/* Get FIFO data */
	mPop64(F);

	/* Copy bytes from the hold to the buffer */
	for (i = 0; i < PartialBytes; i++) {
		mHold_CopyOut(F, i, *DestPtr);
		DestPtr++;
	}

	/* Set the hold's index to its final correct value */
	mHold_SetIndex(F, PartialBytes);
}


/*******************************************************************************
* Read directly from the 64 bit wide receive FIFO into an aligned destination
* buffer. Leftover bytes are written to the holding buffer.
*
* @param F is a pointer to the packet FIFO instance to be worked on.
* @param BufPtr is the destination buffer address on 32-bit alignment
* @param ByteCount is the number of bytes to transfer
*
*******************************************************************************/
static void Read64_Aligned(XTemac_PacketFifo *F, u32 *BufPtr, u32 ByteCount)
{
	unsigned FifoTransfersLeft = ByteCount / PFIFO_64BIT_WIDTH_BYTES;
	unsigned PartialBytes = ByteCount & (PFIFO_64BIT_WIDTH_BYTES - 1);

	/* Direct transfer */
	while (FifoTransfersLeft--) {
		mReadFifo64(F, BufPtr);
		BufPtr += 2;
	}

	/* Leftover bytes are left in the holding area */
	if (PartialBytes) {
		Read64_Unaligned(F, BufPtr, PartialBytes);
	}
}
