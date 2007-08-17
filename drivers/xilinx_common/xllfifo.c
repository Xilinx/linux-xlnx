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
/**
 *
 * @file llfifo.c
 *
 * The Xilinx local link FIFO driver component. This driver supports the
 * Xilinx xps_ll_fifo core.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jvb  10/13/06 First release
 * </pre>
 ******************************************************************************/


/***************************** Include Files *********************************/

#include <linux/string.h>

#include "xllfifo_hw.h"
#include "xllfifo.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

#define FIFO_WIDTH_BYTES 4

/*
 * Implementation Notes:
 *
 * This Fifo driver makes use of a byte streamer driver (xstreamer.h). The code
 * is structured like so:
 *
 * +--------------------+
 * |     llfifo         |
 * |   +----------------+
 * |   | +--------------+
 * |   | |  xstreamer   |
 * |   | +--------------+
 * |   +----------------+
 * |                    |
 * +--------------------+
 *
 * Initialization
 * At initialization time this driver (llfifo) sets up the streamer objects to
 * use routines in this driver (llfifo) to perform the actual I/O to the H/W
 * FIFO core.
 *
 * Operation
 * Once the streamer objects are set up, the API routines in this driver, just
 * call through to the streamer driver to perform the read/write operations.
 * The streamer driver will eventually make calls back into the routines (which
 * reside in this driver) given at initialization to peform the actual I/O.
 *
 * Interrupts
 * Interrupts are handled in the OS/Application layer above this driver.
 */

xdbg_stmnt(u32 _xllfifo_rr_value;)
xdbg_stmnt(u32 _xllfifo_ipie_value;)
xdbg_stmnt(u32 _xllfifo_ipis_value;)

/****************************************************************************/
/*
*
* XLlFifo_RxGetWord reads one 32 bit word from the FIFO specified by
* <i>InstancePtr</i>.
*
* XLlFifo_RxGetLen or XLlFifo_iRxGetLen must be called before calling
* XLlFifo_RxGetWord. Otherwise, the hardware will raise an <i>Over Read
* Exception</i>.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   XLlFifo_RxGetWord returns the 32 bit word read from the FIFO.
*
* @note
* C-style signature:
*    u32 XLlFifo_RxGetWord(XLlFifo *InstancePtr)
*
*****************************************************************************/
#define XLlFifo_RxGetWord(InstancePtr) \
	XLlFifo_ReadReg((InstancePtr)->BaseAddress, XLLF_RDFD_OFFSET)

/****************************************************************************/
/*
*
* XLlFifo_TxPutWord writes the 32 bit word, <i>Word</i> to the FIFO specified by
* <i>InstancePtr</i>.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   N/A
*
* @note
* C-style signature:
*    void XLlFifo_TxPutWord(XLlFifo *InstancePtr, u32 Word)
*
*****************************************************************************/
#define XLlFifo_TxPutWord(InstancePtr, Word) \
	XLlFifo_WriteReg((InstancePtr)->BaseAddress, XLLF_TDFD_OFFSET, \
			(Word))

/*****************************************************************************/
/*
*
* XLlFifo_iRxOccupancy returns the number of 32-bit words available (occupancy)
* to be read from the receive channel of the FIFO, specified by
* <i>InstancePtr</i>.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   XLlFifo_iRxOccupancy returns the occupancy count in 32-bit words for
*           the specified FIFO.
*
******************************************************************************/
static u32 XLlFifo_iRxOccupancy(XLlFifo *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr);

	return XLlFifo_ReadReg(InstancePtr->BaseAddress,
			XLLF_RDFO_OFFSET);
}

/*****************************************************************************/
/*
*
* XLlFifo_iRxGetLen notifies the hardware that the program is ready to receive the
* next frame from the receive channel of the FIFO specified by <i>InstancePtr</i>.
*
* Note that the program must first call XLlFifo_iRxGetLen before pulling data
* out of the receive channel of the FIFO with XLlFifo_Read.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   XLlFifo_iRxGetLen returns the number of bytes available in the next
*           frame.
*
******************************************************************************/
static u32 XLlFifo_iRxGetLen(XLlFifo *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr);

	return XLlFifo_ReadReg(InstancePtr->BaseAddress,
		XLLF_RLF_OFFSET);
}

/*****************************************************************************/
/*
*
* XLlFifo_iRead_Aligned reads, <i>WordCount</i>, words from the FIFO referenced by
* <i>InstancePtr</i> to the block of memory, referenced by <i>BufPtr</i>.
*
* XLlFifo_iRead_Aligned assumes that <i>BufPtr</i> is already aligned according
* to the following hardware limitations:
*    ppc        - aligned on 32 bit boundaries to avoid performance penalties
*                 from unaligned exception handling.
*    microblaze - aligned on 32 bit boundaries as microblaze does not handle
*                 unaligned transfers.
*
* Care must be taken to ensure that the number of words read with one or more
* calls to XLlFifo_Read() does not exceed the number of bytes (rounded up to
* the nearest whole 32 bit word) available given from the last call to
* XLlFifo_RxGetLen().
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @param    BufPtr specifies the memory address to place the data read.
*
* @param    WordCount specifies the number of 32 bit words to read.
*
* @return   XLlFifo_iRead_Aligned always returns XST_SUCCESS. Error handling is
*           otherwise handled through hardware exceptions and interrupts.
*
* @note
*
* C Signature: int XLlFifo_iRead_Aligned(XLlFifo *InstancePtr,
*                      void *BufPtr, unsigned WordCount);
*
******************************************************************************/
/* static */ int XLlFifo_iRead_Aligned(XLlFifo *InstancePtr, void *BufPtr,
			     unsigned WordCount)
{
	unsigned WordsRemaining = WordCount;
	u32 *BufPtrIdx = BufPtr;

	xdbg_printf(XDBG_DEBUG_FIFO_RX, "XLlFifo_iRead_Aligned: start\n");
	XASSERT_NONVOID(InstancePtr);
	XASSERT_NONVOID(BufPtr);
	/* assert bufer is 32 bit aligned */
	XASSERT_NONVOID(((unsigned)BufPtr & 0x3) == 0x0);
	xdbg_printf(XDBG_DEBUG_FIFO_RX, "XLlFifo_iRead_Aligned: after asserts\n");

	while (WordsRemaining) {
/*		xdbg_printf(XDBG_DEBUG_FIFO_RX,
			    "XLlFifo_iRead_Aligned: WordsRemaining: %d\n",
			    WordsRemaining);
*/
		*BufPtrIdx = XLlFifo_RxGetWord(InstancePtr);
		BufPtrIdx++;
		WordsRemaining--;
	}
	xdbg_printf(XDBG_DEBUG_FIFO_RX,
		    "XLlFifo_iRead_Aligned: returning SUCCESS\n");
	return XST_SUCCESS;
}

/****************************************************************************/
/*
*
* XLlFifo_iTxVacancy returns the number of unused 32 bit words available
* (vacancy) in the send channel of the FIFO, specified by <i>InstancePtr</i>.
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @return   XLlFifo_iTxVacancy returns the vacancy count in 32-bit words for
*           the specified FIFO.
*
*****************************************************************************/
static u32 XLlFifo_iTxVacancy(XLlFifo *InstancePtr)
{
	XASSERT_NONVOID(InstancePtr);

	return XLlFifo_ReadReg(InstancePtr->BaseAddress,
			XLLF_TDFV_OFFSET);
}

/*****************************************************************************/
/*
*
* XLlFifo_iTxSetLen begins a hardware transfer of data out of the transmit
* channel of the FIFO, specified by <i>InstancePtr</i>. <i>Bytes</i> specifies the number
* of bytes in the frame to transmit.
*
* Note that <i>Bytes</i> (rounded up to the nearest whole 32 bit word) must be same
* number of words just written using one or more calls to
* XLlFifo_iWrite_Aligned()
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @param    Bytes specifies the number of bytes to transmit.
*
* @return   N/A
*
******************************************************************************/
static void XLlFifo_iTxSetLen(XLlFifo *InstancePtr, u32 Bytes)
{
	XASSERT_VOID(InstancePtr);

	XLlFifo_WriteReg(InstancePtr->BaseAddress, XLLF_TLF_OFFSET,
			Bytes);
}

/*****************************************************************************/
/*
*
* XLlFifo_iWrite_Aligned writes, <i>WordCount</i>, words to the FIFO referenced by
* <i>InstancePtr</i> from the block of memory, referenced by <i>BufPtr</i>.
*
* XLlFifo_iWrite_Aligned assumes that <i>BufPtr</i> is already aligned according
* to the following hardware limitations:
*    ppc        - aligned on 32 bit boundaries to avoid performance penalties
*                 from unaligned exception handling.
*    microblaze - aligned on 32 bit boundaries as microblaze does not handle
*                 unaligned transfers.
*
* Care must be taken to ensure that the number of words written with one or
* more calls to XLlFifo_iWrite_Aligned() matches the number of bytes (rounded up
* to the nearest whole 32 bit word) given in the next call to
* XLlFifo_iTxSetLen().
*
* @param    InstancePtr references the FIFO on which to operate.
*
* @param    BufPtr specifies the memory address to place the data read.
*
* @param    WordCount specifies the number of 32 bit words to read.
*
* @return   XLlFifo_iWrite_Aligned always returns XST_SUCCESS. Error handling is
*           otherwise handled through hardware exceptions and interrupts.
*
* @note
*
* C Signature: int XLlFifo_iWrite_Aligned(XLlFifo *InstancePtr,
*                      void *BufPtr, unsigned WordCount);
*
******************************************************************************/
/* static */ int XLlFifo_iWrite_Aligned(XLlFifo *InstancePtr, void *BufPtr,
			      unsigned WordCount)
{
	unsigned WordsRemaining = WordCount;
	u32 *BufPtrIdx = BufPtr;

	xdbg_printf(XDBG_DEBUG_FIFO_TX,
		    "XLlFifo_iWrite_Aligned: Inst: %p; Buff: %p; Count: %d\n",
		    InstancePtr, BufPtr, WordCount);
	XASSERT_NONVOID(InstancePtr);
	XASSERT_NONVOID(BufPtr);
	/* assert bufer is 32 bit aligned */
	XASSERT_NONVOID(((unsigned)BufPtr & 0x3) == 0x0);

	xdbg_printf(XDBG_DEBUG_FIFO_TX,
		    "XLlFifo_iWrite_Aligned: WordsRemaining: %d\n",
		    WordsRemaining);
	while (WordsRemaining) {
		XLlFifo_TxPutWord(InstancePtr, *BufPtrIdx);
		BufPtrIdx++;
		WordsRemaining--;
	}
	
	xdbg_printf(XDBG_DEBUG_FIFO_TX,
		    "XLlFifo_iWrite_Aligned: returning SUCCESS\n");
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* XLlFifo_Initialize initializes an XPS_ll_Fifo device along with the
* <i>InstancePtr</i> that references it.
*
* @param    InstancePtr references the memory instance to be associated with
*           the FIFO device upon initialization.
*
* @param    BaseAddress is the processor address used to access the
*           base address of the Fifo device.
*
* @return   N/A
*
******************************************************************************/
void XLlFifo_Initialize(XLlFifo *InstancePtr, u32 BaseAddress)
{
	XASSERT_VOID(InstancePtr);
	XASSERT_VOID(BaseAddress);

	/* Clear instance memory */
	memset(InstancePtr, 0, sizeof(XLlFifo));

	/*
	 * We don't care about the physical base address, just copy the
	 * processor address over it.
	 */
	InstancePtr->BaseAddress = BaseAddress;

	InstancePtr->IsReady = XCOMPONENT_IS_READY;

	XLlFifo_TxReset(InstancePtr);
	XLlFifo_RxReset(InstancePtr);

	XStrm_RxInitialize(&(InstancePtr->RxStreamer), FIFO_WIDTH_BYTES,
			(void *)InstancePtr,
                        (XStrm_XferFnType)XLlFifo_iRead_Aligned,
                        (XStrm_GetLenFnType)XLlFifo_iRxGetLen,
                        (XStrm_GetOccupancyFnType)XLlFifo_iRxOccupancy);

	XStrm_TxInitialize(&(InstancePtr->TxStreamer), FIFO_WIDTH_BYTES,
			(void *)InstancePtr,
                        (XStrm_XferFnType)XLlFifo_iWrite_Aligned,
                        (XStrm_SetLenFnType)XLlFifo_iTxSetLen,
                        (XStrm_GetVacancyFnType)XLlFifo_iTxVacancy);
}

