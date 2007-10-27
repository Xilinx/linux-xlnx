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
*       (c) Copyright 2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xlldma_bdring.c
*
* This file implements buffer descriptor ring related functions. For more
* information on this driver, see xlldma.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a xd   12/21/06 First release
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>

#include "xlldma.h"
#include "xenv.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/******************************************************************************
 * Define methods to flush and invalidate cache for BDs should they be
 * located in cached memory. These macros may NOPs if the underlying
 * XCACHE_FLUSH_DCACHE_RANGE and XCACHE_INVALIDATE_DCACHE_RANGE macros are not
 * implemented or they do nothing.
 *****************************************************************************/
#ifdef XCACHE_FLUSH_DCACHE_RANGE
#  define XLLDMA_CACHE_FLUSH(BdPtr)               \
	XCACHE_FLUSH_DCACHE_RANGE((BdPtr), XLLDMA_BD_HW_NUM_BYTES)
#else
#  define XLLDMA_CACHE_FLUSH(BdPtr)
#endif

#ifdef XCACHE_INVALIDATE_DCACHE_RANGE
#  define XLLDMA_CACHE_INVALIDATE(BdPtr)          \
	XCACHE_INVALIDATE_DCACHE_RANGE((BdPtr), XLLDMA_BD_HW_NUM_BYTES)
#else
#  define XLLDMA_CACHE_INVALIDATE(BdPtr)
#endif

/******************************************************************************
 * Compute the virtual address of a descriptor from its physical address
 *
 * @param BdPtr is the physical address of the BD
 *
 * @returns Virtual address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 * @note RingPtr is an implicit parameter
 *****************************************************************************/
#define XLLDMA_PHYS_TO_VIRT(BdPtr) \
	((u32)(BdPtr) + (RingPtr->FirstBdAddr - RingPtr->FirstBdPhysAddr))

/******************************************************************************
 * Compute the physical address of a descriptor from its virtual address
 *
 * @param BdPtr is the virtual address of the BD
 *
 * @returns Physical address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 * @note RingPtr is an implicit parameter
 *****************************************************************************/
#define XLLDMA_VIRT_TO_PHYS(BdPtr) \
	((u32)(BdPtr) - (RingPtr->FirstBdAddr - RingPtr->FirstBdPhysAddr))

/******************************************************************************
 * Move the BdPtr argument ahead an arbitrary number of BDs wrapping around
 * to the beginning of the ring if needed.
 *
 * We know if a wraparound should occur if the new BdPtr is greater than
 * the high address in the ring OR if the new BdPtr crosses the 0xFFFFFFFF
 * to 0 boundary.
 *
 * @param RingPtr is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 *****************************************************************************/
#define XLLDMA_RING_SEEKAHEAD(RingPtr, BdPtr, NumBd)			    \
	{								    \
		u32 Addr = (u32)(BdPtr);				    \
									    \
		Addr += ((RingPtr)->Separation * (NumBd));		    \
		if ((Addr > (RingPtr)->LastBdAddr) || ((u32)(BdPtr) > Addr))\
		{							    \
			Addr -= (RingPtr)->Length;			    \
		}							    \
									    \
		(BdPtr) = (XLlDma_Bd*)Addr;				    \
	}

/******************************************************************************
 * Move the BdPtr argument backwards an arbitrary number of BDs wrapping
 * around to the end of the ring if needed.
 *
 * We know if a wraparound should occur if the new BdPtr is less than
 * the base address in the ring OR if the new BdPtr crosses the 0xFFFFFFFF
 * to 0 boundary.
 *
 * @param RingPtr is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 *****************************************************************************/
#define XLLDMA_RING_SEEKBACK(RingPtr, BdPtr, NumBd)			      \
	{                                                                     \
		u32 Addr = (u32)(BdPtr);				      \
									      \
		Addr -= ((RingPtr)->Separation * (NumBd));		      \
		if ((Addr < (RingPtr)->FirstBdAddr) || ((u32)(BdPtr) < Addr)) \
		{							      \
			Addr += (RingPtr)->Length;			      \
		}							      \
									      \
		(BdPtr) = (XLlDma_Bd*)Addr;				      \
	}


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
 * Using a memory segment allocated by the caller, create and setup the BD list
 * for the given SGDMA ring.
 *
 * @param InstancePtr is the instance to be worked on.
 * @param PhysAddr is the physical base address of application memory region.
 * @param VirtAddr is the virtual base address of the application memory
 *        region.If address translation is not being utilized, then VirtAddr
 *        should be equivalent to PhysAddr.
 * @param Alignment governs the byte alignment of individual BDs. This function
 *        will enforce a minimum alignment of XLLDMA_BD_MINIMUM_ALIGNMENT bytes
 *        with no maximum as long as it is specified as a power of 2.
 * @param BdCount is the number of BDs to setup in the application memory
 *        region. It is assumed the region is large enough to contain the BDs.
 *        Refer to the "SGDMA Ring Creation" section  in xlldma.h for more
 *        information. The minimum valid value for this parameter is 1.
 *
 * @return
 *
 * - XST_SUCCESS if initialization was successful
 * - XST_NO_FEATURE if the provided instance is a non SGDMA type of DMA
 *   channel.
 * - XST_INVALID_PARAM under any of the following conditions: 1) PhysAddr
 *   and/or VirtAddr are not aligned to the given Alignment parameter;
 *   2) Alignment parameter does not meet minimum requirements or is not a
 *   power of 2 value; 3) BdCount is 0.
 * - XST_DMA_SG_LIST_ERROR if the memory segment containing the list spans
 *   over address 0x00000000 in virtual address space.
 *
 *****************************************************************************/
int XLlDma_BdRingCreate(XLlDma_BdRing * RingPtr, u32 PhysAddr,
			u32 VirtAddr, u32 Alignment, unsigned BdCount)
{
	unsigned i;
	u32 BdVirtAddr;
	u32 BdPhysAddr;

	/* In case there is a failure prior to creating list, make sure the
	 * following attributes are 0 to prevent calls to other SG functions
	 * from doing anything
	 */
	RingPtr->AllCnt = 0;
	RingPtr->FreeCnt = 0;
	RingPtr->HwCnt = 0;
	RingPtr->PreCnt = 0;
	RingPtr->PostCnt = 0;

	/* Make sure Alignment parameter meets minimum requirements */
	if (Alignment < XLLDMA_BD_MINIMUM_ALIGNMENT) {
		return (XST_INVALID_PARAM);
	}

	/* Make sure Alignment is a power of 2 */
	if ((Alignment - 1) & Alignment) {
		return (XST_INVALID_PARAM);
	}

	/* Make sure PhysAddr and VirtAddr are on same Alignment */
	if ((PhysAddr % Alignment) || (VirtAddr % Alignment)) {
		return (XST_INVALID_PARAM);
	}

	/* Is BdCount reasonable? */
	if (BdCount == 0) {
		return (XST_INVALID_PARAM);
	}

	/* Compute how many bytes will be between the start of adjacent BDs */
	RingPtr->Separation =
		(sizeof(XLlDma_Bd) + (Alignment - 1)) & ~(Alignment - 1);

	/* Must make sure the ring doesn't span address 0x00000000. If it does,
	 * then the next/prev BD traversal macros will fail.
	 */
	if (VirtAddr > (VirtAddr + (RingPtr->Separation * BdCount) - 1)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Initial ring setup:
	 *  - Clear the entire space
	 *  - Setup each BD's next pointer with the physical address of the
	 *    next BD
	 *  - Set each BD's DMA complete status bit
	 */
	memset((void *) VirtAddr, 0, (RingPtr->Separation * BdCount));

	BdVirtAddr = VirtAddr;
	BdPhysAddr = PhysAddr + RingPtr->Separation;
	for (i = 1; i < BdCount; i++) {
		XLlDma_mBdWrite(BdVirtAddr, XLLDMA_BD_NDESC_OFFSET, BdPhysAddr);
		XLlDma_mBdWrite(BdVirtAddr, XLLDMA_BD_STSCTRL_USR0_OFFSET,
				XLLDMA_BD_STSCTRL_COMPLETED_MASK);
		XLLDMA_CACHE_FLUSH(BdVirtAddr);
		BdVirtAddr += RingPtr->Separation;
		BdPhysAddr += RingPtr->Separation;
	}

	/* At the end of the ring, link the last BD back to the top */
	XLlDma_mBdWrite(BdVirtAddr, XLLDMA_BD_NDESC_OFFSET, PhysAddr);

	/* Setup and initialize pointers and counters */
	RingPtr->RunState = XST_DMA_SG_IS_STOPPED;
	RingPtr->FirstBdAddr = VirtAddr;
	RingPtr->FirstBdPhysAddr = PhysAddr;
	RingPtr->LastBdAddr = BdVirtAddr;
	RingPtr->Length = RingPtr->LastBdAddr - RingPtr->FirstBdAddr +
		RingPtr->Separation;
	RingPtr->AllCnt = BdCount;
	RingPtr->FreeCnt = BdCount;
	RingPtr->FreeHead = (XLlDma_Bd *) VirtAddr;
	RingPtr->PreHead = (XLlDma_Bd *) VirtAddr;
	RingPtr->HwHead = (XLlDma_Bd *) VirtAddr;
	RingPtr->HwTail = (XLlDma_Bd *) VirtAddr;
	RingPtr->PostHead = (XLlDma_Bd *) VirtAddr;
	RingPtr->BdaRestart = (XLlDma_Bd *) PhysAddr;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Clone the given BD into every BD in the ring. Except for
 * XLLDMA_BD_NDESC_OFFSET, every field of the source BD is replicated in every
 * BD in the ring.
 *
 * This function can be called only when all BDs are in the free group such as
 * they are immediately after creation of the ring. This prevents modification
 * of BDs while they are in use by hardware or the application.
 *
 * @param InstancePtr is the instance to be worked on.
 * @param SrcBdPtr is the source BD template to be cloned into the list.
 *
 * @return
 *   - XST_SUCCESS if the list was modified.
 *   - XST_DMA_SG_NO_LIST if a list has not been created.
 *   - XST_DMA_SG_LIST_ERROR if some of the BDs in this channel are under
 *     hardware or application control.
 *   - XST_DEVICE_IS_STARTED if the DMA channel has not been stopped.
 *
 *****************************************************************************/
int XLlDma_BdRingClone(XLlDma_BdRing * RingPtr, XLlDma_Bd * SrcBdPtr)
{
	unsigned i;
	u32 CurBd;
	u32 Save;
	XLlDma_Bd TmpBd;

	/* Can't do this function if there isn't a ring */
	if (RingPtr->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Can't do this function with the channel running */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Can't do this function with some of the BDs in use */
	if (RingPtr->FreeCnt != RingPtr->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}


	/* Make a copy of the template then modify it by setting complete bit
	 * in status/control field
	 */
	memcpy(&TmpBd, SrcBdPtr, sizeof(XLlDma_Bd));
	Save = XLlDma_mBdRead(&TmpBd, XLLDMA_BD_STSCTRL_USR0_OFFSET);
	Save |= XLLDMA_BD_STSCTRL_COMPLETED_MASK;
	XLlDma_mBdWrite(&TmpBd, XLLDMA_BD_STSCTRL_USR0_OFFSET, Save);

	/* Starting from the top of the ring, save BD.Next, overwrite the
	 * entire BD with the template, then restore BD.Next
	 */
	for (i = 0, CurBd = RingPtr->FirstBdAddr;
	     i < RingPtr->AllCnt; i++, CurBd += RingPtr->Separation) {
		Save = XLlDma_mBdRead(CurBd, XLLDMA_BD_NDESC_OFFSET);
		memcpy((void *) CurBd, (void *) &TmpBd, sizeof(XLlDma_Bd));
		XLlDma_mBdWrite(CurBd, XLLDMA_BD_NDESC_OFFSET, Save);
		XLLDMA_CACHE_FLUSH(CurBd);
	}

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Allow DMA transactions to commence on the given channels if descriptors are
 * ready to be processed.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 *
 * @return
 * - XST_SUCCESS if the channel) were started.
 * - XST_DMA_SG_NO_LIST if the channel) have no initialized BD ring.
 *
 *****************************************************************************/
int XLlDma_BdRingStart(XLlDma_BdRing * RingPtr)
{
	/* BD list has yet to be created for this channel */
	if (RingPtr->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Do nothing if already started */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_SUCCESS);
	}

	/* Sync hardware and driver with the last unprocessed BD or the 1st BD
	 * in the ring if this is the first time starting the channel
	 */
	XLlDma_mWriteReg(RingPtr->ChanBase, XLLDMA_CDESC_OFFSET,
			 (u32) RingPtr->BdaRestart);

	/* Note as started */
	RingPtr->RunState = XST_DMA_SG_IS_STARTED;

	/* If there are unprocessed BDs then we want to channel to begin
	 * processing right away
	 */
	if (RingPtr->HwCnt > 0) {
		XLLDMA_CACHE_INVALIDATE(RingPtr->HwTail);

		if ((XLlDma_mBdRead(RingPtr->HwTail,
				    XLLDMA_BD_STSCTRL_USR0_OFFSET) &
		     XLLDMA_BD_STSCTRL_COMPLETED_MASK) == 0) {
			XLlDma_mWriteReg(RingPtr->ChanBase,
					 XLLDMA_TDESC_OFFSET,
					 XLLDMA_VIRT_TO_PHYS(RingPtr->HwTail));
		}
	}

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Set interrupt coalescing parameters for the given descriptor ring channel.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param Counter sets the packet counter on the channel. Valid range is
 *        1..255, or XLLDMA_NO_CHANGE to leave this setting unchanged.
 * @param Timer sets the waitbound timer on the channel. Valid range is
 *        1..255, or XLLDMA_NO_CHANGE to leave this setting unchanged. LSB is
 *        in units of 1 / (local link clock).
 *
 * @return
 *        - XST_SUCCESS if interrupt coalescing settings updated
 *        - XST_FAILURE if Counter or Timer parameters are out of range
 *****************************************************************************/
int XLlDma_BdRingSetCoalesce(XLlDma_BdRing * RingPtr, u32 Counter, u32 Timer)
{
	u32 Cr = XLlDma_mReadReg(RingPtr->ChanBase, XLLDMA_CR_OFFSET);

	if (Counter != XLLDMA_NO_CHANGE) {
		if ((Counter == 0) || (Counter > 0xFF)) {
			return (XST_FAILURE);
		}

		Cr = (Cr & ~XLLDMA_CR_IRQ_COUNT_MASK) |
			(Counter << XLLDMA_CR_IRQ_COUNT_SHIFT);
		Cr |= XLLDMA_CR_LD_IRQ_CNT_MASK;
	}

	if (Timer != XLLDMA_NO_CHANGE) {
		if ((Timer == 0) || (Timer > 0xFF)) {
			return (XST_FAILURE);
		}

		Cr = (Cr & ~XLLDMA_CR_IRQ_TIMEOUT_MASK) |
			(Timer << XLLDMA_CR_IRQ_TIMEOUT_SHIFT);
		Cr |= XLLDMA_CR_LD_IRQ_CNT_MASK;
	}

	XLlDma_mWriteReg(RingPtr->ChanBase, XLLDMA_CR_OFFSET, Cr);
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Retrieve current interrupt coalescing parameters from the given descriptor
 * ring channel.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param CounterPtr points to a memory location where the current packet
 *        counter will be written.
 * @param TimerPtr points to a memory location where the current waitbound
 *        timer will be written.
 *****************************************************************************/
void XLlDma_BdRingGetCoalesce(XLlDma_BdRing * RingPtr,
			      u32 *CounterPtr, u32 *TimerPtr)
{
	u32 Cr = XLlDma_mReadReg(RingPtr->ChanBase, XLLDMA_CR_OFFSET);

	*CounterPtr =
		((Cr & XLLDMA_CR_IRQ_COUNT_MASK) >> XLLDMA_CR_IRQ_COUNT_SHIFT);
	*TimerPtr =
		((Cr & XLLDMA_CR_IRQ_TIMEOUT_MASK) >>
		 XLLDMA_CR_IRQ_TIMEOUT_SHIFT);
}


/*****************************************************************************/
/**
 * Reserve locations in the BD ring. The set of returned BDs may be modified in
 * preparation for future DMA transactions). Once the BDs are ready to be
 * submitted to hardware, the application must call XLlDma_BdRingToHw() in the
 * same order which they were allocated here. Example:
 *
 * <pre>
 *        NumBd = 2;
 *        Status = XDsma_RingBdAlloc(MyRingPtr, NumBd, &MyBdSet);
 *
 *        if (Status != XST_SUCCESS)
 *        {
 *            // Not enough BDs available for the request
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *            // Prepare CurBd.....
 *
 *            // Onto next BD
 *            CurBd = XLlDma_mBdRingNext(MyRingPtr, CurBd);
 *        }
 *
 *        // Give list to hardware
 *        Status = XLlDma_BdRingToHw(MyRingPtr, NumBd, MyBdSet);
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be allocated and given to hardware in the correct sequence:
 * <pre>
 *        // Legal
 *        XLlDma_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XLlDma_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingAlloc(MyRingPtr, NumBd2, &MySet2);
 *        XLlDma_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 *        XLlDma_BdRingToHw(MyRingPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XLlDma_BdRingAlloc(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingAlloc(MyRingPtr, NumBd2, &MySet2);
 *        XLlDma_BdRingToHw(MyRingPtr, NumBd2, MySet2);
 *        XLlDma_BdRingToHw(MyRingPtr, NumBd1, MySet1);
 * </pre>
 *
 * Use the API defined in xlldmabd.h to modify individual BDs. Traversal of the
 * BD set can be done using XLlDma_mBdRingNext() and XLlDma_mBdRingPrev().
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param NumBd is the number of BDs to allocate
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for modification.
 *
 * @return
 *   - XST_SUCCESS if the requested number of BDs was returned in the BdSetPtr
 *     parameter.
 *   - XST_FAILURE if there were not enough free BDs to satisfy the request.
 *
 * @note This function should not be preempted by another XLlDma_BdRing
 *       function call that modifies the BD space. It is the caller's
 *       responsibility to provide a mutual exclusion mechanism.
 *
 * @note Do not modify more BDs than the number requested with the NumBd
 *       parameter. Doing so will lead to data corruption and system
 *       instability.
 *
 *****************************************************************************/
int XLlDma_BdRingAlloc(XLlDma_BdRing * RingPtr, unsigned NumBd,
		       XLlDma_Bd ** BdSetPtr)
{
	/* Enough free BDs available for the request? */
	if (RingPtr->FreeCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead forward */
	*BdSetPtr = RingPtr->FreeHead;
	XLLDMA_RING_SEEKAHEAD(RingPtr, RingPtr->FreeHead, NumBd);
	RingPtr->FreeCnt -= NumBd;
	RingPtr->PreCnt += NumBd;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Fully or partially undo an XLlDma_BdRingAlloc() operation. Use this function
 * if all the BDs allocated by XLlDma_BdRingAlloc() could not be transferred to
 * hardware with XLlDma_BdRingToHw().
 *
 * This function helps out in situations when an unrelated error occurs after
 * BDs have been allocated but before they have been given to hardware.
 *
 * This function is not the same as XLlDma_BdRingFree(). The Free function
 * returns BDs to the free list after they have been processed by hardware,
 * while UnAlloc returns them before being processed by hardware.
 *
 * There are two scenarios where this function can be used. Full UnAlloc or
 * Partial UnAlloc. A Full UnAlloc means all the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XLlDma_BdRingAlloc(MyRingPtr, 10, &BdPtr);
 *        .
 *        .
 *    if (Error)
 *    {
 *        Status = XLlDma_BdRingUnAlloc(MyRingPtr, 10, &BdPtr);
 *    }
 * </pre>
 *
 * A partial UnAlloc means some of the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XLlDma_BdRingAlloc(MyRingPtr, 10, &BdPtr);
 *    BdsLeft = 10;
 *    CurBdPtr = BdPtr;
 *
 *    while (BdsLeft)
 *    {
 *       if (Error)
 *       {
 *          Status = XLlDma_BdRingUnAlloc(MyRingPtr, BdsLeft, CurBdPtr);
 *       }
 *
 *       CurBdPtr = XLlDma_mBdRingNext(MyRingPtr, CurBdPtr);
 *       BdsLeft--;
 *    }
 * </pre>
 *
 * A partial UnAlloc must include the last BD in the list that was Alloc'd.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param NumBd is the number of BDs to unallocate
 * @param BdSetPtr points to the first of the BDs to be returned.
 *
 * @return
 *   - XST_SUCCESS if the BDs were unallocated.
 *   - XST_FAILURE if NumBd parameter was greater that the number of BDs in the
 *     preprocessing state.
 *
 * @note This function should not be preempted by another XLlDma ring function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XLlDma_BdRingUnAlloc(XLlDma_BdRing * RingPtr, unsigned NumBd,
			 XLlDma_Bd * BdSetPtr)
{
	/* Enough BDs in the free state for the request? */
	if (RingPtr->PreCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead backward */
	XLLDMA_RING_SEEKBACK(RingPtr, RingPtr->FreeHead, NumBd);
	RingPtr->FreeCnt += NumBd;
	RingPtr->PreCnt -= NumBd;

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Enqueue a set of BDs to hardware that were previously allocated by
 * XLlDma_BdRingAlloc(). Once this function returns, the argument BD set goes
 * under hardware control. Any changes made to these BDs after this point will
 * corrupt the BD list leading to data corruption and system instability.
 *
 * The set will be rejected if the last BD of the set does not mark the end of
 * a packet.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param NumBd is the number of BDs in the set.
 * @param BdSetPtr is the first BD of the set to commit to hardware.
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was accepted and enqueued to hardware
 *   - XST_FAILURE if the set of BDs was rejected because the first BD
 *     did not have its start-of-packet bit set, the last BD did not have
 *     its end-of-packet bit set, or any one of the BD set has 0 as length
 *     value
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XLlDma_BdRingAlloc()
 *
 * @note This function should not be preempted by another XLlDma ring function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XLlDma_BdRingToHw(XLlDma_BdRing * RingPtr, unsigned NumBd,
		      XLlDma_Bd * BdSetPtr)
{
	XLlDma_Bd *CurBdPtr;
	unsigned i;
	u32 BdStsCr;

	/* If the commit set is empty, do nothing */
	if (NumBd == 0) {
		return (XST_SUCCESS);
	}

	/* Make sure we are in sync with XLlDma_BdRingAlloc() */
	if ((RingPtr->PreCnt < NumBd) || (RingPtr->PreHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	CurBdPtr = BdSetPtr;
	BdStsCr = XLlDma_mBdRead(CurBdPtr, XLLDMA_BD_STSCTRL_USR0_OFFSET);

	/* The first BD should have been marked as start-of-packet */
	if (!(BdStsCr & XLLDMA_BD_STSCTRL_SOP_MASK)) {
		return (XST_FAILURE);
	}

	/* For each BD being submitted except the last one, clear the completed
	 * bit and stop_on_end bit in the status word
	 */
	for (i = 0; i < NumBd - 1; i++) {

		/* Make sure the length value in the BD is non-zero. */
		if (XLlDma_mBdGetLength(CurBdPtr) == 0) {
			return (XST_FAILURE);
		}

		BdStsCr &=
			~(XLLDMA_BD_STSCTRL_COMPLETED_MASK |
			  XLLDMA_BD_STSCTRL_SOE_MASK);
		XLlDma_mBdWrite(CurBdPtr, XLLDMA_BD_STSCTRL_USR0_OFFSET,
				BdStsCr);

		/* In RX channel case, the current BD should have the
		 * XLLDMA_USERIP_APPWORD_OFFSET initialized to
		 * XLLDMA_USERIP_APPWORD_INITVALUE
		 */
		if (RingPtr->IsRxChannel) {
			XLlDma_mBdWrite(CurBdPtr, XLLDMA_USERIP_APPWORD_OFFSET,
					XLLDMA_USERIP_APPWORD_INITVALUE);
		}

		/* Flush the current BD so DMA core could see the updates */
		XLLDMA_CACHE_FLUSH(CurBdPtr);

		CurBdPtr = XLlDma_mBdRingNext(RingPtr, CurBdPtr);
		BdStsCr =
			XLlDma_mBdRead(CurBdPtr, XLLDMA_BD_STSCTRL_USR0_OFFSET);
	}

	/* The last BD should have end-of-packet bit set */
	if (!(BdStsCr & XLLDMA_BD_STSCTRL_EOP_MASK)) {
		return (XST_FAILURE);
	}

	/* Make sure the length value in the last BD is non-zero. */
	if (XLlDma_mBdGetLength(CurBdPtr) == 0) {
		return (XST_FAILURE);
	}

	/* The last BD should also have the completed and stop-on-end bits
	 * cleared
	 */
	BdStsCr &=
		~(XLLDMA_BD_STSCTRL_COMPLETED_MASK |
		  XLLDMA_BD_STSCTRL_SOE_MASK);
	XLlDma_mBdWrite(CurBdPtr, XLLDMA_BD_STSCTRL_USR0_OFFSET, BdStsCr);

	/* In RX channel case, the last BD should have the
	 * XLLDMA_USERIP_APPWORD_OFFSET initialized to
	 * XLLDMA_USERIP_APPWORD_INITVALUE
	 */
	if (RingPtr->IsRxChannel) {
		XLlDma_mBdWrite(CurBdPtr, XLLDMA_USERIP_APPWORD_OFFSET,
				XLLDMA_USERIP_APPWORD_INITVALUE);
	}

	/* Flush the last BD so DMA core could see the updates */
	XLLDMA_CACHE_FLUSH(CurBdPtr);

	/* This set has completed pre-processing, adjust ring pointers and
	 * counters
	 */
	XLLDMA_RING_SEEKAHEAD(RingPtr, RingPtr->PreHead, NumBd);
	RingPtr->PreCnt -= NumBd;
	RingPtr->HwTail = CurBdPtr;
	RingPtr->HwCnt += NumBd;

	/* If it was enabled, tell the engine to begin processing */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		XLlDma_mWriteReg(RingPtr->ChanBase, XLLDMA_TDESC_OFFSET,
				 XLLDMA_VIRT_TO_PHYS(RingPtr->HwTail));
	}
	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Returns a set of BD(s) that have been processed by hardware. The returned
 * BDs may be examined by the application to determine the outcome of the DMA
 * transactions). Once the BDs have been examined, the application must call
 * XLlDma_BdRingFree() in the same order which they were retrieved here.
 *
 * Example:
 *
 * <pre>
 *        NumBd = XLlDma_BdRingFromHw(MyRingPtr, XLLDMA_ALL_BDS, &MyBdSet);
 *
 *        if (NumBd == 0)
 *        {
 *           // hardware has nothing ready for us yet
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *           // Examine CurBd for post processing.....
 *
 *           // Onto next BD
 *           CurBd = XLlDma_mBdRingNext(MyRingPtr, CurBd);
 *        }
 *
 *        XLlDma_BdRingFree(MyRingPtr, NumBd, MyBdSet); // Return the list
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be retrieved from hardware and freed in the correct sequence:
 * <pre>
 *        // Legal
 *        XLlDma_BdRingFromHw(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XLlDma_BdRingFromHw(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingFromHw(MyRingPtr, NumBd2, &MySet2);
 *        XLlDma_BdRingFree(MyRingPtr, NumBd1, MySet1);
 *        XLlDma_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XLlDma_BdRingFromHw(MyRingPtr, NumBd1, &MySet1);
 *        XLlDma_BdRingFromHw(MyRingPtr, NumBd2, &MySet2);
 *        XLlDma_BdRingFree(MyRingPtr, NumBd2, MySet2);
 *        XLlDma_BdRingFree(MyRingPtr, NumBd1, MySet1);
 * </pre>
 *
 * If hardware has partially completed a packet spanning multiple BDs, then
 * none of the BDs for that packet will be included in the results.
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param BdLimit is the maximum number of BDs to return in the set. Use
 *        XLLDMA_ALL_BDS to return all BDs that have been processed.
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for examination.
 *
 * @return
 *   The number of BDs processed by hardware. A value of 0 indicates that no
 *   data is available. No more than BdLimit BDs will be returned.
 *
 * @note Treat BDs returned by this function as read-only.
 *
 * @note This function should not be preempted by another XLlDma ring function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
unsigned XLlDma_BdRingFromHw(XLlDma_BdRing * RingPtr, unsigned BdLimit,
			     XLlDma_Bd ** BdSetPtr)
{
	XLlDma_Bd *CurBdPtr;
	unsigned BdCount;
	unsigned BdPartialCount;
	u32 BdStsCr;
	u32 UserIpAppWord;

	CurBdPtr = RingPtr->HwHead;
	BdCount = 0;
	BdPartialCount = 0;

	/* If no BDs in work group, then there's nothing to search */
	if (RingPtr->HwCnt == 0) {
		*BdSetPtr = NULL;
		return (0);
	}

	/* Starting at HwHead, keep moving forward in the list until:
	 *  - A BD is encountered with its completed bit clear in the status
	 *    word which means hardware has not completed processing of that
	 *    BD.
	 *  - A BD is encountered with its XLLDMA_USERIP_APPWORD_OFFSET field
	 *    with value XLLDMA_USERIP_APPWORD_INITVALUE which means hardware
	 *    has not completed updating the BD structure.
	 *  - RingPtr->HwTail is reached
	 *  - The number of requested BDs has been processed
	 */
	while (BdCount < BdLimit) {
		/* Read the status */
		XLLDMA_CACHE_INVALIDATE(CurBdPtr);
		BdStsCr = XLlDma_mBdRead(CurBdPtr,
					 XLLDMA_BD_STSCTRL_USR0_OFFSET);

		/* If the hardware still hasn't processed this BD then we are
		 * done
		 */
		if (!(BdStsCr & XLLDMA_BD_STSCTRL_COMPLETED_MASK)) {
			break;
		}

		/* In RX channel case, check if XLLDMA_USERIP_APPWORD_OFFSET
		 * field of the BD has been updated. If not, RX channel has
		 * not completed updating the BD structure and we delay
		 * the processing of this BD to next time
		 */
		if (RingPtr->IsRxChannel) {
			UserIpAppWord = XLlDma_mBdRead(CurBdPtr,
						       XLLDMA_USERIP_APPWORD_OFFSET);
			if (UserIpAppWord == XLLDMA_USERIP_APPWORD_INITVALUE) {
				break;
			}
		}


		BdCount++;

		/* Hardware has processed this BD so check the "last" bit. If
		 * it is clear, then there are more BDs for the current packet.
		 * Keep a count of these partial packet BDs.
		 */
		if (BdStsCr & XLLDMA_BD_STSCTRL_EOP_MASK) {
			BdPartialCount = 0;
		}
		else {
			BdPartialCount++;
		}

		/* Reached the end of the work group */
		if (CurBdPtr == RingPtr->HwTail) {
			break;
		}

		/* Move on to next BD in work group */
		CurBdPtr = XLlDma_mBdRingNext(RingPtr, CurBdPtr);
	}

	/* Subtract off any partial packet BDs found */
	BdCount -= BdPartialCount;

	/* If BdCount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return success
	 */
	if (BdCount) {
		*BdSetPtr = RingPtr->HwHead;
		RingPtr->HwCnt -= BdCount;
		RingPtr->PostCnt += BdCount;
		XLLDMA_RING_SEEKAHEAD(RingPtr, RingPtr->HwHead, BdCount);
		return (BdCount);
	}
	else {
		*BdSetPtr = NULL;
		return (0);
	}
}


/*****************************************************************************/
/**
 * Frees a set of BDs that had been previously retrieved with
 * XLlDma_BdRingFromHw().
 *
 * @param RingPtr is a pointer to the descriptor ring instance to be worked on.
 * @param NumBd is the number of BDs to free.
 * @param BdSetPtr is the head of a list of BDs returned by
 *        XLlDma_BdRingFromHw().
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was freed.
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XLlDma_BdRingFromHw().
 *
 * @note This function should not be preempted by another XLlDma function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 * @internal
 *          This Interrupt handler provided by application MUST clear pending
 *          interrupts before handling them by calling the call back. Otherwise
 *          the following corner case could raise some issue:
 *
 *           - A packet was transmitted and asserted an TX interrupt, and if
 *             this interrupt handler calls the call back before clears the
 *             interrupt, another packet could get transmitted (and assert the
 *             interrupt) between when the call back function returned and when
 *             the interrupt clearing operation begins, and the interrupt
 *             clearing operation will clear the interrupt raised by the second
 *             packet and won't never process its according buffer descriptors
 *             until a new interrupt occurs.
 *
 *           Changing the sequence to "Clear interrupts, then handle" solve this
 *           issue. If the interrupt raised by the second packet is before the
 *           the interrupt clearing operation, the descriptors associated with
 *           the second packet must have been finished by hardware and ready for
 *           the handling by the call back; otherwise, the interrupt raised by
 *           the second packet is after the interrupt clearing operation,
 *           the packet's buffer descriptors will be handled by the call back in
 *           current pass, if the descriptors are finished before the call back
 *           is invoked, or next pass otherwise.
 *
 *           Please note that if the second packet is handled by the call back
 *           in current pass, the next pass could find no buffer descriptor
 *           finished by the hardware. (i.e., XLlDma_BdRingFromHw() returns 0).
 *           As XLlDma_BdRingFromHw() and XLlDma_BdRingFree() are used in pair,
 *           XLlDma_BdRingFree() covers this situation by checking if the BD
 *           list to free is empty
 *****************************************************************************/
int XLlDma_BdRingFree(XLlDma_BdRing * RingPtr, unsigned NumBd,
		      XLlDma_Bd * BdSetPtr)
{
	/* If the BD Set to free is empty, return immediately with value
	 * XST_SUCCESS. See the @internal comment block above for detailed
	 * information
	 */
	if (NumBd == 0) {
		return XST_SUCCESS;
	}

	/* Make sure we are in sync with XLlDma_BdRingFromHw() */
	if ((RingPtr->PostCnt < NumBd) || (RingPtr->PostHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Update pointers and counters */
	RingPtr->FreeCnt += NumBd;
	RingPtr->PostCnt -= NumBd;
	XLLDMA_RING_SEEKAHEAD(RingPtr, RingPtr->PostHead, NumBd);

	return (XST_SUCCESS);
}


/*****************************************************************************/
/**
 * Check the internal data structures of the BD ring for the provided channel.
 * The following checks are made:
 *
 *   - Is the BD ring linked correctly in physical address space.
 *   - Do the internal pointers point to BDs in the ring.
 *   - Do the internal counters add up.
 *
 * The channel should be stopped prior to calling this function.
 *
 * @param RingPtr is a pointer to the descriptor ring to be worked on.
 *
 * @return
 *   - XST_SUCCESS if no errors were found.
 *   - XST_DMA_SG_NO_LIST if the ring has not been created.
 *   - XST_IS_STARTED if the channel is not stopped.
 *   - XST_DMA_SG_LIST_ERROR if a problem is found with the internal data
 *     structures. If this value is returned, the channel should be reset to
 *     avoid data corruption or system instability.
 *
 * @note This function should not be preempted by another XLlDma ring function
 *       call that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 *****************************************************************************/
int XLlDma_BdRingCheck(XLlDma_BdRing * RingPtr)
{
	u32 AddrV, AddrP;
	unsigned i;

	/* Is the list created */
	if (RingPtr->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Can't check if channel is running */
	if (RingPtr->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_IS_STARTED);
	}

	/* RunState doesn't make sense */
	else if (RingPtr->RunState != XST_DMA_SG_IS_STOPPED) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify internal pointers point to correct memory space */
	AddrV = (u32) RingPtr->FreeHead;
	if ((AddrV < RingPtr->FirstBdAddr) || (AddrV > RingPtr->LastBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PreHead;
	if ((AddrV < RingPtr->FirstBdAddr) || (AddrV > RingPtr->LastBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwHead;
	if ((AddrV < RingPtr->FirstBdAddr) || (AddrV > RingPtr->LastBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwTail;
	if ((AddrV < RingPtr->FirstBdAddr) || (AddrV > RingPtr->LastBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PostHead;
	if ((AddrV < RingPtr->FirstBdAddr) || (AddrV > RingPtr->LastBdAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify internal counters add up */
	if ((RingPtr->HwCnt + RingPtr->PreCnt + RingPtr->FreeCnt +
	     RingPtr->PostCnt) != RingPtr->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify BDs are linked correctly */
	AddrV = RingPtr->FirstBdAddr;
	AddrP = RingPtr->FirstBdPhysAddr + RingPtr->Separation;
	for (i = 1; i < RingPtr->AllCnt; i++) {
		XLLDMA_CACHE_INVALIDATE(AddrV);
		/* Check next pointer for this BD. It should equal to the
		 * physical address of next BD
		 */
		if (XLlDma_mBdRead(AddrV, XLLDMA_BD_NDESC_OFFSET) != AddrP) {
			return (XST_DMA_SG_LIST_ERROR);
		}

		/* Move on to next BD */
		AddrV += RingPtr->Separation;
		AddrP += RingPtr->Separation;
	}

	XLLDMA_CACHE_INVALIDATE(AddrV);
	/* Last BD should point back to the beginning of ring */
	if (XLlDma_mBdRead(AddrV, XLLDMA_BD_NDESC_OFFSET) !=
	    RingPtr->FirstBdPhysAddr) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* No problems found */
	return (XST_SUCCESS);
}
