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
*
******************************************************************************/
/*****************************************************************************/
/*
* @file xstreamer.c
*
* See xtreamer.h for a description on how to use this driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a jvb  10/13/06 First release - based on Robert McGee's streaming packet
*                     fifo driver.
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xstreamer.h"

/*
 * Implementation Notes
 *
 * --- Receive ---
 *
 * The basic algorithm for receiving bytes through this byte streamer copies a
 * fifo key-hole width chunk from the fifo into a holding buffer and then doles
 * out the bytes from the holding buffer. In some cases, when the buffer given
 * happens to already be aligned in memory, this algorithm will bypass the
 * holding buffer.
 *
 * Here is a picture to depict this process:
 *
 * Initial state:                 holding buffer
 *                                +--------------+
 *                                |   <empty>    |
 *                                +--------------+
 *                                               ^
 *                                               |
 *                                             index
 *
 * during XStrm_Read():
 * first holding buffer fill:     holding buffer
 *                                +--------------+
 *                                |////<full>////|
 *                                +--------------+
 *                                ^
 *                                |
 *                              index
 *
 * first holding buffer read:     holding buffer
 *                                 read   unread
 *                                +--------------+
 *                                |      |///////|
 *                                +--------------+
 *                                       ^
 *                                       |
 *                                     index
 *
 * ...
 *
 * last holding buffer read:     holding buffer
 *                                +--------------+
 *                                |   <empty>    |
 *                                +--------------+
 *                                               ^
 *                                               |
 *                                             index
 *
 * repeat this process ^^^
 *
 *
 * --- Transmit ---
 *
 * The basic algorithm for transmitting bytes through this byte streamer copies
 * bytes into a holding buffer and then writes the holding buffer into the fifo
 * when it is full.  In some cases, when the buffer given happens to already be
 * aligned in memory, this algorithm will bypass the holding buffer.
 *
 * Here is a picture to depict this process:
 *
 * Initial state:                 holding buffer
 *                                +--------------+
 *                                |   <empty>    |
 *                                +--------------+
 *                                ^
 *                                |
 *                              index
 *
 * during XStrm_Write():
 * first holding buffer write:    holding buffer
 *                                 writen  empty
 *                                +--------------+
 *                                |//////|       |
 *                                +--------------+
 *                                       ^
 *                                       |
 *                                     index
 *
 * ...
 * last holding buffer write:     holding buffer
 *                                +--------------+
 *                                |////<full>////|
 *                                +--------------+
 *                                               ^
 *                                               |
 *                                             index
 *
 * holding buffer flush:          holding buffer
 *                                +--------------+
 *                                |   <empty>    |
 *                                +--------------+
 *                                ^
 *                                |
 *                              index
 *
 * repeat this process ^^^
 */

#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

/*****************************************************************************/
/*
*
* XStrm_RxInitialize initializes the XStrm_RxFifoStreamer object referenced by
* <i>InstancePtr</i>.
*
* @param    InstancePtr references the tx streamer on which to operate.
*
* @param    FifoWidth specifies the FIFO keyhole size in bytes.
*
* @param    FifoInstance references the FIFO driver instance that this streamer
*           object should use to transfer data into the the actual fifo.
*
* @param    ReadFn specifies a routine to use to read data from the actual
*           FIFO. It is assumed that this read routine will handle only reads
*           from an aligned buffer. (Otherwise, why are we using this streamer
*           driver?)
*
* @param    GetLenFn specifies a routine to use to initiate a receive on the
*           actual FIFO.
*
* @param    GetOccupancyFn specifies a routine to use to retrieve the occupancy
*           in the actual FIFO. The true occupancy value needs to come through
*           this streamer driver becuase it holds some of the bytes.
*
* @return   N/A
*
******************************************************************************/
void XStrm_RxInitialize(XStrm_RxFifoStreamer * InstancePtr,
			unsigned FifoWidth, void *FifoInstance,
			XStrm_XferFnType ReadFn,
			XStrm_GetLenFnType GetLenFn,
			XStrm_GetOccupancyFnType GetOccupancyFn)
{
	/* Verify arguments */
	XASSERT_VOID(InstancePtr != NULL);

	InstancePtr->HeadIndex = FifoWidth;
	InstancePtr->FifoWidth = FifoWidth;
	InstancePtr->FifoInstance = FifoInstance;
	InstancePtr->ReadFn = ReadFn;
	InstancePtr->GetLenFn = GetLenFn;
	InstancePtr->GetOccupancyFn = GetOccupancyFn;
}

/*****************************************************************************/
/*
*
* XStrm_TxInitialize initializes the XStrm_TxFifoStreamer object referenced by
* <i>InstancePtr</i>.
*
* @param    InstancePtr references the tx streamer on which to operate.
*
* @param    FifoWidth specifies the FIFO keyhole size in bytes.
*
* @param    FifoInstance references the FIFO driver instance that this streamer
*           object should use to transfer data into the the actual fifo.
*
* @param    WriteFn specifies a routine to use to write data into the actual
*           FIFO. It is assumed that this write routine will handle only writes
*           from an aligned buffer. (Otherwise, why are we using this streamer
*           driver?)
*
* @param    SetLenFn specifies a routine to use to initiate a transmit on the
*           actual FIFO.
*
* @param    GetVacancyFn specifies a routine to use to retrieve the vacancy in
*           the actual FIFO. The true vacancy value needs to come through this
*           streamer driver becuase it holds some of the bytes.
*
* @return   N/A
*
******************************************************************************/
void XStrm_TxInitialize(XStrm_TxFifoStreamer * InstancePtr, unsigned FifoWidth,
			void *FifoInstance, XStrm_XferFnType WriteFn,
			XStrm_SetLenFnType SetLenFn,
			XStrm_GetVacancyFnType GetVacancyFn)
{
	/* Verify arguments */
	XASSERT_VOID(InstancePtr != NULL);

	InstancePtr->TailIndex = 0;
	InstancePtr->FifoWidth = FifoWidth;
	InstancePtr->FifoInstance = FifoInstance;
	InstancePtr->WriteFn = WriteFn;
	InstancePtr->SetLenFn = SetLenFn;
	InstancePtr->GetVacancyFn = GetVacancyFn;
}

/*****************************************************************************/
/*
*
* XStrm_RxGetLen notifies the hardware that the program is ready to receive the
* next frame from the receive channel of the FIFO, specified by
* <i>InstancePtr</i>.
*
* Note that the program must first call XStrm_RxGetLen before pulling data
* out of the receive channel of the FIFO with XStrm_Read.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   XStrm_RxGetLen returns the number of bytes available in the next
*           frame.
*
******************************************************************************/
u32 XStrm_RxGetLen(XStrm_RxFifoStreamer * InstancePtr)
{
	u32 len;

	InstancePtr->HeadIndex = InstancePtr->FifoWidth;
	len = (*InstancePtr->GetLenFn) (InstancePtr->FifoInstance);
	InstancePtr->FrmByteCnt = len;
	return len;
}

/*****************************************************************************/
/*
*
* XStrm_Read reads <i>Bytes</i> bytes from the FIFO specified by
* <i>InstancePtr</i> to the block of memory, referenced by <i>BufPtr</i>. 
*
* Care must be taken to ensure that the number of bytes read with one or more
* calls to XStrm_Read() does not exceed the number of bytes available given
* from the last call to XStrm_RxGetLen().
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @param    BufPtr specifies the memory address to place the data read.
*
* @param    Bytes specifies the number of bytes to read.
*
* @return   N/A
*
******************************************************************************/
void XStrm_Read(XStrm_RxFifoStreamer * InstancePtr, void *BufPtr,
		unsigned Bytes)
{
	u8 *DestPtr = (u8 *) BufPtr;
	unsigned BytesRemaining = Bytes;
	unsigned FifoWordsToXfer;
	unsigned PartialBytes;
	unsigned i;

	while (BytesRemaining) {
		xdbg_printf(XDBG_DEBUG_FIFO_RX,
			    "XStrm_Read: BytesRemaining: %d\n", BytesRemaining);
		/* Case 1: There are bytes in the holding buffer
		 * 
		 *   1) Read the bytes from the holding buffer to the target buffer.
		 *   2) Loop back around and handle the rest of the transfer.
		 */
		if (InstancePtr->HeadIndex != InstancePtr->FifoWidth) {
			xdbg_printf(XDBG_DEBUG_FIFO_RX,
				    "XStrm_Read: Case 1: InstancePtr->HeadIndex [%d] != InstancePtr->FifoWidth [%d]\n",
				    InstancePtr->HeadIndex,
				    InstancePtr->FifoWidth);
			i = InstancePtr->HeadIndex;

			PartialBytes = min(BytesRemaining,
					   InstancePtr->FifoWidth -
					   InstancePtr->HeadIndex);
			InstancePtr->HeadIndex += PartialBytes;
			BytesRemaining -= PartialBytes;
			InstancePtr->FrmByteCnt -= PartialBytes;
			while (PartialBytes--) {
				*DestPtr = InstancePtr->AlignedBuffer.bytes[i];
				i++;
				DestPtr++;
			}
		}
		/* Case 2: There are no more bytes in the holding buffer and
		 *         the target buffer is 32 bit aligned and
		 *         the number of bytes remaining to transfer is greater
		 *         than or equal to the fifo width.
		 *
		 *   1) We can go fast by reading a long string of fifo words right out
		 *      of the fifo into the target buffer.
		 *   2) Loop back around to transfer the last few bytes.
		 */
		else if ((((unsigned) DestPtr & 3) == 0) &&
			 (BytesRemaining >= InstancePtr->FifoWidth)) {
			xdbg_printf(XDBG_DEBUG_FIFO_RX,
				    "XStrm_Read: Case 2: DestPtr: %p, BytesRemaining: %d, InstancePtr->FifoWidth: %d\n",
				    DestPtr, BytesRemaining,
				    InstancePtr->FifoWidth);
			FifoWordsToXfer =
				BytesRemaining / InstancePtr->FifoWidth;

			(*(InstancePtr->ReadFn)) (InstancePtr->FifoInstance,
						  DestPtr, FifoWordsToXfer);
			DestPtr += FifoWordsToXfer * InstancePtr->FifoWidth;
			BytesRemaining -=
				FifoWordsToXfer * InstancePtr->FifoWidth;
			InstancePtr->FrmByteCnt -=
				FifoWordsToXfer * InstancePtr->FifoWidth;
		}
		/* Case 3: There are no more bytes in the holding buffer and
		 *         the number of bytes remaining to transfer is less than
		 *         the fifo width or
		 *         things just don't line up.
		 *
		 *   1) Fill the holding buffer.
		 *   2) Loop back around and handle the rest of the transfer.
		 */
		else {
			xdbg_printf(XDBG_DEBUG_FIFO_RX, "XStrm_Read: Case 3\n");
			/*
			 * At the tail end, read one fifo word into the local holding
			 * buffer and loop back around to take care of the transfer.
			 */
			(*InstancePtr->ReadFn) (InstancePtr->FifoInstance,
						&(InstancePtr->AlignedBuffer.
						  bytes[0]), 1);
			InstancePtr->HeadIndex = 0;
		}
	}
}

/*****************************************************************************/
/*
*
* XStrm_TxSetLen flushes to the FIFO, specified by <i>InstancePtr</i>, any
* bytes remaining in internal buffers and begins a hardware transfer of data
* out of the transmit channel of the FIFO. <i>Bytes</i> specifies the number
* of bytes in the frame to transmit.
*
* @param    InstancePtr references the FIFO Streamer on which to operate.
*
* @param    Bytes specifies the frame length in bytes.
*
* @return   N/A
*
******************************************************************************/
void XStrm_TxSetLen(XStrm_TxFifoStreamer * InstancePtr, u32 Bytes)
{
	/*
	 * First flush what's in the holding buffer
	 */
	if (InstancePtr->TailIndex != 0) {
		(*InstancePtr->WriteFn) (InstancePtr->FifoInstance,
					 &(InstancePtr->AlignedBuffer.bytes[0]),
					 1);
		InstancePtr->TailIndex = 0;
	}

	/*
	 * Kick off the hw write
	 */
	(*(InstancePtr)->SetLenFn) (InstancePtr->FifoInstance, Bytes);
}

/*****************************************************************************/
/*
*
* XStrm_Write writes <i>Bytes</i> bytes of the block of memory, referenced by
* <i>BufPtr</i>, to the transmit channel of the FIFO referenced by
* <i>InstancePtr</i>. 
*
* Care must be taken to ensure that the number of bytes written with one or
* more calls to XStrm_Write() matches the number of bytes given in the next
* call to XStrm_TxSetLen().
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @param    BufPtr specifies the memory address of data to write.
*
* @param    Bytes specifies the number of bytes to write.
*
* @return   N/A
*
******************************************************************************/
void XStrm_Write(XStrm_TxFifoStreamer * InstancePtr, void *BufPtr,
		 unsigned Bytes)
{
	u8 *SrcPtr = (u8 *) BufPtr;
	unsigned BytesRemaining = Bytes;
	unsigned FifoWordsToXfer;
	unsigned PartialBytes;
	unsigned i;

	while (BytesRemaining) {
		xdbg_printf(XDBG_DEBUG_FIFO_TX,
			    "XStrm_Write: BytesRemaining: %d\n",
			    BytesRemaining);
		/* Case 1: The holding buffer is full
		 * 
		 *   1) Write it to the fifo.
		 *   2) Fall through to transfer more bytes in this iteration.
		 */
		if (InstancePtr->TailIndex == InstancePtr->FifoWidth) {
			xdbg_printf(XDBG_DEBUG_FIFO_TX,
				    "XStrm_Write: (case 1) TailIndex: %d; FifoWidth: %d; WriteFn: %p\n",
				    InstancePtr->TailIndex,
				    InstancePtr->FifoWidth,
				    InstancePtr->WriteFn);
			(*InstancePtr->WriteFn) (InstancePtr->FifoInstance,
						 &(InstancePtr->AlignedBuffer.
						   bytes[0]), 1);
			InstancePtr->TailIndex = 0;
		}
		/* Case 2: There are no bytes in the holding buffer and
		 *         the target buffer is 32 bit aligned and
		 *         the number of bytes remaining to transfer is greater
		 *         than or equal to the fifo width.
		 *
		 *   1) We can go fast by writing a long string of fifo words right out
		 *      of the source buffer into the fifo.
		 *   2) Loop back around to transfer the last few bytes.
		 */
		if ((InstancePtr->TailIndex == 0) &&
		    (BytesRemaining >= InstancePtr->FifoWidth) &&
		    (((unsigned) SrcPtr & 3) == 0)) {
			FifoWordsToXfer =
				BytesRemaining / InstancePtr->FifoWidth;

			xdbg_printf(XDBG_DEBUG_FIFO_TX,
				    "XStrm_Write: (case 2) TailIndex: %d; BytesRemaining: %d; FifoWidth: %d; SrcPtr: %p;\n InstancePtr: %p; WriteFn: %p (XLlFifo_iWrite_Aligned: %p),\nFifoWordsToXfer: %d (BytesRemaining: %d)\n",
				    InstancePtr->TailIndex, BytesRemaining,
				    InstancePtr->FifoWidth, SrcPtr, InstancePtr,
				    InstancePtr->WriteFn,
				    XLlFifo_iWrite_Aligned, FifoWordsToXfer,
				    BytesRemaining);

			(*InstancePtr->WriteFn) (InstancePtr->FifoInstance,
						 SrcPtr, FifoWordsToXfer);
			SrcPtr += FifoWordsToXfer * InstancePtr->FifoWidth;
			BytesRemaining -=
				FifoWordsToXfer * InstancePtr->FifoWidth;
			xdbg_printf(XDBG_DEBUG_FIFO_TX,
				    "XStrm_Write: (end case 2) TailIndex: %d; BytesRemaining: %d; SrcPtr: %p\n",
				    InstancePtr->TailIndex, BytesRemaining,
				    SrcPtr);
		}
		/* Case 3: The alignment of the "galaxies" didn't occur in
		 *         Case 2 above, so we must pump the bytes through the
		 *         holding buffer.
		 * 
		 *   1) Write bytes from the source buffer to the holding buffer
		 *   2) Loop back around and handle the rest of the transfer.
		 */
		else {
			i = InstancePtr->TailIndex;

			PartialBytes =
				min(BytesRemaining,
				    InstancePtr->FifoWidth -
				    InstancePtr->TailIndex);
			BytesRemaining -= PartialBytes;
			InstancePtr->TailIndex += PartialBytes;
			while (PartialBytes--) {
				xdbg_printf(XDBG_DEBUG_FIFO_TX,
					    "XStrm_Write: (case 3) PartialBytes: %d\n",
					    PartialBytes);
				InstancePtr->AlignedBuffer.bytes[i] = *SrcPtr;
				i++;
				SrcPtr++;
			}
		}
	}
}
