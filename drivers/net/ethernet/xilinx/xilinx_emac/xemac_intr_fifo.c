/* $Id: xemac_intr_fifo.c,v 1.2 2007/05/15 00:52:28 wre Exp $ */
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
*       (c) Copyright 2003 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemac_intr_fifo.c
*
* Contains functions related to interrupt mode using direct FIFO I/O or simple
* DMA. The driver uses simple DMA if the device is configured with DMA,
* otherwise it uses direct FIFO access.
*
* The interrupt handler, XEmac_IntrHandlerFifo(), must be connected by the user
* to the interrupt controller.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  07/31/01 First release
* 1.00b rpm  02/20/02 Repartitioned files and functions
* 1.00c rpm  12/05/02 New version includes support for simple DMA
* 1.00c rpm  04/01/03 Added check in FifoSend for room in the data FIFO
*                     before starting a simple DMA transfer.
* 1.00d rpm  09/26/03 New version includes support PLB Ethernet and v2.00a of
*                     the packet fifo driver.
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xemac_i.h"
#include "xio.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Variable Definitions *****************************/


/************************** Function Prototypes ******************************/

static void HandleEmacFifoIntr(XEmac * InstancePtr);

/*****************************************************************************/
/**
*
* Send an Ethernet frame using direct FIFO I/O or simple DMA with interrupts.
* The caller provides a contiguous-memory buffer and its length. The buffer
* must be 32-bit aligned. If using simple DMA and the PLB 10/100 Ethernet core,
* the buffer must be 64-bit aligned. The callback function set by using
* SetFifoSendHandler is invoked when the transmission is complete.
*
* It is assumed that the upper layer software supplies a correctly formatted
* Ethernet frame, including the destination and source addresses, the
* type/length field, and the data field.
*
* If the device is configured with DMA, simple DMA will be used to transfer
* the buffer from memory to the Emac. This means that this buffer should not
* be cached.  See the comment section "Simple DMA" in xemac.h for more
* information.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param BufPtr is a pointer to a aligned buffer containing the Ethernet
*        frame to be sent.
* @param ByteCount is the size of the Ethernet frame.
*
* @return
*
* - XST_SUCCESS if the frame was successfully sent. An interrupt is generated
*   when the EMAC transmits the frame and the driver calls the callback set
*   with XEmac_SetFifoSendHandler()
* - XST_DEVICE_IS_STOPPED  if the device has not yet been started
* - XST_NOT_INTERRUPT if the device is not in interrupt mode
* - XST_FIFO_NO_ROOM if there is no room in the FIFO for this frame
* - XST_DEVICE_BUSY if configured for simple DMA and the DMA engine is busy
* - XST_DMA_ERROR if an error occurred during the DMA transfer (simple DMA).
*   The user should treat this as a fatal error that requires a reset of the
*   EMAC device.
*
* @note
*
* This function is not thread-safe. The user must provide mutually exclusive
* access to this function if there are to be multiple threads that can call it.
*
* @internal
*
* The Ethernet MAC uses FIFOs behind its length and status registers. For this
* reason, it is important to keep the length, status, and data FIFOs in sync
* when reading or writing to them.
*
******************************************************************************/
int XEmac_FifoSend(XEmac * InstancePtr, u8 *BufPtr, u32 ByteCount)
{
	int Result;
	volatile u32 StatusReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufPtr != NULL);
	XASSERT_NONVOID(ByteCount > XEM_HDR_SIZE);	/* send at least 1 byte */
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure the device is configured for interrupt mode and it is started
	 */
	if (InstancePtr->IsPolled) {
		return XST_NOT_INTERRUPT;
	}

	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return XST_DEVICE_IS_STOPPED;
	}

	/*
	 * Before writing to the data FIFO, make sure the length FIFO is not
	 * full.  The data FIFO might not be full yet even though the length FIFO
	 * is. This avoids an overrun condition on the length FIFO and keeps the
	 * FIFOs in sync.
	 */
	StatusReg = XEMAC_READ_IISR(InstancePtr->BaseAddress);
	if (StatusReg & XEM_EIR_XMIT_LFIFO_FULL_MASK) {
		return XST_FIFO_NO_ROOM;
	}

	/*
	 * Send either by directly writing to the FIFOs or using the DMA engine
	 */
	if (!XEmac_mIsDma(InstancePtr)) {
		/*
		 * This is a non-blocking write. The packet FIFO returns an error if there
		 * is not enough room in the FIFO for this frame.
		 */
		Result = XPacketFifoV200a_Write(&InstancePtr->SendFifo, BufPtr,
						ByteCount);
		if (Result != XST_SUCCESS) {
			return Result;
		}
	}
	else {
		u32 Vacancy;

		/*
		 * Need to make sure there is room in the data FIFO for the packet
		 * before trying to DMA into it. Get the vacancy count (in words)
		 * and make sure the packet will fit.
		 */
		Vacancy = XPF_V200A_GET_COUNT(&InstancePtr->SendFifo);
		if ((Vacancy * sizeof(u32)) < ByteCount) {
			return XST_FIFO_NO_ROOM;
		}

		/*
		 * Check the DMA engine to make sure it is not already busy
		 */
		if (XDmaChannel_GetStatus(&InstancePtr->SendChannel) &
		    XDC_DMASR_BUSY_MASK) {
			return XST_DEVICE_BUSY;
		}

		/*
		 * Set the DMA control register up properly
		 */
		XDmaChannel_SetControl(&InstancePtr->SendChannel,
				       XDC_DMACR_SOURCE_INCR_MASK |
				       XDC_DMACR_DEST_LOCAL_MASK |
				       XDC_DMACR_SG_DISABLE_MASK);

		/*
		 * Now transfer the data from the buffer to the FIFO
		 */
		XDmaChannel_Transfer(&InstancePtr->SendChannel, (u32 *) BufPtr,
				     (u32 *) (InstancePtr->BaseAddress +
					      XEM_PFIFO_TXDATA_OFFSET),
				     ByteCount);

		/*
		 * Poll here waiting for DMA to be not busy. We think this will
		 * typically be a single read since DMA should be ahead of the SW.
		 */
		do {
			StatusReg =
				XDmaChannel_GetStatus(&InstancePtr->
						      SendChannel);
		}
		while (StatusReg & XDC_DMASR_BUSY_MASK);

		/* Return an error if there was a problem with DMA */
		if ((StatusReg & XDC_DMASR_BUS_ERROR_MASK) ||
		    (StatusReg & XDC_DMASR_BUS_TIMEOUT_MASK)) {
			InstancePtr->Stats.DmaErrors++;
			return XST_DMA_ERROR;
		}
	}

	/*
	 * Set the MAC's transmit packet length register to tell it to transmit
	 */
	XIo_Out32(InstancePtr->BaseAddress + XEM_TPLR_OFFSET, ByteCount);

	/*
	 * Bump stats here instead of the Isr since we know the byte count
	 * here but would have to save it in the instance in order to know the
	 * byte count at interrupt time.
	 */
	InstancePtr->Stats.XmitFrames++;
	InstancePtr->Stats.XmitBytes += ByteCount;

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Receive an Ethernet frame into the buffer passed as an argument. This
* function is called in response to the callback function for received frames
* being called by the driver. The callback function is set up using
* SetFifoRecvHandler, and is invoked when the driver receives an interrupt
* indicating a received frame. The driver expects the upper layer software to
* call this function, FifoRecv, to receive the frame. The buffer supplied
* should be large enough to hold a maximum-size Ethernet frame.
*
* The buffer into which the frame will be received must be 32-bit aligned. If
* using simple DMA and the PLB 10/100 Ethernet core, the buffer must be 64-bit
* aligned.
*
* If the device is configured with DMA, simple DMA will be used to transfer
* the buffer from the Emac to memory. This means that this buffer should not
* be cached. See the comment section "Simple DMA" in xemac.h for more
* information.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param BufPtr is a pointer to a aligned buffer into which the received
*        Ethernet frame will be copied.
* @param ByteCountPtr is both an input and an output parameter. It is a pointer
*        to a 32-bit word that contains the size of the buffer on entry into
*        the function and the size the received frame on return from the
*        function.
*
* @return
*
* - XST_SUCCESS if the frame was sent successfully
* - XST_DEVICE_IS_STOPPED if the device has not yet been started
* - XST_NOT_INTERRUPT if the device is not in interrupt mode
* - XST_NO_DATA if there is no frame to be received from the FIFO
* - XST_BUFFER_TOO_SMALL if the buffer to receive the frame is too small for
*   the frame waiting in the FIFO.
* - XST_DEVICE_BUSY if configured for simple DMA and the DMA engine is busy
* - XST_DMA_ERROR if an error occurred during the DMA transfer (simple DMA).
*   The user should treat this as a fatal error that requires a reset of the
*   EMAC device.
*
* @note
*
* The input buffer must be big enough to hold the largest Ethernet frame.
*
* @internal
*
* The Ethernet MAC uses FIFOs behind its length and status registers. For this
* reason, it is important to keep the length, status, and data FIFOs in sync
* when reading or writing to them.
*
******************************************************************************/
int XEmac_FifoRecv(XEmac * InstancePtr, u8 *BufPtr, u32 *ByteCountPtr)
{
	int Result;
	u32 PktLength;
	u32 StatusReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufPtr != NULL);
	XASSERT_NONVOID(ByteCountPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure the device is not configured for polled mode and it is started
	 */
	if (InstancePtr->IsPolled) {
		return XST_NOT_INTERRUPT;
	}

	if (InstancePtr->IsStarted != XCOMPONENT_IS_STARTED) {
		return XST_DEVICE_IS_STOPPED;
	}

	/*
	 * Make sure the buffer is big enough to hold the maximum frame size.
	 * We need to do this because as soon as we read the MAC's packet length
	 * register, which is actually a FIFO, we remove that length from the
	 * FIFO.  We do not want to read the length FIFO without also reading the
	 * data FIFO since this would get the FIFOs out of sync.  So we have to
	 * make this restriction.
	 */
	if (*ByteCountPtr < XEM_MAX_FRAME_SIZE) {
		return XST_BUFFER_TOO_SMALL;
	}

	/*
	 * Before reading from the length FIFO, make sure the length FIFO is not
	 * empty. We could cause an underrun error if we try to read from an
	 * empty FIFO.
	 */
	StatusReg = XEMAC_READ_IISR(InstancePtr->BaseAddress);
	if (StatusReg & XEM_EIR_RECV_LFIFO_EMPTY_MASK) {
		/*
		 * Clear the empty status so the next time through the current status
		 * of the hardware is reflected (we have to do this because the status
		 * is level in the device but latched in the interrupt status register).
		 */
		XEMAC_WRITE_IISR(InstancePtr->BaseAddress,
				      XEM_EIR_RECV_LFIFO_EMPTY_MASK);
		return XST_NO_DATA;
	}

	/*
	 * If configured with DMA, make sure the DMA engine is not busy
	 */
	if (XEmac_mIsDma(InstancePtr)) {
		if (XDmaChannel_GetStatus(&InstancePtr->RecvChannel) &
		    XDC_DMASR_BUSY_MASK) {
			return XST_DEVICE_BUSY;
		}
	}

	/*
	 * Determine, from the MAC, the length of the next packet available
	 * in the data FIFO (there should be a non-zero length here)
	 */
	PktLength = XIo_In32(InstancePtr->BaseAddress + XEM_RPLR_OFFSET);
	if (PktLength == 0) {
		return XST_NO_DATA;
	}

	/*
	 * We assume that the MAC never has a length bigger than the largest
	 * Ethernet frame, so no need to make another check here.
	 *
	 * Receive either by directly reading the FIFO or using the DMA engine
	 */
	if (!XEmac_mIsDma(InstancePtr)) {
		/*
		 * This is a non-blocking read. The FIFO returns an error if there is
		 * not at least the requested amount of data in the FIFO.
		 */
		Result = XPacketFifoV200a_Read(&InstancePtr->RecvFifo, BufPtr,
					       PktLength);
		if (Result != XST_SUCCESS) {
			return Result;
		}
	}
	else {
		/*
		 * Call on DMA to transfer from the FIFO to the buffer. First set up
		 * the DMA control register.
		 */
		XDmaChannel_SetControl(&InstancePtr->RecvChannel,
				       XDC_DMACR_DEST_INCR_MASK |
				       XDC_DMACR_SOURCE_LOCAL_MASK |
				       XDC_DMACR_SG_DISABLE_MASK);

		/*
		 * Now transfer the data
		 */
		XDmaChannel_Transfer(&InstancePtr->RecvChannel,
				     (u32 *) (InstancePtr->BaseAddress +
					      XEM_PFIFO_RXDATA_OFFSET),
				     (u32 *) BufPtr, PktLength);

		/*
		 * Poll here waiting for DMA to be not busy. We think this will
		 * typically be a single read since DMA should be ahead of the SW.
		 */
		do {
			StatusReg =
				XDmaChannel_GetStatus(&InstancePtr->
						      RecvChannel);
		}
		while (StatusReg & XDC_DMASR_BUSY_MASK);

		/* Return an error if there was a problem with DMA */
		if ((StatusReg & XDC_DMASR_BUS_ERROR_MASK) ||
		    (StatusReg & XDC_DMASR_BUS_TIMEOUT_MASK)) {
			InstancePtr->Stats.DmaErrors++;
			return XST_DMA_ERROR;
		}
	}

	*ByteCountPtr = PktLength;

	InstancePtr->Stats.RecvFrames++;
	InstancePtr->Stats.RecvBytes += PktLength;

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* The interrupt handler for the Ethernet driver when configured for direct FIFO
* communication or simple DMA.
*
* Get the interrupt status from the IpIf to determine the source of the
* interrupt.  The source can be: MAC, Recv Packet FIFO, or Send Packet FIFO.
* The packet FIFOs only interrupt during "deadlock" conditions.  All other
* FIFO-related interrupts are generated by the MAC.
*
* @param InstancePtr is a pointer to the XEmac instance that just interrupted.
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
void XEmac_IntrHandlerFifo(void *InstancePtr)
{
	u32 IntrStatus;
	XEmac *EmacPtr = (XEmac *) InstancePtr;

	EmacPtr->Stats.TotalIntrs++;

	/*
	 * Get the interrupt status from the IPIF. There is no clearing of
	 * interrupts in the IPIF. Interrupts must be cleared at the source.
	 */
	IntrStatus = XEMAC_READ_DIPR(EmacPtr->BaseAddress);

	if (IntrStatus & XEM_IPIF_EMAC_MASK) {	/* MAC interrupt */
		EmacPtr->Stats.EmacInterrupts++;
		HandleEmacFifoIntr(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_RECV_FIFO_MASK) {	/* Receive FIFO interrupt */
		EmacPtr->Stats.RecvInterrupts++;
		XEmac_CheckFifoRecvError(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_SEND_FIFO_MASK) {	/* Send FIFO interrupt */
		EmacPtr->Stats.XmitInterrupts++;
		XEmac_CheckFifoSendError(EmacPtr);
	}

	if (IntrStatus & XEMAC_ERROR_MASK) {
		/*
		 * An error occurred internal to the IPIF. This is more of a debug and
		 * integration issue rather than a production error. Don't do anything
		 * other than clear it, which provides a spot for software to trap
		 * on the interrupt and begin debugging.
		 */
		XEMAC_WRITE_DISR(EmacPtr->BaseAddress,
				      XEMAC_ERROR_MASK);
	}
}

/*****************************************************************************/
/**
*
* Set the callback function for handling confirmation of transmitted frames when
* configured for direct memory-mapped I/O using FIFOs. The upper layer software
* should call this function during initialization. The callback is called by the
* driver once per frame sent. The callback is responsible for freeing the
* transmitted buffer if necessary.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. If there are potentially slow operations within the
* callback, these should be done at task-level.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param CallBackRef is a reference pointer to be passed back to the driver in
*        the callback. This helps the driver correlate the callback to a
*        particular driver.
* @param FuncPtr is the pointer to the callback function.
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
void XEmac_SetFifoRecvHandler(XEmac * InstancePtr, void *CallBackRef,
			      XEmac_FifoHandler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->FifoRecvHandler = FuncPtr;
	InstancePtr->FifoRecvRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Set the callback function for handling received frames when configured for
* direct memory-mapped I/O using FIFOs. The upper layer software should call
* this function during initialization. The callback is called once per frame
* received. During the callback, the upper layer software should call FifoRecv
* to retrieve the received frame.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. Sending the received frame up the protocol stack
* should be done at task-level. If there are other potentially slow operations
* within the callback, these too should be done at task-level.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param CallBackRef is a reference pointer to be passed back to the driver in
*        the callback. This helps the driver correlate the callback to a
*        particular driver.
* @param FuncPtr is the pointer to the callback function.
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
void XEmac_SetFifoSendHandler(XEmac * InstancePtr, void *CallBackRef,
			      XEmac_FifoHandler FuncPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->FifoSendHandler = FuncPtr;
	InstancePtr->FifoSendRef = CallBackRef;
}

/******************************************************************************
*
* Handle an interrupt from the Ethernet MAC when configured for direct FIFO
* communication.  The interrupts handled are:
* - Transmit done (transmit status FIFO is non-empty). Used to determine when
*   a transmission has been completed.
* - Receive done (receive length FIFO is non-empty). Used to determine when a
*   valid frame has been received.
*
* In addition, the interrupt status is checked for errors.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
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
static void HandleEmacFifoIntr(XEmac * InstancePtr)
{
	u32 IntrStatus;

	/*
	 * The EMAC generates interrupts for errors and generates the transmit
	 * and receive done interrupts for data. We clear the interrupts
	 * immediately so that any latched status interrupt bits will reflect the
	 * true status of the device, and so any pulsed interrupts (non-status)
	 * generated during the Isr will not be lost.
	 */
	IntrStatus = XEMAC_READ_IISR(InstancePtr->BaseAddress);
	XEMAC_WRITE_IISR(InstancePtr->BaseAddress, IntrStatus);

	if (IntrStatus & XEM_EIR_RECV_DONE_MASK) {
		/*
		 * Configured for direct memory-mapped I/O using FIFO with interrupts.
		 * This interrupt means the RPLR is non-empty, indicating a frame has
		 * arrived.
		 */
		InstancePtr->Stats.RecvInterrupts++;

		InstancePtr->FifoRecvHandler(InstancePtr->FifoRecvRef);

		/*
		 * The upper layer has removed as many frames as it wants to, so we
		 * need to clear the RECV_DONE bit before leaving the ISR so that it
		 * reflects the current state of the hardware (because it's a level
		 * interrupt that is latched in the IPIF interrupt status register).
		 * Note that if we've reached this point the bit is guaranteed to be
		 * set because it was cleared at the top of this ISR before any frames
		 * were serviced, so the bit was set again immediately by hardware
		 * because the RPLR was not yet emptied by software.
		 */
		XEMAC_WRITE_IISR(InstancePtr->BaseAddress,
				      XEM_EIR_RECV_DONE_MASK);
	}

	/*
	 * If configured for direct memory-mapped I/O using FIFO, the xmit status
	 * FIFO must be read and the callback invoked regardless of success or not.
	 */
	if (IntrStatus & XEM_EIR_XMIT_DONE_MASK) {
		u32 XmitStatus;

		InstancePtr->Stats.XmitInterrupts++;

		XmitStatus =
			XIo_In32(InstancePtr->BaseAddress + XEM_TSR_OFFSET);

		/*
		 * Collision errors are stored in the transmit status register
		 * instead of the interrupt status register
		 */
		if (XmitStatus & XEM_TSR_EXCESS_DEFERRAL_MASK) {
			InstancePtr->Stats.XmitExcessDeferral++;
		}

		if (XmitStatus & XEM_TSR_LATE_COLLISION_MASK) {
			InstancePtr->Stats.XmitLateCollisionErrors++;
		}

		InstancePtr->FifoSendHandler(InstancePtr->FifoSendRef);

		/*
		 * Only one status is retrieved per interrupt. We need to clear the
		 * XMIT_DONE bit before leaving the ISR so that it reflects the current
		 * state of the hardware (because it's a level interrupt that is latched
		 * in the IPIF interrupt status register). Note that if we've reached
		 * this point the bit is guaranteed to be set because it was cleared at
		 * the top of this ISR before any statuses were serviced, so the bit was
		 * set again immediately by hardware because the TSR was not yet emptied
		 * by software.
		 */
		XEMAC_WRITE_IISR(InstancePtr->BaseAddress,
				      XEM_EIR_XMIT_DONE_MASK);
	}

	/*
	 * Check the MAC for errors
	 */
	XEmac_CheckEmacError(InstancePtr, IntrStatus);
}
