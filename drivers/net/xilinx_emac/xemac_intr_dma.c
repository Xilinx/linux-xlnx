/* $Id: xemac_intr_dma.c,v 1.1 2006/11/01 17:30:12 moleres Exp $ */
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
* @file xemac_intr_dma.c
*
* Contains functions used in interrupt mode when configured with scatter-gather
* DMA.
*
* The interrupt handler, XEmac_IntrHandlerDma(), must be connected by the user
* to the interrupt controller.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- ---------------------------------------------------------
* 1.00a rpm  07/31/01 First release
* 1.00b rpm  02/20/02 Repartitioned files and functions
* 1.00c rpm  12/05/02 New version includes support for simple DMA and the delay
*                     argument to SgSend
* 1.00c rpm  02/03/03 The XST_DMA_SG_COUNT_EXCEEDED return code was removed
*                     from SetPktThreshold in the internal DMA driver. Also
*                     avoided compiler warnings by initializing Result in the
*                     interrupt service routines.
* 1.00c rpm  03/26/03 Fixed a problem in the interrupt service routines where
*                     the interrupt status was toggled clear after a call to
*                     ErrorHandler, but if ErrorHandler reset the device the
*                     toggle actually asserted the interrupt because the
*                     reset had cleared it.
* 1.00d rpm  09/26/03 New version includes support PLB Ethernet and v2.00a of
*                     the packet fifo driver.
* 1.00e rmm  04/06/04 Instead of invoking once for each packet received,
*                     send/recv callbacks are invoked once for all packets.
*                     Added functions XEmac_GetSgRecvFreeDesc() and XEmac_Get-
*                     SgSendFreeDesc().
* 1.01a ecm  09/01/05 Added DRE support through Control words in instance which
*                     are set at initialization.
* 1.01a wgr  09/14/06 Ported to Linux 2.6
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xemac_i.h"
#include "xio.h"
#include "xbuf_descriptor.h"
#include "xdma_channel.h"
#include "xipif_v1_23_b.h"	/* Uses v1.23b of the IPIF */

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Variable Definitions *****************************/


/************************** Function Prototypes ******************************/

static void HandleDmaRecvIntr(XEmac * InstancePtr);
static void HandleDmaSendIntr(XEmac * InstancePtr);
static void HandleEmacDmaIntr(XEmac * InstancePtr);

/*****************************************************************************/
/**
*
* Send an Ethernet frame using scatter-gather DMA. The caller attaches the
* frame to one or more buffer descriptors, then calls this function once for
* each descriptor. The caller is responsible for allocating and setting up the
* descriptor. An entire Ethernet frame may or may not be contained within one
* descriptor.  This function simply inserts the descriptor into the scatter-
* gather engine's transmit list. The caller is responsible for providing mutual
* exclusion to guarantee that a frame is contiguous in the transmit list. The
* buffer attached to the descriptor must be 32-bit aligned if using the OPB
* Ethernet core and 64-bit aligned if using the PLB Ethernet core.
*
* The driver updates the descriptor with the device control register before
* being inserted into the transmit list.  If this is the last descriptor in
* the frame, the inserts are committed, which means the descriptors for this
* frame are now available for transmission.
*
* It is assumed that the upper layer software supplies a correctly formatted
* Ethernet frame, including the destination and source addresses, the
* type/length field, and the data field.  It is also assumed that upper layer
* software does not append FCS at the end of the frame.
*
* This call is non-blocking.  Notification of error or successful transmission
* is done asynchronously through the send or error callback function.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param BdPtr is the address of a descriptor to be inserted into the transmit
*        ring.
* @param Delay indicates whether to start the scatter-gather DMA channel
*        immediately, or whether to wait. This allows the user to build up a
*        list of more than one descriptor before starting the transmission of
*        the packets, which allows the application to keep up with DMA and have
*        a constant stream of frames being transmitted. Use XEM_SGDMA_NODELAY or
*        XEM_SGDMA_DELAY, defined in xemac.h, as the value of this argument. If
*        the user chooses to delay and build a list, the user must call this
*        function with the XEM_SGDMA_NODELAY option or call XEmac_Start() to
*        kick off the tranmissions.
*
* @return
*
* - XST_SUCCESS if the buffer was successfull sent
* - XST_DEVICE_IS_STOPPED if the Ethernet MAC has not been started yet
* - XST_NOT_SGDMA if the device is not in scatter-gather DMA mode
* - XST_DMA_SG_LIST_FULL if the descriptor list for the DMA channel is full
* - XST_DMA_SG_BD_LOCKED if the DMA channel cannot insert the descriptor into
*   the list because a locked descriptor exists at the insert point
* - XST_DMA_SG_NOTHING_TO_COMMIT if even after inserting a descriptor into the
*   list, the DMA channel believes there are no new descriptors to commit. If
*   this is ever encountered, there is likely a thread mutual exclusion problem
*   on transmit.
*
* @note
*
* This function is not thread-safe. The user must provide mutually exclusive
* access to this function if there are to be multiple threads that can call it.
*
* @internal
*
* A status that should never be returned from this function, although
* the code is set up to handle it, is XST_DMA_SG_NO_LIST. Starting the device
* requires a list to be created, and this function requires the device to be
* started.
*
******************************************************************************/
int XEmac_SgSend(XEmac * InstancePtr, XBufDescriptor * BdPtr, int Delay)
{
	int Result;
	u32 BdControl;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure the device is configured for scatter-gather DMA, then be sure
	 * it is started.
	 */
	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	/*
	 * Set some descriptor control word defaults (source address increment
	 * and local destination address) and the destination address
	 * (the FIFO).  These are the same for every transmit descriptor.
	 */
	BdControl = XBufDescriptor_GetControl(BdPtr);
	XBufDescriptor_SetControl(BdPtr,
				  BdControl | (InstancePtr->TxDmaControlWord));

	/*
	 * If the TX channel has Checksum offload enabled, do not overwrite
	 * the Data in the destination address location, it is offload related
	 * not the actual address for the data to be written to
	 */
	if ((InstancePtr->TxDmaControlWord & XDC_DMACR_CS_OFFLOAD_MASK) == 0) {
		XBufDescriptor_SetDestAddress(BdPtr,
					      InstancePtr->PhysAddress +
					      XEM_PFIFO_TXDATA_OFFSET);
	}

	/*
	 * Put the descriptor in the send list. The DMA component accesses data
	 * here that can also be modified in interrupt context, so a critical
	 * section is required.
	 */
	XIIF_V123B_GINTR_DISABLE(InstancePtr->BaseAddress);

	Result = XDmaChannel_PutDescriptor(&InstancePtr->SendChannel, BdPtr);
	if (Result != XST_SUCCESS) {
		XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);
		return Result;
	}

	/*
	 * If this is the last buffer in the frame, commit the inserts and start
	 * the DMA engine if necessary
	 */
	if (XBufDescriptor_IsLastControl(BdPtr)) {
		Result = XDmaChannel_CommitPuts(&InstancePtr->SendChannel);
		if (Result != XST_SUCCESS) {
			XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);
			return Result;
		}

		if (Delay == XEM_SGDMA_NODELAY) {
			/*
			 * Start the DMA channel. Ignore the return status since we know the
			 * list exists and has at least one entry and we don't care if the
			 * channel is already started.  The DMA component accesses data here
			 * that can be modified at interrupt or task levels, so a critical
			 * section is required.
			 */
			(void) XDmaChannel_SgStart(&InstancePtr->SendChannel);
		}
	}

	XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Add a descriptor, with an attached empty buffer, into the receive descriptor
* list. The buffer attached to the descriptor must be 32-bit aligned if using
* the OPB Ethernet core and 64-bit aligned if using the PLB Ethernet core.
* This function is used by the upper layer software during initialization when
* first setting up the receive descriptors, and also during reception of frames
* to replace filled buffers with empty buffers. This function can be called
* when the device is started or stopped. Note that it does start the scatter-
* gather DMA engine.  Although this is not necessary during initialization, it
* is not a problem during initialization because the MAC receiver is not yet
* started.
*
* The buffer attached to the descriptor must be aligned on both the front end
* and the back end.
*
* Notification of received frames are done asynchronously through the receive
* callback function.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param BdPtr is a pointer to the buffer descriptor that will be added to the
*        descriptor list.
*
* @return
*
* - XST_SUCCESS if a descriptor was successfully returned to the driver
* - XST_NOT_SGDMA if the device is not in scatter-gather DMA mode
* - XST_DMA_SG_LIST_FULL if the receive descriptor list is full
* - XST_DMA_SG_BD_LOCKED if the DMA channel cannot insert the descriptor into
*   the list because a locked descriptor exists at the insert point.
* - XST_DMA_SG_NOTHING_TO_COMMIT if even after inserting a descriptor into the
*   list, the DMA channel believes there are no new descriptors to commit.
*
* @internal
*
* A status that should never be returned from this function, although
* the code is set up to handle it, is XST_DMA_SG_NO_LIST. Starting the device
* requires a list to be created, and this function requires the device to be
* started.
*
******************************************************************************/
int XEmac_SgRecv(XEmac * InstancePtr, XBufDescriptor * BdPtr)
{
	int Result;
	u32 BdControl;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure the device is configured for scatter-gather DMA
	 */
	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	/*
	 * Set some descriptor control word defaults (destination address increment
	 * and local source address) and the source address (the FIFO). These are
	 * the same for every receive descriptor.
	 */
	BdControl = XBufDescriptor_GetControl(BdPtr);
	XBufDescriptor_SetControl(BdPtr,
				  BdControl | (InstancePtr->RxDmaControlWord));
	XBufDescriptor_SetSrcAddress(BdPtr,
				     InstancePtr->PhysAddress +
				     XEM_PFIFO_RXDATA_OFFSET);

	/*
	 * Put the descriptor into the channel's descriptor list and commit.
	 * Although this function is likely called within interrupt context, there
	 * is the possibility that the upper layer software queues it to a task.
	 * In this case, a critical section is needed here to protect shared data
	 * in the DMA component.
	 */
	XIIF_V123B_GINTR_DISABLE(InstancePtr->BaseAddress);

	Result = XDmaChannel_PutDescriptor(&InstancePtr->RecvChannel, BdPtr);
	if (Result != XST_SUCCESS) {
		XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);
		return Result;
	}

	Result = XDmaChannel_CommitPuts(&InstancePtr->RecvChannel);
	if (Result != XST_SUCCESS) {
		XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);
		return Result;
	}

	/*
	 * Start the DMA channel. Ignore the return status since we know the list
	 * exists and has at least one entry and we don't care if the channel is
	 * already started. The DMA component accesses data here that can be
	 * modified at interrupt or task levels, so a critical section is required.
	 */
	(void) XDmaChannel_SgStart(&InstancePtr->RecvChannel);

	XIIF_V123B_GINTR_ENABLE(InstancePtr->BaseAddress);

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* The interrupt handler for the Ethernet driver when configured with scatter-
* gather DMA.
*
* Get the interrupt status from the IpIf to determine the source of the
* interrupt.  The source can be: MAC, Recv Packet FIFO, Send Packet FIFO, Recv
* DMA channel, or Send DMA channel. The packet FIFOs only interrupt during
* "deadlock" conditions.
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
void XEmac_IntrHandlerDma(void *InstancePtr)
{
	u32 IntrStatus;
	XEmac *EmacPtr = (XEmac *) InstancePtr;

	EmacPtr->Stats.TotalIntrs++;

	/*
	 * Get the interrupt status from the IPIF. There is no clearing of
	 * interrupts in the IPIF. Interrupts must be cleared at the source.
	 */
	IntrStatus = XIIF_V123B_READ_DIPR(EmacPtr->BaseAddress);

	/*
	 * See which type of interrupt is being requested, and service it
	 */
	if (IntrStatus & XEM_IPIF_RECV_DMA_MASK) {	/* Receive DMA interrupt */
		EmacPtr->Stats.RecvInterrupts++;
		HandleDmaRecvIntr(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_SEND_DMA_MASK) {	/* Send DMA interrupt */
		EmacPtr->Stats.XmitInterrupts++;
		HandleDmaSendIntr(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_EMAC_MASK) {	/* MAC interrupt */
		EmacPtr->Stats.EmacInterrupts++;
		HandleEmacDmaIntr(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_RECV_FIFO_MASK) {	/* Receive FIFO interrupt */
		EmacPtr->Stats.RecvInterrupts++;
		XEmac_CheckFifoRecvError(EmacPtr);
	}

	if (IntrStatus & XEM_IPIF_SEND_FIFO_MASK) {	/* Send FIFO interrupt */
		EmacPtr->Stats.XmitInterrupts++;
		XEmac_CheckFifoSendError(EmacPtr);
	}

	if (IntrStatus & XIIF_V123B_ERROR_MASK) {
		/*
		 * An error occurred internal to the IPIF. This is more of a debug and
		 * integration issue rather than a production error. Don't do anything
		 * other than clear it, which provides a spot for software to trap
		 * on the interrupt and begin debugging.
		 */
		XIIF_V123B_WRITE_DISR(EmacPtr->BaseAddress,
				      XIIF_V123B_ERROR_MASK);
	}
}

/*****************************************************************************/
/**
*
* Set the packet count threshold for this device. The device must be stopped
* before setting the threshold. The packet count threshold is used for interrupt
* coalescing, which reduces the frequency of interrupts from the device to the
* processor. In this case, the scatter-gather DMA engine only interrupts when
* the packet count threshold is reached, instead of interrupting for each packet.
* A packet is a generic term used by the scatter-gather DMA engine, and is
* equivalent to an Ethernet frame in our case.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param Direction indicates the channel, send or receive, from which the
*        threshold register is read.
* @param Threshold is the value of the packet threshold count used during
*        interrupt coalescing. A value of 0 disables the use of packet threshold
*        by the hardware.
*
* @return
*
* - XST_SUCCESS if the threshold was successfully set
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_DEVICE_IS_STARTED if the device has not been stopped
* - XST_INVALID_PARAM if the Direction parameter is invalid. Turning on
*   asserts would also catch this error.
*
* @note
*
* The packet threshold could be set to larger than the number of descriptors
* allocated to the DMA channel. In this case, the wait bound will take over
* and always indicate data arrival. There was a check in this function that
* returned an error if the treshold was larger than the number of descriptors,
* but that was removed because users would then have to set the threshold
* only after they set descriptor space, which is an order dependency that
* caused confustion.
*
******************************************************************************/
int XEmac_SetPktThreshold(XEmac * InstancePtr, u32 Direction, u8 Threshold)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(Direction == XEM_SEND || Direction == XEM_RECV);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure device is configured for scatter-gather DMA and has been stopped
	 */
	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return XST_DEVICE_IS_STARTED;
	}

	/*
	 * Based on the direction, set the packet threshold in the
	 * corresponding DMA channel component.  Default to the receive
	 * channel threshold register (if an invalid Direction is passed).
	 */
	switch (Direction) {
	case XEM_SEND:
		return XDmaChannel_SetPktThreshold(&InstancePtr->SendChannel,
						   Threshold);

	case XEM_RECV:
		return XDmaChannel_SetPktThreshold(&InstancePtr->RecvChannel,
						   Threshold);

	default:
		return XST_INVALID_PARAM;
	}
}

/*****************************************************************************/
/**
*
* Get the value of the packet count threshold for this driver/device. The packet
* count threshold is used for interrupt coalescing, which reduces the frequency
* of interrupts from the device to the processor. In this case, the
* scatter-gather DMA engine only interrupts when the packet count threshold is
* reached, instead of interrupting for each packet. A packet is a generic term
* used by the scatter-gather DMA engine, and is equivalent to an Ethernet frame
* in our case.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param Direction indicates the channel, send or receive, from which the
*        threshold register is read.
* @param ThreshPtr is a pointer to the byte into which the current value of the
*        packet threshold register will be copied. An output parameter. A value
*        of 0 indicates the use of packet threshold by the hardware is disabled.
*
* @return
*
* - XST_SUCCESS if the packet threshold was retrieved successfully
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_INVALID_PARAM if the Direction parameter is invalid. Turning on
*   asserts would also catch this error.
*
* @note
*
* None.
*
******************************************************************************/
int XEmac_GetPktThreshold(XEmac * InstancePtr, u32 Direction, u8 *ThreshPtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(Direction == XEM_SEND || Direction == XEM_RECV);
	XASSERT_NONVOID(ThreshPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	/*
	 * Based on the direction, return the packet threshold set in the
	 * corresponding DMA channel component.  Default to the value in
	 * the receive channel threshold register (if an invalid Direction
	 * is passed).
	 */
	switch (Direction) {
	case XEM_SEND:
		*ThreshPtr =
			XDmaChannel_GetPktThreshold(&InstancePtr->SendChannel);
		break;

	case XEM_RECV:
		*ThreshPtr =
			XDmaChannel_GetPktThreshold(&InstancePtr->RecvChannel);
		break;

	default:
		return XST_INVALID_PARAM;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Set the packet wait bound timer for this driver/device. The device must be
* stopped before setting the timer value. The packet wait bound is used during
* interrupt coalescing to trigger an interrupt when not enough packets have been
* received to reach the packet count threshold. A packet is a generic term used
* by the scatter-gather DMA engine, and is equivalent to an Ethernet frame in
* our case. The timer is in milliseconds.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param Direction indicates the channel, send or receive, from which the
*        threshold register is read.
* @param TimerValue is the value of the packet wait bound used during interrupt
*        coalescing. It is in milliseconds in the range 0  - 1023. A value of 0
*        disables the packet wait bound timer.
*
* @return
*
* - XST_SUCCESS if the packet wait bound was set successfully
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_DEVICE_IS_STARTED if the device has not been stopped
* - XST_INVALID_PARAM if the Direction parameter is invalid. Turning on
*   asserts would also catch this error.
*
* @note
*
* None.
*
******************************************************************************/
int XEmac_SetPktWaitBound(XEmac * InstancePtr, u32 Direction, u32 TimerValue)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(Direction == XEM_SEND || Direction == XEM_RECV);
	XASSERT_NONVOID(TimerValue <= XEM_SGDMA_MAX_WAITBOUND);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Be sure device is configured for scatter-gather DMA and has been stopped
	 */
	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return XST_DEVICE_IS_STARTED;
	}

	/*
	 * Based on the direction, set the packet wait bound in the
	 * corresponding DMA channel component.  Default to the receive
	 * channel wait bound register (if an invalid Direction is passed).
	 */
	switch (Direction) {
	case XEM_SEND:
		XDmaChannel_SetPktWaitBound(&InstancePtr->SendChannel,
					    TimerValue);
		break;

	case XEM_RECV:
		XDmaChannel_SetPktWaitBound(&InstancePtr->RecvChannel,
					    TimerValue);
		break;

	default:
		return XST_INVALID_PARAM;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Get the packet wait bound timer for this driver/device. The packet wait bound
* is used during interrupt coalescing to trigger an interrupt when not enough
* packets have been received to reach the packet count threshold. A packet is a
* generic term used by the scatter-gather DMA engine, and is equivalent to an
* Ethernet frame in our case. The timer is in milliseconds.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param Direction indicates the channel, send or receive, from which the
*        threshold register is read.
* @param WaitPtr is a pointer to the byte into which the current value of the
*        packet wait bound register will be copied. An output parameter. Units
*        are in milliseconds in the range 0  - 1023. A value of 0 indicates the
*        packet wait bound timer is disabled.
*
* @return
*
* - XST_SUCCESS if the packet wait bound was retrieved successfully
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_INVALID_PARAM if the Direction parameter is invalid. Turning on
*   asserts would also catch this error.
*
* @note
*
* None.
*
******************************************************************************/
int XEmac_GetPktWaitBound(XEmac * InstancePtr, u32 Direction, u32 *WaitPtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(Direction == XEM_SEND || Direction == XEM_RECV);
	XASSERT_NONVOID(WaitPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	/*
	 * Based on the direction, return the packet wait bound set in the
	 * corresponding DMA channel component.  Default to the value in
	 * the receive channel wait bound register (if an invalid Direction
	 * is passed).
	 */
	switch (Direction) {
	case XEM_SEND:
		*WaitPtr =
			XDmaChannel_GetPktWaitBound(&InstancePtr->SendChannel);
		break;

	case XEM_RECV:
		*WaitPtr =
			XDmaChannel_GetPktWaitBound(&InstancePtr->RecvChannel);
		break;

	default:
		return XST_INVALID_PARAM;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Give the driver the memory space to be used for the scatter-gather DMA
* receive descriptor list. This function should only be called once, during
* initialization of the Ethernet driver. The memory space must be big enough
* to hold some number of descriptors, depending on the needs of the system.
* The xemac.h file defines minimum and default numbers of descriptors
* which can be used to allocate this memory space.
*
* The memory space must be 32-bit aligned. An assert will occur if asserts
* are turned on and the memory is not aligned.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param MemoryPtr is a pointer to the aligned memory.
* @param ByteCount is the length, in bytes, of the memory space.
*
* @return
*
* - XST_SUCCESS if the space was initialized successfully
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_DMA_SG_LIST_EXISTS if this list space has already been created
*
* @note
*
* If the device is configured for scatter-gather DMA, this function must be
* called AFTER the XEmac_Initialize() function because the DMA channel
* components must be initialized before the memory space is set.
*
******************************************************************************/
int XEmac_SetSgRecvSpace(XEmac * InstancePtr, u32 *MemoryPtr,
			 u32 ByteCount, void *PhyPtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(MemoryPtr != NULL);
	XASSERT_NONVOID(ByteCount != 0);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	return XDmaChannel_CreateSgList(&InstancePtr->RecvChannel, MemoryPtr,
					ByteCount, PhyPtr);
}

/*****************************************************************************/
/**
*
* Give the driver the memory space to be used for the scatter-gather DMA
* transmit descriptor list. This function should only be called once, during
* initialization of the Ethernet driver. The memory space must be big enough
* to hold some number of descriptors, depending on the needs of the system.
* The xemac.h file defines minimum and default numbers of descriptors
* which can be used to allocate this memory space.
*
* The memory space must be 32-bit aligned. An assert will occur if asserts
* are turned on and the memory is not aligned.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param MemoryPtr is a pointer to the aligned memory.
* @param ByteCount is the length, in bytes, of the memory space.
*
* @return
*
* - XST_SUCCESS if the space was initialized successfully
* - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA
* - XST_DMA_SG_LIST_EXISTS if this list space has already been created
*
* @note
*
* If the device is configured for scatter-gather DMA, this function must be
* called AFTER the XEmac_Initialize() function because the DMA channel
* components must be initialized before the memory space is set.
*
******************************************************************************/
int XEmac_SetSgSendSpace(XEmac * InstancePtr, u32 *MemoryPtr,
			 u32 ByteCount, void *PhyPtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(MemoryPtr != NULL);
	XASSERT_NONVOID(ByteCount != 0);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	if (!XEmac_mIsSgDma(InstancePtr)) {
		return XST_NOT_SGDMA;
	}

	return XDmaChannel_CreateSgList(&InstancePtr->SendChannel, MemoryPtr,
					ByteCount, PhyPtr);
}

/*****************************************************************************/
/**
*
* Return the number of free buffer descriptor slots that can be added to the
* send descriptor ring with XEmac_SgSend() before filling it up.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* - The number of descriptors that can be given to the HW with XEmac_SgSend()
* - 0 if no room is left or the device is not configured for SG DMA
*
* @note
*
* None.
*
******************************************************************************/
unsigned XEmac_GetSgSendFreeDesc(XEmac * InstancePtr)
{
	unsigned Slots;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	Slots = InstancePtr->SendChannel.TotalDescriptorCount -
		InstancePtr->SendChannel.ActiveDescriptorCount;

	return Slots;
}


/*****************************************************************************/
/**
*
* Return the number of free buffer descriptor slots that can be added to the
* receive descriptor ring with XEmac_SgRecv() before filling it up.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* - The number of descriptors that can be given to the HW with XEmac_SgRecv()
* - 0 if no room is left or the device is not configured for SG DMA
*
* @note
*
* None.
*
******************************************************************************/
unsigned XEmac_GetSgRecvFreeDesc(XEmac * InstancePtr)
{
	unsigned Slots;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	Slots = InstancePtr->RecvChannel.TotalDescriptorCount -
		InstancePtr->RecvChannel.ActiveDescriptorCount;

	return Slots;
}


/*****************************************************************************/
/**
*
* Set the callback function for handling received frames in scatter-gather DMA
* mode.  The upper layer software should call this function during
* initialization.  The callback is called once per frame received. The head of
* a descriptor list is passed in along with the number of descriptors in the
* list. Before leaving the callback, the upper layer software should attach a
* new buffer to each descriptor in the list.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. Sending the received frame up the protocol stack
* should be done at task-level. If there are other potentially slow operations
* within the callback, these too should be done at task-level.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param CallBackRef is a reference pointer to be passed back to the adapter in
*        the callback. This helps the adapter correlate the callback to a
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
void XEmac_SetSgRecvHandler(XEmac * InstancePtr, void *CallBackRef,
			    XEmac_SgHandler FuncPtr)
{
	/*
	 * Asserted IsDmaSg here instead of run-time check because there is really
	 * no ill-effects of setting these when not configured for scatter-gather.
	 */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(XEmac_mIsSgDma(InstancePtr));
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->SgRecvHandler = FuncPtr;
	InstancePtr->SgRecvRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Set the callback function for handling confirmation of transmitted frames in
* scatter-gather DMA mode.  The upper layer software should call this function
* during initialization.  The callback is called once per frame sent. The head
* of a descriptor list is passed in along with the number of descriptors in
* the list. The callback is responsible for freeing buffers attached to these
* descriptors.
*
* The callback is invoked by the driver within interrupt context, so it needs
* to do its job quickly. If there are potentially slow operations within the
* callback, these should be done at task-level.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
* @param CallBackRef is a reference pointer to be passed back to the adapter in
*        the callback. This helps the adapter correlate the callback to a
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
void XEmac_SetSgSendHandler(XEmac * InstancePtr, void *CallBackRef,
			    XEmac_SgHandler FuncPtr)
{
	/*
	 * Asserted IsDmaSg here instead of run-time check because there is really
	 * no ill-effects of setting these when not configured for scatter-gather.
	 */
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(XEmac_mIsSgDma(InstancePtr));
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->SgSendHandler = FuncPtr;
	InstancePtr->SgSendRef = CallBackRef;
}

/*****************************************************************************/
/*
*
* Handle an interrupt from the DMA receive channel. DMA interrupts are:
*
* - DMA error. DMA encountered a bus error or timeout. This is a fatal error
*   that requires reset of the channel.  The driver calls the error handler
*   of the upper layer software with an error code indicating the device should
*   be reset.
* - Packet count threshold reached.  For scatter-gather operations, indicates
*   the threshold for the number of packets not serviced by software has been
*   reached. The driver behaves as follows:
*       - Get the value of the packet counter, which tells us how many packets
*         are ready to be serviced
*       - For each packet
*           - For each descriptor, remove it from the scatter-gather list
*           - Check for the last descriptor in the frame, and if set
*               - Bump frame statistics
*               - Decrement the packet counter by one
*       - Call the scatter-gather receive callback function
*       Note that there are no receive errors reported in the status word of
*       the buffer descriptor.  If receive errors occur, the MAC drops the
*       packet, and we only find out about the errors through various error
*       count registers.
* - Packet wait bound reached.  For scatter-gather, indicates the time to wait
*   for the next packet has expired.  The driver follows the same logic as when
*   the packet count threshold interrupt is received.
* - Scatter-gather end acknowledge.  Hardware has reached the end of the
*   descriptor list.  The driver follows the same logic as when the packet count
*   threshold interrupt is received. In addition, the driver restarts the DMA
*   scatter-gather channel in case there are newly inserted descriptors.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Although the function returns void, there are asynchronous errors that can
* be generated (by calling the ErrorHandler) from this function.  These are:
* - XST_DMA_SG_LIST_EMPTY indicates we tried to get a buffer descriptor from the
*   DMA channel, but there was not one ready for software.
* - XST_DMA_ERROR indicates a DMA bus error or timeout occurred. This is a fatal
*   error that requires reset.
*
* @note
*
* None.
*
******************************************************************************/
static void HandleDmaRecvIntr(XEmac * InstancePtr)
{
	int Result;
	u32 IntrStatus;
	u32 NumBds;
	u32 PacketsLeft;
	XBufDescriptor *BdHeadPtr;
	XBufDescriptor *BdPtr;

	/*
	 * Read the interrupt status
	 */
	IntrStatus = XDmaChannel_GetIntrStatus(&InstancePtr->RecvChannel);

	/*
	 * For packet threshold or wait bound interrupts, process desciptors. Also
	 * process descriptors on a SG end acknowledgement, which means the end of
	 * the descriptor list has been reached by the hardware. For receive, this
	 * is potentially trouble since it means the descriptor list is full,
	 * unless software can process enough packets quickly enough so the
	 * hardware has room to put new packets.
	 */
	if (IntrStatus & (XDC_IXR_PKT_THRESHOLD_MASK |
			  XDC_IXR_PKT_WAIT_BOUND_MASK | XDC_IXR_SG_END_MASK)) {
		/* Get the number of packets that need processing */
		PacketsLeft =
			XDmaChannel_GetPktCount(&InstancePtr->RecvChannel);

		if (PacketsLeft) {
			/* Get the buffer descriptor at the head of the list */
			Result = XDmaChannel_GetDescriptor(&InstancePtr->
							   RecvChannel,
							   &BdHeadPtr);
			BdPtr = BdHeadPtr;
			NumBds = 0;

			/* Loop until all packets have been pulled or an error occurs */
			while (1) {
				NumBds++;

				/*
				 * An error getting a buffer descriptor from the list.
				 * This should not happen, but if it does, report it to
				 * the error callback and break out of the loop to service
				 * other interrupts.
				 */
				if (Result != XST_SUCCESS) {
					InstancePtr->ErrorHandler(InstancePtr->
								  ErrorRef,
								  Result);
					break;
				}

				/* Bump statistics */
				InstancePtr->Stats.RecvBytes +=
					XBufDescriptor_GetLength(BdPtr);

				/* Have all BDs been read for this packet */
				if (XBufDescriptor_IsLastStatus(BdPtr)) {
					/*
					 * Decrement the packet count register to reflect the fact
					 * we just processed a packet
					 */
					XDmaChannel_DecrementPktCount
						(&InstancePtr->RecvChannel);

					/* Bump statistics */
					InstancePtr->Stats.RecvFrames++;

					/* Test loop exit condition */
					if (--PacketsLeft == 0) {
						break;
					}
				}

				/* Get the next buffer descriptor in the list */
				Result = XDmaChannel_GetDescriptor
					(&InstancePtr->RecvChannel, &BdPtr);
			}	/* while */

			/*
			 * Check for error that occurred inside the while loop, and break
			 * out of the for loop if there was one so other interrupts can
			 * be serviced.
			 */
			if (Result == XST_SUCCESS) {
				/*
				 * Make the callback to the upper layers, passing it the first
				 * descriptor in the first packet and the number of descriptors
				 * in the list.
				 */
				InstancePtr->SgRecvHandler(InstancePtr->
							   SgRecvRef, BdHeadPtr,
							   NumBds);
			}
		}		/* if (PacketsLeft) */

		/*
		 * If the interrupt was an end-ack, check the descriptor list again to
		 * see if it is empty. If not, go ahead and restart the scatter-gather
		 * channel. This is to fix a possible race condition where, on receive,
		 * the driver attempted to start a scatter-gather channel that was
		 * already started, which resulted in no action from the XDmaChannel
		 * component. But, just after the XDmaChannel component saw that the
		 * hardware was already started, the hardware stopped because it
		 * reached the end of the list.  In that case, this interrupt is
		 * generated and we can restart the hardware here.
		 */
		if (IntrStatus & XDC_IXR_SG_END_MASK) {
			/*
			 * Ignore the return status since we know the list exists and we
			 * don't care if the list is empty or the channel is already started.
			 */
			(void) XDmaChannel_SgStart(&InstancePtr->RecvChannel);
		}
	}

	/*
	 * All interrupts are handled (except the error below) so acknowledge
	 * (clear) the interrupts by writing the value read above back to the status
	 * register. The packet count interrupt must be acknowledged after the
	 * decrement, otherwise it will come right back. We clear the interrupts
	 * before we handle the error interrupt because the ErrorHandler should
	 * result in a reset, which clears the interrupt status register. So we
	 * don't want to toggle the interrupt back on by writing the interrupt
	 * status register with an old value after a reset.
	 */
	XDmaChannel_SetIntrStatus(&InstancePtr->RecvChannel, IntrStatus);

	/*
	 * Check for DMA errors and call the error callback function if an error
	 * occurred (DMA bus or timeout error), which should result in a reset of
	 * the device by the upper layer software.
	 */
	if (IntrStatus & XDC_IXR_DMA_ERROR_MASK) {
		InstancePtr->Stats.DmaErrors++;
		InstancePtr->ErrorHandler(InstancePtr->ErrorRef, XST_DMA_ERROR);
	}
}

/*****************************************************************************/
/*
*
* Handle an interrupt from the DMA send channel. DMA interrupts are:
*
* - DMA error. DMA encountered a bus error or timeout. This is a fatal error
*   that requires reset of the channel.  The driver calls the error handler
*   of the upper layer software with an error code indicating the device should
*   be reset.
* - Packet count threshold reached.  For scatter-gather operations, indicates
*   the threshold for the number of packets not serviced by software has been
*   reached. The driver behaves as follows:
*       - Get the value of the packet counter, which tells us how many packets
*         are ready to be serviced
*       - For each packet
*           - For each descriptor, remove it from the scatter-gather list
*           - Check for the last descriptor in the frame, and if set
*               - Bump frame statistics
*               - Decrement the packet counter by one
*       - Call the scatter-gather receive callback function
*       Note that there are no receive errors reported in the status word of
*       the buffer descriptor.  If receive errors occur, the MAC drops the
*       packet, and we only find out about the errors through various error
*       count registers.
* - Packet wait bound reached.  For scatter-gather, indicates the time to wait
*   for the next packet has expired.  The driver follows the same logic as when
*   the packet count threshold interrupt is received.
* - Scatter-gather end acknowledge.  Hardware has reached the end of the
*   descriptor list.  The driver follows the same logic as when the packet count
*   threshold interrupt is received. In addition, the driver restarts the DMA
*   scatter-gather channel in case there are newly inserted descriptors.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Although the function returns void, there are asynchronous errors
* that can be generated from this function.  These are:
* - XST_DMA_SG_LIST_EMPTY indicates we tried to get a buffer descriptor from
*   the DMA channel, but there was not one ready for software.
* - XST_DMA_ERROR indicates a DMA bus error or timeout occurred. This is a
*   fatal error that requires reset.
*
* @note
*
* None.
*
******************************************************************************/
static void HandleDmaSendIntr(XEmac * InstancePtr)
{
	int Result;
	u32 IntrStatus;
	u32 NumBds;
	u32 PacketsLeft;
	u32 XmitStatus;
	int PacketStart;
	XBufDescriptor *BdHeadPtr;
	XBufDescriptor *BdPtr;

	/*
	 * Read the interrupt status
	 */
	IntrStatus = XDmaChannel_GetIntrStatus(&InstancePtr->SendChannel);

	/*
	 * For packet threshold or wait bound interrupt, process descriptors. Also
	 * process descriptors on a SG end acknowledgement, which means the end of
	 * the descriptor list has been reached by the hardware. For transmit,
	 * this is a normal condition during times of light traffic.  In fact, the
	 * wait bound interrupt may be masked for transmit since the end-ack would
	 * always occur before the wait bound expires.
	 */
	if (IntrStatus & (XDC_IXR_PKT_THRESHOLD_MASK |
			  XDC_IXR_PKT_WAIT_BOUND_MASK | XDC_IXR_SG_END_MASK)) {
		/* Get the number of packets that need processing */
		PacketsLeft =
			XDmaChannel_GetPktCount(&InstancePtr->SendChannel);

		if (PacketsLeft) {
			/* Get the buffer descriptor at the head of the list */
			Result = XDmaChannel_GetDescriptor(&InstancePtr->
							   SendChannel,
							   &BdHeadPtr);
			BdPtr = BdHeadPtr;
			NumBds = 0;
			PacketStart = 1;

			/* Loop until all packets have been pulled or an error occurs */
			while (1) {
				NumBds++;

				/*
				 * An error getting a buffer descriptor from the list.
				 * This should not happen, but if it does, report it to
				 * the error callback and break out of the loop to service
				 * other interrupts.
				 */
				if (Result != XST_SUCCESS) {
					InstancePtr->ErrorHandler(InstancePtr->
								  ErrorRef,
								  Result);
					break;
				}

				/* Bump statistics */
				InstancePtr->Stats.XmitBytes +=
					XBufDescriptor_GetLength(BdPtr);

				/* If 1st BD in a packet, then check xmit status */
				if (PacketStart) {
					XmitStatus =
						XBufDescriptor_GetDeviceStatus
						(BdPtr);
					if (XmitStatus &
					    XEM_TSR_EXCESS_DEFERRAL_MASK) {
						InstancePtr->Stats.
							XmitExcessDeferral++;
					}

					if (XmitStatus &
					    XEM_TSR_LATE_COLLISION_MASK) {
						InstancePtr->Stats.
							XmitLateCollisionErrors++;
					}

					PacketStart = 0;
				}

				/* Have all BDs been read for this packet */
				if (XBufDescriptor_IsLastStatus(BdPtr)) {
					/*
					 * Decrement the packet count register to reflect the fact
					 * we just processed a packet
					 */
					XDmaChannel_DecrementPktCount
						(&InstancePtr->SendChannel);

					/* Bump statistics */
					InstancePtr->Stats.XmitFrames++;

					/* Test loop exit condition */
					if (--PacketsLeft == 0) {
						break;
					}

					/* Next BD will mark the beginning of a new packet */
					PacketStart = 1;
				}

				/* Get the next buffer descriptor in the list */
				Result = XDmaChannel_GetDescriptor
					(&InstancePtr->SendChannel, &BdPtr);
			}	/* while */

			/*
			 * Check for error that occurred inside the while loop, and break
			 * out of the for loop if there was one so other interrupts can
			 * be serviced.
			 */
			if (Result == XST_SUCCESS) {
				/*
				 * Make the callback to the upper layers, passing it the first
				 * descriptor in the first packet and the number of descriptors
				 * in the list.
				 */
				InstancePtr->SgSendHandler(InstancePtr->
							   SgSendRef, BdHeadPtr,
							   NumBds);
			}
		}		/* if (PacketsLeft) */

		/*
		 * If the interrupt was an end-ack, check the descriptor list again to
		 * see if it is empty. If not, go ahead and restart the scatter-gather
		 * channel. This is to fix a possible race condition where, on transmit,
		 * the driver attempted to start a scatter-gather channel that was
		 * already started, which resulted in no action from the XDmaChannel
		 * component. But, just after the XDmaChannel component saw that the
		 * hardware was already started, the hardware stopped because it
		 * reached the end of the list.  In that case, this interrupt is
		 * generated and we can restart the hardware here.
		 */
		if (IntrStatus & XDC_IXR_SG_END_MASK) {
			/*
			 * Ignore the return status since we know the list exists and we
			 * don't care if the list is empty or the channel is already started.
			 */
			(void) XDmaChannel_SgStart(&InstancePtr->SendChannel);
		}
	}

	/*
	 * All interrupts are handled (except the error below) so acknowledge
	 * (clear) the interrupts by writing the value read above back to the status
	 * register. The packet count interrupt must be acknowledged after the
	 * decrement, otherwise it will come right back. We clear the interrupts
	 * before we handle the error interrupt because the ErrorHandler should
	 * result in a reset, which clears the interrupt status register. So we
	 * don't want to toggle the interrupt back on by writing the interrupt
	 * status register with an old value after a reset.
	 */
	XDmaChannel_SetIntrStatus(&InstancePtr->SendChannel, IntrStatus);

	/*
	 * Check for DMA errors and call the error callback function if an error
	 * occurred (DMA bus or timeout error), which should result in a reset of
	 * the device by the upper layer software.
	 */
	if (IntrStatus & XDC_IXR_DMA_ERROR_MASK) {
		InstancePtr->Stats.DmaErrors++;
		InstancePtr->ErrorHandler(InstancePtr->ErrorRef, XST_DMA_ERROR);
	}
}

/*****************************************************************************/
/*
*
* Handle an interrupt from the Ethernet MAC when configured with scatter-gather
* DMA. The only interrupts handled in this case are errors.
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
static void HandleEmacDmaIntr(XEmac * InstancePtr)
{
	u32 IntrStatus;

	/*
	 * When configured with DMA, the EMAC generates interrupts only when errors
	 * occur. We clear the interrupts immediately so that any latched status
	 * interrupt bits will reflect the true status of the device, and so any
	 * pulsed interrupts (non-status) generated during the Isr will not be lost.
	 */
	IntrStatus = XIIF_V123B_READ_IISR(InstancePtr->BaseAddress);
	XIIF_V123B_WRITE_IISR(InstancePtr->BaseAddress, IntrStatus);

	/*
	 * Check the MAC for errors
	 */
	XEmac_CheckEmacError(InstancePtr, IntrStatus);
}
