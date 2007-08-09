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
 *
 * @file xstreamer.h
 *
 * The Xilinx byte streamer for packet FIFOs.
 *
 * <h2>Driver Description</h2>
 *
 * This driver enables higher layer software to access a hardware FIFO using
 * any alignment in the data buffers while preserving alignment for the hardware
 * FIFO access.
 * 
 * This driver treats send and receive channels separately, using different
 * types of instance objects for each.
 * 
 * This driver makes use of another FIFO driver to access the specific FIFO
 * hardware through use of the routines passed into the Tx/RxInitialize
 * routines.
 * 
 * <h2>Initialization</h2>
 *
 * Send and receive channels are intialized separately. The receive channel is
 * initiailzed using XStrm_RxInitialize(). The send channel is initialized
 * using XStrm_TxInitialize().
 *
 * 
 * <h2>Usage</h2>
 * It is fairly simple to use the API provided by this byte streamer
 * driver. The only somewhat tricky part is that the calling code must
 * correctly call a couple routines in the right sequence for receive and
 * transmit.
 *
 * 	This sequence is described here. Check the routine functional 
 * descriptions for information on how to use a specific API routine.
 *
 * <h3>Receive</h3>
 * A frame is received by using the following sequence:<br>
 * 1) call XStrm_RxGetLen() to get the length of the next incoming frame.<br>
 * 2) call XStrm_Read() one or more times to read the number of bytes
 * 	   reported by XStrm_RxGetLen().<br>
 *
 * For example:
 * <pre>
 * 	frame_len = XStrm_RxGetLen(&RxInstance);
 * 	while (frame_len) {
 * 		unsigned bytes = min(sizeof(buffer), frame_len);
 * 		XStrm_Read(&RxInstance, buffer, bytes);
 * 		// do something with buffer here
 * 		frame_len -= bytes;
 * 	}
 * </pre>
 *
 * Other restrictions on the sequence of API calls may apply depending on
 * the specific FIFO driver used by this byte streamer driver.
 *
 * <h3>Transmit</h3>
 * A frame is transmittted by using the following sequence:<br>
 * 1) call XStrm_Write() one or more times to write all the of bytes in
 *    the next frame.<br>
 * 2) call XStrm_TxSetLen() to begin the transmission of frame just
 *    written.<br>
 *
 * For example:
 * <pre>
 * 	frame_left = frame_len;
 * 	while (frame_left) {
 * 		unsigned bytes = min(sizeof(buffer), frame_left);
 * 		XStrm_Write(&TxInstance, buffer, bytes);
 * 		// do something here to refill buffer
 * 	}
 * 	XStrm_TxSetLen(&RxInstance, frame_len);
 * </pre>
 *
 * Other restrictions on the sequence of API calls may apply depending on
 * the specific FIFO driver used by this byte streamer driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jvb  10/12/06 First release
 * </pre>
 *
 *****************************************************************************/
#ifndef XSTREAMER_H		/* prevent circular inclusions */
#define XSTREAMER_H		/* by using preprocessor symbols */

/* force C linkage */
#ifdef __cplusplus
extern "C" {
#endif

#include "xenv.h"
#include "xbasic_types.h"
#include "xstatus.h"
#include "xdebug.h"

/*
 * key hole size in 32 bit words
 */
#define LARGEST_FIFO_KEYHOLE_SIZE_WORDS 4

/*
 * This union is used simply to force a 32bit alignment on the
 * buffer. Only the 'bytes' member is really used.
 */
union XStrm_AlignedBufferType {
	u32 _words[LARGEST_FIFO_KEYHOLE_SIZE_WORDS];
	char bytes[LARGEST_FIFO_KEYHOLE_SIZE_WORDS * 4];
};

typedef int(*XStrm_XferFnType) (void *FifoInstance, void *BufPtr,
                                    unsigned WordCount);
typedef u32 (*XStrm_GetLenFnType) (void *FifoInstance);
typedef void (*XStrm_SetLenFnType) (void *FifoInstance,
                                    u32 ByteCount);
typedef u32 (*XStrm_GetOccupancyFnType) (void *FifoInstance);
typedef u32 (*XStrm_GetVacancyFnType) (void *FifoInstance);

/**
 * This typedef defines a run-time instance of a receive byte-streamer.
 */
typedef struct XStrm_RxFifoStreamer {
	union XStrm_AlignedBufferType AlignedBuffer;
	unsigned HeadIndex;  /**< HeadIndex is the index to the AlignedBuffer
	                      *   as bytes.
                              */
	unsigned FifoWidth;  /**< FifoWidth is the FIFO key hole width in bytes.
	                      */
	unsigned FrmByteCnt; /**< FrmByteCnt is the number of bytes in the next
			      *   Frame.
			      */
	void *FifoInstance;  /**< FifoInstance is the FIFO driver instance to
	                      *   pass to ReadFn, GetLenFn, and GetOccupancyFn
	                      *   routines.
	                      */
	XStrm_XferFnType ReadFn;     /**< ReadFn is the routine the streamer
	                              *   uses to receive bytes from the Fifo.
	                              */
	XStrm_GetLenFnType GetLenFn; /**< GetLenFn is the routine the streamer
	                              *   uses to initiate receive operations
	                              *   on the FIFO.
                                      */
	XStrm_GetOccupancyFnType GetOccupancyFn; /**< GetOccupancyFn is the
	                                          *   routine the streamer uses
	                                          *   to get the occupancy from
	                                          *   the FIFO.
	                                          */
} XStrm_RxFifoStreamer;

/**
 * This typedef defines a run-time instance of a transmit byte-streamer.
 */
typedef struct XStrm_TxFifoStreamer {
	union XStrm_AlignedBufferType AlignedBuffer;
	unsigned TailIndex; /**< TailIndex is the index to the AlignedBuffer
	                     *   as bytes
                             */
	unsigned FifoWidth; /**< FifoWidth is the FIFO key hole width in bytes.
	                     */

	void *FifoInstance; /**< FifoInstance is the FIFO driver instance to
	                     *   pass to WriteFn, SetLenFn, and GetVacancyFn
	                     *   routines.
	                     */
	XStrm_XferFnType WriteFn; /**< WriteFn is the routine the streamer
	                           *   uses to transmit bytes to the Fifo.
	                           */
	XStrm_SetLenFnType SetLenFn; /**< SetLenFn is the routine the streamer
	                              *   uses to initiate transmit operations
	                              *   on the FIFO.
                                      */
	XStrm_GetVacancyFnType GetVacancyFn; /**< GetVaccancyFn is the routine
	                                      *   the streamer uses to get the
	                                      *   vacancy from the FIFO.
	                                      */
} XStrm_TxFifoStreamer;

/*****************************************************************************/
/*
*
* XStrm_TxVacancy returns the number of unused 32-bit words available (vacancy)
* between the streamer, specified by <i>InstancePtr</i>, and the FIFO this
* streamer is using.
*
* @param    InstancePtr references the streamer on which to operate.
*
* @return   XStrm_TxVacancy returns the vacancy count in number of 32 bit words.
*
* @note
*
* C Signature: u32 XStrm_TxVacancy(XStrm_TxFifoStreamer *InstancePtr)
*
* The amount of bytes in the holding buffer (rounded up to whole 32-bit words)
* is subtracted from the vacancy value of FIFO this streamer is using. This is
* to ensure the caller can write the number words given in the return value and
* not overflow the FIFO.
*
******************************************************************************/
#define XStrm_TxVacancy(InstancePtr) \
	(((*(InstancePtr)->GetVacancyFn)((InstancePtr)->FifoInstance)) - \
			(((InstancePtr)->TailIndex + 3) / 4))

/*****************************************************************************/
/*
*
* XStrm_RxOccupancy returns the number of 32-bit words available (occupancy) to
* be read from the streamer, specified by <i>InstancePtr</i>, and FIFO this
* steamer is using.
*
* @param    InstancePtr references the streamer on which to operate.
*
* @return   XStrm_RxOccupancy returns the occupancy count in number of 32 bit
*           words.
*
* @note
*
* C Signature: u32 XStrm_RxOccupancy(XStrm_RxFifoStreamer *InstancePtr)
*
* The amount of bytes in the holding buffer (rounded up to whole 32-bit words)
* is added to the occupancy value of FIFO this streamer is using. This is to
* ensure the caller will get a little more accurate occupancy value.
*
******************************************************************************/
#ifdef DEBUG
extern u32 _xstrm_ro_value;
extern u32 _xstrm_buffered;
#define XStrm_RxOccupancy(InstancePtr) \
        (_xstrm_ro_value = ((*(InstancePtr)->GetOccupancyFn)((InstancePtr)->FifoInstance)), \
	xdbg_printf(XDBG_DEBUG_FIFO_RX, "reg: %d; frmbytecnt: %d\n", \
		_xstrm_ro_value, (InstancePtr)->FrmByteCnt), \
	(((InstancePtr)->FrmByteCnt) ? \
		_xstrm_buffered = ((InstancePtr)->FifoWidth - (InstancePtr)->HeadIndex) : \
		0), \
	xdbg_printf(XDBG_DEBUG_FIFO_RX, "buffered_bytes: %d\n", _xstrm_buffered), \
	xdbg_printf(XDBG_DEBUG_FIFO_RX, "buffered (rounded): %d\n", _xstrm_buffered), \
	(_xstrm_ro_value + _xstrm_buffered))
#else
#define XStrm_RxOccupancy(InstancePtr) \
	( \
	  ((*(InstancePtr)->GetOccupancyFn)((InstancePtr)->FifoInstance)) + \
	  ( \
	    ((InstancePtr)->FrmByteCnt) ? \
	      ((InstancePtr)->FifoWidth - (InstancePtr)->HeadIndex) : \
	      0 \
	  ) \
	)
#endif

/****************************************************************************/
/*
*
* XStrm_IsRxInternalEmpty returns true if the streamer, specified by
* <i>InstancePtr</i>, is not holding any bytes in it's  internal buffers. Note
* that this routine does not reflect information about the state of the
* FIFO used by this streamer.
*
* @param    InstancePtr references the streamer on which to operate.
*
* @return   XStrm_IsRxInternalEmpty returns TRUE when the streamer is not
*           holding any bytes in it's internal buffers. Otherwise,
*           XStrm_IsRxInternalEmpty returns FALSE.
*
* @note
* C-style signature:
*    int XStrm_IsRxInternalEmpty(XStrm_RxFifoStreamer *InstancePtr)
*
*****************************************************************************/
#define XStrm_IsRxInternalEmpty(InstancePtr) \
	(((InstancePtr)->HeadIndex == (InstancePtr)->FifoWidth) ? TRUE : FALSE)

void XStrm_RxInitialize(XStrm_RxFifoStreamer *InstancePtr,
                        unsigned FifoWidth, void *FifoInstance,
                        XStrm_XferFnType ReadFn,
                        XStrm_GetLenFnType GetLenFn,
                        XStrm_GetOccupancyFnType GetOccupancyFn);

void XStrm_TxInitialize(XStrm_TxFifoStreamer *InstancePtr,
                        unsigned FifoWidth, void *FifoInstance,
                        XStrm_XferFnType WriteFn,
                        XStrm_SetLenFnType SetLenFn,
                        XStrm_GetVacancyFnType GetVacancyFn);

void XStrm_TxSetLen(XStrm_TxFifoStreamer *InstancePtr, u32 Bytes);
void XStrm_Write(XStrm_TxFifoStreamer *InstancePtr, void *BufPtr,
                    unsigned bytes);

u32 XStrm_RxGetLen(XStrm_RxFifoStreamer *InstancePtr);
void XStrm_Read(XStrm_RxFifoStreamer *InstancePtr, void *BufPtr,
                   unsigned bytes);

#ifdef __cplusplus
}
#endif
#endif				/* XSTREAMER_H  end of preprocessor protection symbols */
