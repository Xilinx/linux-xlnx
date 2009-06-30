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
 * @file xtemac_sgdma.c
 *
 * Functions in this file implement scatter-gather DMA frame transfer mode.
 * See xtemac.h for a detailed description of the driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a rmm  06/01/05 First release
 * 2.00a rmm  11/21/05 Switched to local link DMA driver
 *       rmm  06/22/06 Fixed C++ compiler warnings
 *
 * </pre>
 *
 ******************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac.h"
#include "xtemac_i.h"
#include "xdmav3_l.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Variable Definitions *****************************/


/************************** Function Prototypes ******************************/


/*****************************************************************************/
/**
 * Allocate a set of BDs from the given SGDMA channel. It is expected the user
 * will attach buffers and set other DMA transaction parameters to the returned
 * BDs in preparation to calling XTemac_SgCommit(). The set of BDs returned is
 * a list starting with the BdPtr and extending for NumBd BDs. The list can be
 * navigated with macros XTemac_mSgRecvBdNext() for the XTE_RECV channel, and
 * XTemac_mSgSendBdNext() for the XTE_SEND channel.
 *
 * The BDs returned by this function are a segment of the BD ring maintained
 * by the SGDMA driver. Do not modify BDs past the end of the returned list.
 * Doing so will cause data corruption and may lead to system instability.
 *
 * This function and XTemac_SgCommit() must be called in the correct order. See
 * xtemac.h for more information on the SGDMA use model.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address (XTE_SEND or XTE_RECV).
 * @param NumBd is the number of BDs to allocate.
 * @param BdPtr is an output parameter, it points to the first BD in the
 *        returned list.
 *
 * @return
 * - XST_SUCCESS if the requested number of BDs was returned.
 * - XST_INVALID_PARAM if Direction did not specify a valid channel.
 * - XST_FAILURE if there were not enough free BDs to satisfy the request.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
int XTemac_SgAlloc(XTemac *InstancePtr, u32 Direction,
		   unsigned NumBd, XDmaBdV3 ** BdPtr)
{
	int Status;
	XDmaV3 *DmaPtr;
	u32 DgieReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Which channel to address */
	if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else {
		return (XST_INVALID_PARAM);
	}

	/* XDmaV3_SgBdAlloc() will return either XST_SUCCESS or XST_FAILURE
	 * This is a critical section, prevent interrupts from the device while
	 * the BD ring is being modified.
	 */
	DgieReg = XTemac_mGetIpifReg(XTE_DGIE_OFFSET);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);
	Status = XDmaV3_SgBdAlloc(DmaPtr, NumBd, BdPtr);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, DgieReg);
	return (Status);
}

/*****************************************************************************/
/**
 * Fully or partially undo a XTemac_SgAlloc() operation. Use this function to
 * free BDs prior to being given to HW with XTemac_SgCommit().
 *
 * An UnAlloc operation may be required if for some reason there is an error
 * (OS out of resources for example) prior to committing them. The last BD
 * in the list provided by XTemac_SgAlloc() must be the last BD in the list
 * provided to XTemac_SgUnAlloc().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address (XTE_SEND or XTE_RECV).
 * @param NumBd is the number of BDs to allocate.
 * @param BdPtr is an output parameter, it points to the first BD in the
 *        returned list.
 *
 * @return
 * - XST_SUCCESS if the requested number of BDs was returned.
 * - XST_INVALID_PARAM if Direction did not specify a valid channel.
 * - XST_FAILURE if there were not enough free BDs to satisfy the request.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
int XTemac_SgUnAlloc(XTemac *InstancePtr, u32 Direction,
		     unsigned NumBd, XDmaBdV3 * BdPtr)
{
	int Status;
	XDmaV3 *DmaPtr;
	u32 DgieReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Which channel to address */
	if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else {
		return (XST_INVALID_PARAM);
	}

	/* XDmaV3_SgBdAlloc() will return either XST_SUCCESS or XST_FAILURE
	 * This is a critical section, prevent interrupts from the device while
	 * the BD ring is being modified.
	 */
	DgieReg = XTemac_mGetIpifReg(XTE_DGIE_OFFSET);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);
	Status = XDmaV3_SgBdUnAlloc(DmaPtr, NumBd, BdPtr);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, DgieReg);
	return (Status);
}

/*****************************************************************************/
/**
 * Commit a set of BDs to the SGDMA engine that had been allocated by
 * XTemac_SgAlloc() and prepared by the user to describe SGDMA transaction(s).
 *
 * This function and XTemac_SgAlloc() must be called in the correct order. See
 * xtemac.h for more information on the SGDMA use model.
 *
 * Upon return, the committed BDs go under hardware control. Do not modify BDs
 * after they have been committed. Doing so may cause data corruption and system
 * instability.
 *
 * This function may be called if the TEMAC device is started or stopped. If
 * started (see XTemac_Start()), then the BDs may be processed by HW at any
 * time.
 *
 * This function is non-blocking.  Notification of error or successful
 * transmission/reception is done asynchronously through callback functions.
 *
 * For transmit (XTE_SEND):
 *
 * It is assumed that the upper layer software supplies a correctly formatted
 * Ethernet frame, including the destination and source addresses, the
 * type/length field, and the data field.
 *
 * For receive (XTE_RECV):
 *
 * It is assumed that BDs have an appropriately sized frame buffer attached
 * that corresponds to the network MTU.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address (XTE_SEND or XTE_RECV).
 * @param NumBd is the number of BDs to commit. This is typically the same
 *        value used when the BDs were allocated with XTemac_SgAlloc().
 * @param BdPtr is the first BD in the set to commit and is typically the
 *        same value returned by XTemac_SgAlloc().
 *
 * @return
 * - XST_SUCCESS if the requested number of BDs was returned.
 * - XST_INVALID_PARAM if Direction did not specify a valid channel.
 * - XST_FAILURE if the last BD in the set does not have its "last" bit
 *   set (see XDmaBdV3_mSetLast()).
 * - XST_DMA_SG_LIST_ERROR if BdPtr parameter does not reflect the correct
 *   insertion point within the internally maintained BD ring. This error occurs
 *   when this function and XTemac_SgAlloc() are called out of order.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
int XTemac_SgCommit(XTemac *InstancePtr, u32 Direction,
		    unsigned NumBd, XDmaBdV3 * BdPtr)
{
	int Status;
	XDmaV3 *DmaPtr;
	u32 DgieReg;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Which channel to address */
	if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else {
		return (XST_INVALID_PARAM);
	}

	/* XDmaV3_SgToHw() will return either XST_SUCCESS, XST_FAILURE, or
	 * XST_DMA_SG_LIST_ERROR
	 *
	 * This is a critical section, prevent interrupts from the device while
	 * the BD ring is being modified.
	 */
	DgieReg = XTemac_mGetIpifReg(XTE_DGIE_OFFSET);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);
	Status = XDmaV3_SgBdToHw(DmaPtr, NumBd, BdPtr);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, DgieReg);
	return (Status);
}


/*****************************************************************************/
/**
 * Retrieve BDs that have been processed by the SGDMA channel. This function is
 * called typically after the XTE_HANDLER_SGRECV handler has been invoked for
 * the receive channel or XTE_HANDLER_SGSEND for the transmit channel.
 *
 * The set of BDs returned is a list starting with the BdPtr and extending
 * for 1 or more BDs (the exact number is the return value of this function).
 * The list can be navigated with macros XTemac_mSgRecvBdNext() for the
 * XTE_RECV channel, and XTemac_mSgSendBdNext() for the XTE_SEND channel.
 * Treat the returned BDs as read-only.
 *
 * This function and XTemac_SgFree() must be called in the correct order. See
 * xtemac.h for more information on the SGDMA use model.
 *
 * The last BD in the returned list is guaranteed to have the "Last" bit set
 * (i.e. XDmaBdV3_IsLast evaluates to true).
 *
 * The returned BDs can be examined for the outcome of the SGDMA transaction.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address (XTE_SEND or XTE_RECV).
 * @param BdPtr is an output parameter that points to the 1st BD in the returned
 *        list. If no BDs were ready, then this parameter is unchanged.
 * @param NumBd is an upper limit to the number of BDs to retrieve.
 *
 * @return
 * Number of BDs that are ready for post processing. If the direction parameter
 * is invalid, then 0 is returned.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
unsigned XTemac_SgGetProcessed(XTemac *InstancePtr, u32 Direction,
			       unsigned NumBd, XDmaBdV3 ** BdPtr)
{
	u32 DgieReg;
	XDmaV3 *DmaPtr;
	unsigned Rc;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Which channel to address */
	if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else {
		return (0);
	}

	/* This is a critical section. Prevent interrupts from the device while
	 * the BD ring is being modified.
	 */
	DgieReg = XTemac_mGetIpifReg(XTE_DGIE_OFFSET);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);

	/* Extract ready BDs */
	Rc = XDmaV3_SgBdFromHw(DmaPtr, NumBd, BdPtr);

	/* End critical section */
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, DgieReg);

	return (Rc);
}


/*****************************************************************************/
/**
 * Free a set of BDs that had been retrieved by XTemac_SgGetProcessed(). If BDs
 * are not freed, then eventually the channel will run out of BDs to
 * XTemac_SgAlloc().
 *
 * This function and XTemac_SgGetProcessed() must be called in the correct
 * order. See xtemac.h for more information on the SGDMA use model.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address (XTE_SEND or XTE_RECV).
 * @param BdPtr is the first BD in the set to free. This is typically the same
 *        value returned by XTemac_SgGetProcessed().
 * @param NumBd is the number of BDs to free. This is typically the same value
 *        returned by XTemac_SgGetProcessed().
 *
 * @return
 * - XST_SUCCESS if the requested number of BDs was returned.
 * - XST_INVALID_PARAM if Direction did not specify a valid channel.
 * - XST_DMA_SG_LIST_ERROR if BdPtr parameter does not reflect the correct
 *   insertion point within the internally maintained BD ring. This error occurs
 *   when this function and XTemac_SgGetProcessed() are called out of order.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
int XTemac_SgFree(XTemac *InstancePtr, u32 Direction,
		  unsigned NumBd, XDmaBdV3 * BdPtr)
{
	u32 DgieReg;
	XDmaV3 *DmaPtr;
	int Status;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Which channel to address */
	if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else {
		return (XST_INVALID_PARAM);
	}

	/* This is a critical section. Prevent interrupts from the device while
	 * the BD ring is being modified.
	 */
	DgieReg = XTemac_mGetIpifReg(XTE_DGIE_OFFSET);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, 0);
	Status = XDmaV3_SgBdFree(DmaPtr, NumBd, BdPtr);
	XTemac_mSetIpifReg(XTE_DGIE_OFFSET, DgieReg);

	return (Status);
}


/*****************************************************************************/
/**
 * Give the driver memory space to be used for the scatter-gather DMA
 * descriptor list. This function should only be called once for each channel
 * during initialization. If a list had already been created, then it is
 * destroyed and replaced with a new one.
 *
 * To increase performance, a BdTemplate parameter is provided to allow the
 * user to permanently set BD fields in all BDs for this SGDMA channel. For
 * example, if every BD describes a buffer that will contain a full packet (as
 * it typically does with receive channels), then XDmaBdV3_mSetLast(BdTemplate)
 * can be performed prior to calling this function and when it returns every BD
 * will have the "last" bit set in it's DMACR word. The user will never have to
 * explicitly set the "last" bit again.
 *
 * The following operations can be replicated for the BdTemplate:
 *   - XDmaBdV3_mSetId()
 *   - XDmaBdV3_mSetLast()
 *   - XDmaBdV3_mClearLast()
 *   - XDmaBdV3_mSetBufIncrement()
 *   - XDmaBdV3_mSetBufNoIncrement()
 *   - XDmaBdV3_mSetDevSel()
 *   - XDmaBdV3_mSetBdPage()
 *   - XDmaBdV3_mSetTransferType()
 *   - XDmaBdV3_mSetBufAddrHigh()
 *   - XDmaBdV3_mSetBufAddrLow()
 *   - XTemac_mSgSendBdCsumEnable()  -- transmit channel only
 *   - XTemac_mSgSendBdCsumDisable() -- transmit channel only
 *   - XTemac_mSgSendBdCsumSetup()   -- transmit channel only
 *   - XTemac_mSgSendBdCsumSeed()    -- transmit channel only
 *
 * The base address of the memory space must be aligned according to buffer
 * descriptor requirements (see xtemac.h).
 *
 * The size of the memory space is assumed to be big enough to contain BdCount
 * buffers at the given alignment. If the region is too small, then adjacent
 * data may be overwritten causing system instability. There are tools in the
 * DMA driver that help calculate the sizing requirments. See macros
 * XDmaV3_mSgListCntCalc() and XDmaV3_mSgListMemCalc().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to address.
 * @param PhysicalAddr is the physical base address of user memory region.
 * @param VirtualAddr is the virtual base address of the user memory region. If
 *        address translation is not being utilized, then VirtAddr should be
 *        equivalent to PhysAddr.
 * @param Alignment governs the byte alignment of individual BDs. This function
 *        will enforce a minimum alignment of 8 bytes with no maximum as long as
 *        it is specified as a power of 2.
 * @param BdCount is the number of BDs to allocate in the memory region. It is
 *        assumed the region is large enough to contain all the BDs.
 * @param BdTemplate is copied to each BD after the list is created. If the user
 *        does not have a need to replicate any BD fields then this parameter
 *        should be zeroed (XDmaBdV3_mClear()). This parameter will be modified
 *        by this function.
 *
 * @return
 * - XST_SUCCESS if the space was initialized successfully
 * - XST_DEVICE_IS_STARTED if the device has not been stopped.
 * - XST_NOT_SGDMA if the MAC is not configured for scatter-gather DMA per
 *   the configuration information contained in XTemac_Config.
 * - XST_INVALID_PARAM if: 1) Direction is not either XTE_SEND or XTE_RECV;
 *   2) PhysicalAddr and/or VirtualAddr are not aligned to the given
 *   alignment parameter; 3) Alignment parameter does not meet minimum
 *   requirements of this device; 3) BdCount is 0.
 * - XST_DMA_SG_LIST_ERROR if the memory segment containing the list spans
 *   over address 0x00000000 in virtual address space.
 * - XST_NO_FEATURE if the DMA sub-driver discovers that the HW is not SGDMA
 *   capable.
 * - XST_FAILURE for other failures that shouldn't occur. If this is returned,
 *   then the driver is experiencing a problem that should be reported to
 *   Xilinx.
 *
 * @note
 * If the device is configured for scatter-gather DMA, this function must be
 * called AFTER the XTemac_Initialize() function because the DMA channel
 * components must be initialized before the memory space is set.
 *
 ******************************************************************************/
int XTemac_SgSetSpace(XTemac *InstancePtr, u32 Direction,
		      u32 PhysicalAddr, u32 VirtualAddr,
		      u32 Alignment, unsigned BdCount, XDmaBdV3 * BdTemplate)
{
	int Status;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BdTemplate != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Make sure device is ready for this operation */
	if (InstancePtr->IsStarted == XCOMPONENT_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Must have sgdma */
	if (!XTemac_mIsSgDma(InstancePtr)) {
		return (XST_NOT_SGDMA);
	}

	/* Check alignment */
	if (Alignment < XTE_PLB_BD_ALIGNMENT) {
		return (XST_INVALID_PARAM);
	}

	if (Direction == XTE_SEND) {
		/* Create the list. This function will return one of XST_SUCCESS,
		 * XST_INVALID_PARAM (for alignment violations), or
		 * XST_DMA_SG_LIST_ERROR (if memory segment spans address 0)
		 */
		Status = XDmaV3_SgListCreate(&InstancePtr->SendDma,
					     PhysicalAddr, VirtualAddr,
					     Alignment, BdCount);
		if (Status != XST_SUCCESS) {
			return (Status);
		}

		/* Clone the template BD. This should always work. If it does not
		 * then something is seriously wrong
		 */
		Status = XDmaV3_SgListClone(&InstancePtr->SendDma, BdTemplate);
		if (Status != XST_SUCCESS) {
			return (XST_FAILURE);
		}
		else {
			return (XST_SUCCESS);
		}
	}
	else if (Direction == XTE_RECV) {
		/* Create the list. This function will return one of XST_SUCCESS,
		 * XST_INVALID_PARAM (for alignment violations), or
		 * XST_DMA_SG_LIST_ERROR (if memory segment spans address 0)
		 */
		Status = XDmaV3_SgListCreate(&InstancePtr->RecvDma,
					     PhysicalAddr, VirtualAddr,
					     Alignment, BdCount);
		if (Status != XST_SUCCESS) {
			return (Status);
		}

		/* Clone the template BD */
		Status = XDmaV3_SgListClone(&InstancePtr->RecvDma, BdTemplate);
		if (Status != XST_SUCCESS) {
			return (XST_FAILURE);
		}
		else {
			return (XST_SUCCESS);
		}
	}

	/* Direction is incorrect */
	return (XST_INVALID_PARAM);
}


/*****************************************************************************/
/**
 * Verify the consistency of the SGDMA BD ring. While the check occurs, the
 * device is stopped. If any problems are found the device is left stopped.
 *
 * Use this function to troubleshoot SGDMA problems.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction is the channel to check (XTE_SEND or XTE_RECV)
 *
 * @return
 * - XST_SUCCESS if no problems are found.
 * - XST_INVALID_PARAM if Direction is not XTE_SEND or XTE_RECV.
 * - XST_DMA_SG_NO_LIST if the SG list has not yet been setup.
 * - XST_DMA_BD_ERROR if a BD has been corrupted.
 * - XST_DMA_SG_LIST_ERROR if the internal data structures of the BD ring are
 *   inconsistent.
 *
 * @note
 * This function is not thread-safe. The user must provide mutually exclusive
 * access to this function if there are to be multiple threads that can call it.
 *
 ******************************************************************************/
int XTemac_SgCheck(XTemac *InstancePtr, u32 Direction)
{
	XDmaV3 *DmaPtr;
	XDmaBdV3 *BdPtr;
	unsigned i;
	int Restart = 0;
	int Rc;

	XASSERT_NONVOID(InstancePtr != NULL);

	/* Select channel to check */
	if (Direction == XTE_SEND) {
		DmaPtr = &InstancePtr->SendDma;
	}
	else if (Direction == XTE_RECV) {
		DmaPtr = &InstancePtr->RecvDma;
	}
	else {
		return (XST_INVALID_PARAM);
	}

	/* Stop the device if it is running */
	if (InstancePtr->IsStarted == XST_DEVICE_IS_STARTED) {
		XTemac_Stop(InstancePtr);
		Restart = 1;
	}

	/* Perform check of ring structure using DMA driver routine */
	Rc = XDmaV3_SgCheck(DmaPtr);

	/* Check BDs for consistency as used by TEMAC */
	if (Rc == XST_SUCCESS) {
		/* Verify DMACR is setup for Tx direction */
		if (Direction == XTE_SEND) {
			BdPtr = (XDmaBdV3 *) DmaPtr->BdRing.BaseAddr;
			for (i = 0; i < DmaPtr->BdRing.AllCnt; i++) {
				if (XDmaV3_mReadBd(BdPtr, XDMAV3_DMACR_OFFSET) &
				    XDMAV3_DMACR_DIR_RX_MASK) {
					return (XST_DMA_BD_ERROR);
				}
			}
		}
		else {		/* XTE_RECV */

			BdPtr = (XDmaBdV3 *) DmaPtr->BdRing.BaseAddr;
			for (i = 0; i < DmaPtr->BdRing.AllCnt; i++) {
				if (!
				    (XDmaV3_mReadBd(BdPtr, XDMAV3_DMACR_OFFSET)
				     & XDMAV3_DMACR_DIR_RX_MASK)) {
					return (XST_DMA_BD_ERROR);
				}
			}
		}
	}

	/* Restart the device if it was stopped by this function */
	if ((Rc == XST_SUCCESS) && Restart) {
		XTemac_Start(InstancePtr);
	}

	return (Rc);
}
