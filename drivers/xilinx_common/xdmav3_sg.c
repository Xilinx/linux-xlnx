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
*       (c) Copyright 2006 Xilinx Inc.
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
* @file xdmav3_sg.c
*
* This file implements Scatter-Gather DMA (SGDMA) related functions. For more
* information on this driver, see xdmav3.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 3.00a rmm  03/11/06 First release
*       rmm  06/22/06 Fixed C++ compiler warnings
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>
#include <asm/delay.h>

#include "xdmav3.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/****************************************************************************
 * These cache macros are used throughout this source code file to show
 * users where cache operations should occur if BDs were to be placed in
 * a cached memory region. Cacheing BD regions, however, is not common.
 *
 * The macros are implemented as NULL operations, but may be hooked into
 * XENV macros in future revisions of this driver.
 ****************************************************************************/
#define XDMAV3_CACHE_FLUSH(BdPtr)
#define XDMAV3_CACHE_INVALIDATE(BdPtr)

/****************************************************************************
 * Compute the virtual address of a descriptor from its physical address
 *
 * @param Ring is the ring BdPtr appears in
 * @param BdPtr is the physical address of the BD
 *
 * @returns Virtual address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 ****************************************************************************/
#define XDMAV3_PHYS_TO_VIRT(Ring, BdPtr) \
    ((u32)BdPtr + (Ring->BaseAddr - Ring->PhysBaseAddr))

/****************************************************************************
 * Compute the physical address of a descriptor from its virtual address
 *
 * @param Ring is the ring BdPtr appears in
 * @param BdPtr is the physical address of the BD
 *
 * @returns Physical address of BdPtr
 *
 * @note Assume BdPtr is always a valid BD in the ring
 ****************************************************************************/
#define XDMAV3_VIRT_TO_PHYS(Ring, BdPtr) \
    ((u32)BdPtr - (Ring->BaseAddr - Ring->PhysBaseAddr))

/****************************************************************************
 * Clear or set the SGS bit of the DMACR register
 ****************************************************************************/
#define XDMAV3_HW_SGS_CLEAR                                             \
    XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_DMACR_OFFSET,         \
                     XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMACR_OFFSET) \
                     & ~XDMAV3_DMACR_SGS_MASK)

#define XDMAV3_HW_SGS_SET                                               \
    XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_DMACR_OFFSET,         \
                     XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMACR_OFFSET) \
                     | XDMAV3_DMACR_SGS_MASK)

/****************************************************************************
 * Move the BdPtr argument ahead an arbitrary number of BDs wrapping around
 * to the beginning of the ring if needed.
 *
 * We know if a wrapaound should occur if the new BdPtr is greater than
 * the high address in the ring OR if the new BdPtr crosses over the
 * 0xFFFFFFFF to 0 boundary. The latter test is a valid one since we do not
 * allow a BD space to span this boundary.
 *
 * @param Ring is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 ****************************************************************************/
#define XDMAV3_RING_SEEKAHEAD(Ring, BdPtr, NumBd)                       \
    {                                                                   \
        u32 Addr = (u32)BdPtr;                                  \
                                                                        \
        Addr += (Ring->Separation * NumBd);                             \
        if ((Addr > Ring->HighAddr) || ((u32)BdPtr > Addr))         \
        {                                                               \
            Addr -= Ring->Length;                                       \
        }                                                               \
                                                                        \
        BdPtr = (XDmaBdV3*)Addr;                                        \
    }

/****************************************************************************
 * Move the BdPtr argument backwards an arbitrary number of BDs wrapping
 * around to the end of the ring if needed.
 *
 * We know if a wrapaound should occur if the new BdPtr is less than
 * the base address in the ring OR if the new BdPtr crosses over the
 * 0xFFFFFFFF to 0 boundary. The latter test is a valid one since we do not
 * allow a BD space to span this boundary.
 *
 * @param Ring is the ring BdPtr appears in
 * @param BdPtr on input is the starting BD position and on output is the
 *        final BD position
 * @param NumBd is the number of BD spaces to increment
 *
 ****************************************************************************/
#define XDMAV3_RING_SEEKBACK(Ring, BdPtr, NumBd)                        \
    {                                                                   \
        u32 Addr = (u32)BdPtr;                                  \
                                                                        \
        Addr -= (Ring->Separation * NumBd);                             \
        if ((Addr < Ring->BaseAddr) || ((u32)BdPtr < Addr))         \
        {                                                               \
            Addr += Ring->Length;                                       \
        }                                                               \
                                                                        \
        BdPtr = (XDmaBdV3*)Addr;                                        \
    }


/************************** Function Prototypes ******************************/

static int IsSgDmaChannel(XDmaV3 * InstancePtr);


/************************** Variable Definitions *****************************/

/******************************************************************************/
/**
 * Start the SGDMA channel.
 *
 * @param InstancePtr is a pointer to the instance to be started.
 *
 * @return
 * - XST_SUCCESS if channel was started.
 * - XST_DMA_SG_NO_LIST if the channel has no initialized BD ring.
 *
 ******************************************************************************/
int XDmaV3_SgStart(XDmaV3 * InstancePtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;
	u32 Swcr;

	/* BD list has yet to be created for this channel */
	if (Ring->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Do nothing if already started */
	if (Ring->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_SUCCESS);
	}

	/* Note as started */
	Ring->RunState = XST_DMA_SG_IS_STARTED;

	/* Restore BDA */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_BDA_OFFSET,
			 Ring->BdaRestart);

	/* If there are unprocessed BDs then we want to channel to begin processing
	 * right away
	 */
	if ((XDmaV3_mReadBd(XDMAV3_PHYS_TO_VIRT(Ring, Ring->BdaRestart),
			    XDMAV3_BD_DMASR_OFFSET) & XDMAV3_DMASR_DMADONE_MASK)
	    == 0) {
		/* DMACR.SGS = 0 */
		XDMAV3_HW_SGS_CLEAR;
	}

	/* To start, clear SWCR.DSGAR, and set SWCR.SGE */
	Swcr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	Swcr &= ~XDMAV3_SWCR_DSGAR_MASK;
	Swcr |= XDMAV3_SWCR_SGE_MASK;
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET, Swcr);

	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Stop the SGDMA or Simple SGDMA channel gracefully. Any DMA operation
 * currently in progress is allowed to finish.
 *
 * An interrupt may be generated as the DMA engine finishes the packet in
 * process. To prevent this (if desired) then disabled DMA interrupts prior to
 * invoking this function.
 *
 * If after stopping the channel, new BDs are enqueued with XDmaV3_SgBdToHw(),
 * then those BDs will not be processed until after XDmaV3_SgStart() is called.
 *
 * @param InstancePtr is a pointer to the instance to be stopped.
 *
 * @note This function will block until the HW indicates that DMA has stopped.
 *
 ******************************************************************************/
void XDmaV3_SgStop(XDmaV3 * InstancePtr)
{
	volatile u32 Swcr;
	u32 Dmasr;
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;
	u32 Ier;

	/* Save the contents of the interrupt enable register then disable
	 * interrupts. This register will be restored at the end of the function
	 */
	Ier = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_IER_OFFSET);
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_IER_OFFSET, 0);

	/* Stopping the HW is a three step process:
	 *   1. Set SWCR.SGD=1
	 *   2. Wait for SWCR.SGE=0
	 *   3. Set SWCR.DSGAR=0 and SWCR.SGE=1
	 *
	 * Once we've successfully gone through this process, the HW is fully
	 * stopped. To restart we must give the HW a new BDA.
	 */
	Swcr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);

	/* If the channel is currently active, stop it by setting SWCR.SGD=1
	 * and waiting for SWCR.SGE to toggle to 0
	 */
	if (Swcr & XDMAV3_SWCR_SGE_MASK) {
		Swcr |= XDMAV3_SWCR_SGD_MASK;
		XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET,
				 Swcr);

		while (Swcr & XDMAV3_SWCR_SGE_MASK) {
			Swcr = XDmaV3_mReadReg(InstancePtr->RegBase,
					       XDMAV3_SWCR_OFFSET);
		}
	}

	/* Note as stopped */
	Ring->RunState = XST_DMA_SG_IS_STOPPED;

	/* Save the BDA to restore when channel is restarted */
	Ring->BdaRestart =
		(XDmaBdV3 *) XDmaV3_mReadReg(InstancePtr->RegBase,
					     XDMAV3_BDA_OFFSET);

	/* If this is a receive channel, then the BDA restore may require a more
	 * complex treatment. If the channel stopped without processing a packet,
	 * then DMASR.SGDONE will be clear. The BDA we've already read in this case
	 * is really BDA->BDA so we need to backup one BDA to get the correct
	 * restart point.
	 */
	Dmasr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if ((Dmasr & XDMAV3_DMASR_DMACNFG_MASK) ==
	    XDMAV3_DMASR_DMACNFG_SGDMARX_MASK) {
		if (!(Dmasr & XDMAV3_DMASR_SGDONE_MASK)) {
			Ring->BdaRestart =
				(XDmaBdV3 *) XDMAV3_PHYS_TO_VIRT(Ring,
								 Ring->
								 BdaRestart);
			Ring->BdaRestart =
				XDmaV3_mSgBdPrev(InstancePtr, Ring->BdaRestart);
			Ring->BdaRestart =
				(XDmaBdV3 *) XDMAV3_VIRT_TO_PHYS(Ring,
								 Ring->
								 BdaRestart);
		}
	}

	Swcr |= XDMAV3_SWCR_DSGAR_MASK;
	Swcr &= ~XDMAV3_SWCR_SGD_MASK;
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET, Swcr);

	/* Restore interrupt enables. If an interrupt occurs due to this function
	 * stopping the channel then it will happen right here
	 */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_IER_OFFSET, Ier);
}


/******************************************************************************/
/**
 * Set the packet threshold for this SGDMA channel. This has the effect of
 * delaying processor interrupts until the given number of packets (not BDs)
 * have been processed.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Threshold is the packet threshold to set. If 0 is specified, then
 *        this feature is disabled. Maximum threshold is 2^12 - 1.
 *
 * @return
 * - XST_SUCCESS if threshold set properly.
 * - XST_NO_FEATURE if this function was called on a DMA channel that does not
 *   have interrupt coalescing capabilities.
 *
 * @note This function should not be prempted by another XDmaV3 function.
 *
 ******************************************************************************/
int XDmaV3_SgSetPktThreshold(XDmaV3 * InstancePtr, u16 Threshold)
{
	u32 Reg;

	/* Is this a SGDMA channel */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (!IsSgDmaChannel(InstancePtr)) {
		return (XST_NO_FEATURE);
	}

	/* Replace the pkt threshold field in the SWCR */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	Reg &= ~XDMAV3_SWCR_PCT_MASK;
	Reg |= ((Threshold << XDMAV3_SWCR_PCT_SHIFT) & XDMAV3_SWCR_PCT_MASK);
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET, Reg);

	/* Finished */
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Set the packet waitbound timer for this SGDMA channel. See xdmav3.h for more
 * information on interrupt coalescing and the effects of the waitbound timer.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param TimerVal is the waitbound period to set. If 0 is specified, then
 *        this feature is disabled. Maximum waitbound is 2^12 - 1. LSB is
 *        1 millisecond (approx).
 *
 * @return
 * - XST_SUCCESS if waitbound set properly.
 * - XST_NO_FEATURE if this function was called on a DMA channel that does not
 *   have interrupt coalescing capabilities.
 *
 * @note This function should not be prempted by another XDmaV3 function.
 *
 ******************************************************************************/
int XDmaV3_SgSetPktWaitbound(XDmaV3 * InstancePtr, u16 TimerVal)
{
	u32 Reg;

	/* Is this a SGDMA channel */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (!IsSgDmaChannel(InstancePtr)) {
		return (XST_NO_FEATURE);
	}

	/* Replace the waitbound field in the SWCR */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	Reg &= ~XDMAV3_SWCR_PWB_MASK;
	Reg |= ((TimerVal << XDMAV3_SWCR_PWB_SHIFT) & XDMAV3_SWCR_PWB_MASK);
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET, Reg);

	/* Finished */
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Get the packet threshold for this channel that was set with
 * XDmaV3_SgSetPktThreshold().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return Current packet threshold as reported by HW. If the channel does not
 *         include interrupt coalescing, then the return value will always be 0.
 ******************************************************************************/
u16 XDmaV3_SgGetPktThreshold(XDmaV3 * InstancePtr)
{
	u32 Reg;

	/* Is this a SGDMA channel */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (!IsSgDmaChannel(InstancePtr)) {
		return (0);
	}

	/* Get the threshold */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	Reg &= XDMAV3_SWCR_PCT_MASK;
	Reg >>= XDMAV3_SWCR_PCT_SHIFT;
	return ((u16) Reg);
}


/******************************************************************************/
/**
 * Get the waitbound timer for this channel that was set with
 * XDmaV3_SgSetPktWaitbound().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return Current waitbound timer as reported by HW. If the channel does not
 *         include interrupt coalescing, then the return value will always be 0.
 ******************************************************************************/
u16 XDmaV3_SgGetPktWaitbound(XDmaV3 * InstancePtr)
{
	u32 Reg;

	/* Is this a SGDMA channel */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (!IsSgDmaChannel(InstancePtr)) {
		return (0);
	}

	/* Get the threshold */
	Reg = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	Reg &= XDMAV3_SWCR_PWB_MASK;
	Reg >>= XDMAV3_SWCR_PWB_SHIFT;
	return ((u16) Reg);
}


/******************************************************************************/
/**
 * Using a memory segment allocated by the caller, create and setup the BD list
 * for the given SGDMA channel.
 *
 * @param InstancePtr is the instance to be worked on.
 * @param PhysAddr is the physical base address of user memory region.
 * @param VirtAddr is the virtual base address of the user memory region. If
 *        address translation is not being utilized, then VirtAddr should be
 *        equivalent to PhysAddr.
 * @param Alignment governs the byte alignment of individual BDs. This function
 *        will enforce a minimum alignment of 4 bytes with no maximum as long as
 *        it is specified as a power of 2.
 * @param BdCount is the number of BDs to setup in the user memory region. It is
 *        assumed the region is large enough to contain the BDs. Refer to the
 *        "SGDMA List Creation" section  in xdmav3.h for more information on
 *        list creation.
 *
 * @return
 *
 * - XST_SUCCESS if initialization was successful
 * - XST_NO_FEATURE if the provided instance is a non SGDMA type of DMA
 *   channel.
 * - XST_INVALID_PARAM under any of the following conditions: 1) PhysAddr and/or
 *   VirtAddr are not aligned to the given Alignment parameter; 2) Alignment
 *   parameter does not meet minimum requirements or is not a power of 2 value;
 *   3) BdCount is 0.
 * - XST_DMA_SG_LIST_ERROR if the memory segment containing the list spans
 *   over address 0x00000000 in virtual address space.
 *
 * @note
 *
 * Some DMA HW requires 8 or more byte alignments of BDs. Make sure the correct
 * value is passed into the Alignment parameter to meet individual DMA HW
 * requirements.
 *
 ******************************************************************************/
int XDmaV3_SgListCreate(XDmaV3 * InstancePtr, u32 PhysAddr, u32 VirtAddr,
			u32 Alignment, unsigned BdCount)
{
	unsigned i;
	u32 BdV;
	u32 BdP;
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;

	/* In case there is a failure prior to creating list, make sure the following
	 * attributes are 0 to prevent calls to other SG functions from doing anything
	 */
	Ring->AllCnt = 0;
	Ring->FreeCnt = 0;
	Ring->HwCnt = 0;
	Ring->PreCnt = 0;
	Ring->PostCnt = 0;

	/* Is this a SGDMA channel */
	if (!IsSgDmaChannel(InstancePtr)) {
		return (XST_NO_FEATURE);
	}

	/* Make sure Alignment parameter meets minimum requirements */
	if (Alignment < XDMABDV3_MINIMUM_ALIGNMENT) {
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

	/* Parameters are sane. Stop the HW just to be safe */
	XDmaV3_SgStop(InstancePtr);

	/* Figure out how many bytes will be between the start of adjacent BDs */
	Ring->Separation =
		(sizeof(XDmaBdV3) + (Alignment - 1)) & ~(Alignment - 1);

	/* Must make sure the ring doesn't span address 0x00000000. If it does,
	 * then the next/prev BD traversal macros will fail.
	 */
	if (VirtAddr > (VirtAddr + (Ring->Separation * BdCount) - 1)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Initial ring setup:
	 *  - Clear the entire space
	 *  - Setup each BD's BDA field with the physical address of the next BD
	 *  - Set each BD's DMASR.DMADONE bit
	 */
	memset((void *) VirtAddr, 0, (Ring->Separation * BdCount));

	BdV = VirtAddr;
	BdP = PhysAddr + Ring->Separation;
	for (i = 1; i < BdCount; i++) {
		XDmaV3_mWriteBd(BdV, XDMAV3_BD_BDA_OFFSET, BdP);
		XDmaV3_mWriteBd(BdV, XDMAV3_BD_DMASR_OFFSET,
				XDMAV3_DMASR_DMADONE_MASK);
		XDMAV3_CACHE_FLUSH(BdV);
		BdV += Ring->Separation;
		BdP += Ring->Separation;
	}

	/* At the end of the ring, link the last BD back to the top */
	XDmaV3_mWriteBd(BdV, XDMAV3_BD_BDA_OFFSET, PhysAddr);

	/* Setup and initialize pointers and counters */
	InstancePtr->BdRing.RunState = XST_DMA_SG_IS_STOPPED;
	Ring->BaseAddr = VirtAddr;
	Ring->PhysBaseAddr = PhysAddr;
	Ring->HighAddr = BdV;
	Ring->Length = Ring->HighAddr - Ring->BaseAddr + Ring->Separation;
	Ring->AllCnt = BdCount;
	Ring->FreeCnt = BdCount;
	Ring->FreeHead = (XDmaBdV3 *) VirtAddr;
	Ring->PreHead = (XDmaBdV3 *) VirtAddr;
	Ring->HwHead = (XDmaBdV3 *) VirtAddr;
	Ring->HwTail = (XDmaBdV3 *) VirtAddr;
	Ring->PostHead = (XDmaBdV3 *) VirtAddr;
	Ring->BdaRestart = (XDmaBdV3 *) PhysAddr;

	/* Make sure the DMACR.SGS is 1 so that no DMA operations proceed until
	 * the start function is called.
	 */
	XDMAV3_HW_SGS_SET;

	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Clone the given BD into every BD in the list. Except for XDMAV3_BD_BDA_OFFSET,
 * every field of the source BD is replicated in every BD of the list.
 *
 * This function can be called only when all BDs are in the free group such as
 * they are immediately after initialization with XDmaV3_SgListCreate(). This
 * prevents modification of BDs while they are in use by HW or the user.
 *
 * @param InstancePtr is the instance to be worked on.
 * @param SrcBdPtr is the source BD template to be cloned into the list. This BD
 *        will be modified.
 *
 * @return
 *   - XST_SUCCESS if the list was modified.
 *   - XST_DMA_SG_NO_LIST if a list has not been created.
 *   - XST_DMA_SG_LIST_ERROR if some of the BDs in this channel are under HW
 *     or user control.
 *   - XST_DEVICE_IS_STARTED if the DMA channel has not been stopped.
 *
 ******************************************************************************/
int XDmaV3_SgListClone(XDmaV3 * InstancePtr, XDmaBdV3 * SrcBdPtr)
{
	unsigned i;
	u32 CurBd;
	u32 Save;
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;

	/* Can't do this function if there isn't a ring */
	if (Ring->AllCnt == 0) {
		return (XST_DMA_SG_NO_LIST);
	}

	/* Can't do this function with the channel running */
	if (Ring->RunState == XST_DMA_SG_IS_STARTED) {
		return (XST_DEVICE_IS_STARTED);
	}

	/* Can't do this function with some of the BDs in use */
	if (Ring->FreeCnt != Ring->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Modify the template by setting DMASR.DMADONE */
	Save = XDmaV3_mReadBd(SrcBdPtr, XDMAV3_BD_DMASR_OFFSET);
	Save |= XDMAV3_DMASR_DMADONE_MASK;
	XDmaV3_mWriteBd(SrcBdPtr, XDMAV3_BD_DMASR_OFFSET, Save);

	/* Starting from the top of the ring, save BD.Next, overwrite the entire BD
	 * with the template, then restore BD.Next
	 */
	for (i = 0, CurBd = Ring->BaseAddr;
	     i < Ring->AllCnt; i++, CurBd += Ring->Separation) {
		Save = XDmaV3_mReadBd(CurBd, XDMAV3_BD_BDA_OFFSET);
		memcpy((void *) CurBd, SrcBdPtr, sizeof(XDmaBdV3));
		XDmaV3_mWriteBd(CurBd, XDMAV3_BD_BDA_OFFSET, Save);
		XDMAV3_CACHE_FLUSH(CurBd);
	}

	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Reserve locations in the BD list. The set of returned BDs may be modified in
 * preparation for future DMA transaction(s). Once the BDs are ready to be
 * submitted to HW, the user must call XDmaV3_SgBdToHw() in the same order which
 * they were allocated here. Example:
 *
 * <pre>
 *        NumBd = 2;
 *        Status = XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd, &MyBdSet);
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
 *            CurBd = XDmaV3_mSgBdNext(MyDmaInstPtr, CurBd);
 *        }
 *
 *        // Give list to HW
 *        Status = XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd, MyBdSet);
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be allocated and given to HW in the correct sequence:
 * <pre>
 *        // Legal
 *        XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd2, &MySet2);
 *        XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd1, MySet1);
 *        XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdAlloc(MyDmaInstPtr, NumBd2, &MySet2);
 *        XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd2, MySet2);
 *        XDmaV3_SgBdToHw(MyDmaInstPtr, NumBd1, MySet1);
 * </pre>
 *
 * Use the API defined in xdmabdv3.h to modify individual BDs. Traversal of the
 * BD set can be done using XDmaV3_mSgBdNext() and XDmaV3_mSgBdPrev().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs to allocate
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for modification.
 *
 * @return
 *   - XST_SUCCESS if the requested number of BDs was returned in the BdSetPtr
 *     parameter.
 *   - XST_FAILURE if there were not enough free BDs to satisfy the request.
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 * @note Do not modify more BDs than the number requested with the NumBd
 *       parameter. Doing so will lead to data corruption and system
 *       instability.
 *
 ******************************************************************************/
int XDmaV3_SgBdAlloc(XDmaV3 * InstancePtr, unsigned NumBd, XDmaBdV3 ** BdSetPtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;

	/* Enough free BDs available for the request? */
	if (Ring->FreeCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead forward */
	*BdSetPtr = Ring->FreeHead;
	XDMAV3_RING_SEEKAHEAD(Ring, Ring->FreeHead, NumBd);
	Ring->FreeCnt -= NumBd;
	Ring->PreCnt += NumBd;
	return (XST_SUCCESS);
}

/******************************************************************************/
/**
 * Fully or partially undo an XDmaV3_SgBdAlloc() operation. Use this function
 * if all the BDs allocated by XDmaV3_SgBdAlloc() could not be transferred to
 * HW with XDmaV3_SgBdToHw().
 *
 * This function helps out in situations when an unrelated error occurs after
 * BDs have been allocated but before they have been given to HW. An example of
 * this type of error would be an OS running out of resources.
 *
 * This function is not the same as XDmaV3_SgBdFree(). The Free function returns
 * BDs to the free list after they have been processed by HW, while UnAlloc
 * returns them before being processed by HW.
 *
 * There are two scenarios where this function can be used. Full UnAlloc or
 * Partial UnAlloc. A Full UnAlloc means all the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XDmaV3_SgBdAlloc(Inst, 10, &BdPtr);
 *        .
 *        .
 *    if (Error)
 *    {
 *        Status = XDmaV3_SgBdUnAlloc(Inst, 10, &BdPtr);
 *    }
 * </pre>
 *
 * A partial UnAlloc means some of the BDs Alloc'd will be returned:
 *
 * <pre>
 *    Status = XDmaV3_SgBdAlloc(Inst, 10, &BdPtr);
 *    BdsLeft = 10;
 *    CurBdPtr = BdPtr;
 *
 *    while (BdsLeft)
 *    {
 *       if (Error)
 *       {
 *          Status = XDmaV3_SgBdUnAlloc(Inst, BdsLeft, CurBdPtr);
 *       }
 *
 *       CurBdPtr = XDmaV3_SgBdNext(Inst, CurBdPtr);
 *       BdsLeft--;
 *    }
 * </pre>
 *
 * A partial UnAlloc must include the last BD in the list that was Alloc'd.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs to allocate
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for modification.
 *
 * @return
 *   - XST_SUCCESS if the BDs were unallocated.
 *   - XST_FAILURE if NumBd parameter was greater that the number of BDs in the
 *     preprocessing state.
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 ******************************************************************************/
int XDmaV3_SgBdUnAlloc(XDmaV3 * InstancePtr, unsigned NumBd,
		       XDmaBdV3 * BdSetPtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;

	/* Enough BDs in the free state for the request? */
	if (Ring->PreCnt < NumBd) {
		return (XST_FAILURE);
	}

	/* Set the return argument and move FreeHead backward */
	XDMAV3_RING_SEEKBACK(Ring, Ring->FreeHead, NumBd);
	Ring->FreeCnt += NumBd;
	Ring->PreCnt -= NumBd;
	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Enqueue a set of BDs to HW that were previously allocated by
 * XDmaV3_SgBdAlloc(). Once this function returns, the argument BD set goes
 * under HW control. Any changes made to these BDs after this point will corrupt
 * the BD list leading to data corruption and system instability.
 *
 * The set will be rejected if the last BD of the set does not mark the end of
 * a packet (see XDmaBdV3_mSetLast()).
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs in the set.
 * @param BdSetPtr is the first BD of the set to commit to HW.
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was accepted and enqueued to HW.
 *   - XST_FAILURE if the set of BDs was rejected because the last BD of the set
 *     did not have its "last" bit set.
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XDmaV3_SgBdAlloc().
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 ******************************************************************************/
int XDmaV3_SgBdToHw(XDmaV3 * InstancePtr, unsigned NumBd, XDmaBdV3 * BdSetPtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;
	XDmaBdV3 *LastBdPtr;
	unsigned i;
	u32 Dmacr;
	u32 Swcr;

	/* Make sure we are in sync with XDmaV3_SgBdAlloc() */
	if ((Ring->PreCnt < NumBd) || (Ring->PreHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* For all BDs in this set (except the last one)
	 *   - Clear DMASR except for DMASR.DMABSY
	 *   - Clear DMACR.SGS
	 *
	 * For the last BD in this set
	 *   - Clear DMASR except for DMASR.DMABSY
	 *   - Set DMACR.SGS (marks the end of the new active list)
	 */
	LastBdPtr = BdSetPtr;
	for (i = 1; i < NumBd; i++) {
		XDmaV3_mWriteBd(LastBdPtr, XDMAV3_BD_DMASR_OFFSET,
				XDMAV3_DMASR_DMABSY_MASK);

		Dmacr = XDmaV3_mReadBd(LastBdPtr, XDMAV3_BD_DMACR_OFFSET);
		XDmaV3_mWriteBd(LastBdPtr, XDMAV3_BD_DMACR_OFFSET,	/* DMACR.SGS = 0 */
				Dmacr & ~XDMAV3_DMACR_SGS_MASK);
		XDMAV3_CACHE_FLUSH(LastBdPtr);

		LastBdPtr = XDmaV3_mSgBdNext(InstancePtr, LastBdPtr);
	}

	/* Last BD */
	XDmaV3_mWriteBd(LastBdPtr, XDMAV3_BD_DMASR_OFFSET,
			XDMAV3_DMASR_DMABSY_MASK);

	Dmacr = XDmaV3_mReadBd(LastBdPtr, XDMAV3_BD_DMACR_OFFSET);
	XDmaV3_mWriteBd(LastBdPtr, XDMAV3_BD_DMACR_OFFSET,	/* DMACR.SGS = 1 */
			Dmacr | XDMAV3_DMACR_SGS_MASK);
	XDMAV3_CACHE_FLUSH(LastBdPtr);

	/* The last BD should have DMACR.LAST set */
	if (!(Dmacr & XDMAV3_DMACR_LAST_MASK)) {
		return (XST_FAILURE);
	}

	/* This set has completed pre-processing, adjust ring pointers & counters */
	XDMAV3_RING_SEEKAHEAD(Ring, Ring->PreHead, NumBd);
	Ring->PreCnt -= NumBd;

	/* If it is running, tell the DMA engine to pause */
	Swcr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET);
	if (Ring->RunState == XST_DMA_SG_IS_STARTED) {
		Swcr |= XDMAV3_SWCR_SGD_MASK;
		XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET,
				 Swcr);
	}

	/* Transfer control of the BDs to the DMA engine. There are two cases to
	 * consider:
	 *
	 * 1) No currently active list.
	 *    In this case, just resume the engine.
	 *
	 * 2) Active list.
	 *    In this case, the last BD in the current list should have DMACR.SGS
	 *    cleared so the engine will never stop there. The new stopping
	 *    point is at the end of the extended list. Once the SGS bits are
	 *    changed, resume the engine.
	 */
	if (Ring->HwCnt != 0) {
		/* Handle case 2 */
		Dmacr = XDmaV3_mReadBd(Ring->HwTail, XDMAV3_BD_DMACR_OFFSET);
		Dmacr &= ~XDMAV3_DMACR_SGS_MASK;
		XDmaV3_mWriteBd(Ring->HwTail, XDMAV3_BD_DMACR_OFFSET, Dmacr);
		XDMAV3_CACHE_FLUSH(Ring->HwTail);
	}

	/* Adjust Hw pointers and counters. XDMAV3_RING_SEEKAHEAD could be used to
	 * advance HwTail, but it will always evaluate to LastBdPtr
	 */
	Ring->HwTail = LastBdPtr;
	Ring->HwCnt += NumBd;

	/* If it was enabled, tell the engine to resume */
	if (Ring->RunState == XST_DMA_SG_IS_STARTED) {
		Swcr &= ~XDMAV3_SWCR_SGD_MASK;
		Swcr |= XDMAV3_SWCR_SGE_MASK;
		XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_SWCR_OFFSET,
				 Swcr);
	}

	return (XST_SUCCESS);
}


/******************************************************************************/
/**
 * Returns a set of BD(s) that have been processed by HW. The returned BDs may
 * be examined to determine the outcome of the DMA transaction(s). Once the BDs
 * have been examined, the user must call XDmaV3_SgBdFree() in the same order
 * which they were retrieved here. Example:
 *
 * <pre>
 *        MaxBd = 0xFFFFFFFF;   // Ensure we get all that are ready
 *
 *        NumBd = XDmaV3_SgBdFromHw(MyDmaInstPtr, MaxBd, &MyBdSet);
 *
 *        if (NumBd == 0)
 *        {
 *           // HW has nothing ready for us yet
 *        }
 *
 *        CurBd = MyBdSet;
 *        for (i=0; i<NumBd; i++)
 *        {
 *           // Examine CurBd for post processing.....
 *
 *           // Onto next BD
 *           CurBd = XDmaV3_mSgBdNext(MyDmaInstPtr, CurBd);
 *           }
 *
 *           XDmaV3_SgBdFree(MyDmaInstPtr, NumBd, MyBdSet); // Return the list
 *        }
 * </pre>
 *
 * A more advanced use of this function may allocate multiple sets of BDs.
 * They must be retrieved from HW and freed in the correct sequence:
 * <pre>
 *        // Legal
 *        XDmaV3_SgBdFromHw(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdFree(MyDmaInstPtr, NumBd1, MySet1);
 *
 *        // Legal
 *        XDmaV3_SgBdFromHw(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdFromHw(MyDmaInstPtr, NumBd2, &MySet2);
 *        XDmaV3_SgBdFree(MyDmaInstPtr, NumBd1, MySet1);
 *        XDmaV3_SgBdFree(MyDmaInstPtr, NumBd2, MySet2);
 *
 *        // Not legal
 *        XDmaV3_SgBdFromHw(MyDmaInstPtr, NumBd1, &MySet1);
 *        XDmaV3_SgBdFromHw(MyDmaInstPtr, NumBd2, &MySet2);
 *        XDmaV3_SgBdFree(MyDmaInstPtr, NumBd2, MySet2);
 *        XDmaV3_SgBdFree(MyDmaInstPtr, NumBd1, MySet1);
 * </pre>
 *
 * If HW has only partially completed a packet spanning multiple BDs, then none
 * of the BDs for that packet will be included in the results.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param BdLimit is the maximum number of BDs to return in the set.
 * @param BdSetPtr is an output parameter, it points to the first BD available
 *        for examination.
 *
 * @return
 *   The number of BDs processed by HW. A value of 0 indicates that no data
 *   is available. No more than BdLimit BDs will be returned.
 *
 * @note Treat BDs returned by this function as read-only.
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 ******************************************************************************/
unsigned XDmaV3_SgBdFromHw(XDmaV3 * InstancePtr, unsigned BdLimit,
			   XDmaBdV3 ** BdSetPtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;
	XDmaBdV3 *CurBd;
	unsigned BdCount;
	unsigned BdPartialCount;
	u32 Dmasr;

	CurBd = Ring->HwHead;
	BdCount = 0;
	BdPartialCount = 0;

	/* If no BDs in work group, then there's nothing to search */
	if (Ring->HwCnt == 0) {
		*BdSetPtr = NULL;
		return (0);
	}

	/* Starting at HwHead, keep moving forward in the list until:
	 *  - A BD is encountered with its DMASR.DMABSY bit set which means HW has
	 *    not completed processing of that BD.
	 *  - Ring->HwTail is reached
	 *  - The number of requested BDs has been processed
	 */
	while (BdCount < BdLimit) {
		/* Read the status */
		XDMAV3_CACHE_INVALIDATE(CurBd);
		Dmasr = XDmaV3_mReadBd(CurBd, XDMAV3_BD_DMASR_OFFSET);

		/* If the HW still hasn't processed this BD then we are done */
		if (Dmasr & XDMAV3_DMASR_DMABSY_MASK) {
			break;
		}

		BdCount++;

		/* HW has processed this BD so check the "last" bit. If it is clear,
		 * then there are more BDs for the current packet. Keep a count of
		 * these partial packet BDs.
		 */
		if (Dmasr & XDMAV3_DMASR_LAST_MASK) {
			BdPartialCount = 0;
		}
		else {
			BdPartialCount++;
		}

		/* Reached the end of the work group */
		if (CurBd == Ring->HwTail) {
			break;
		}

		/* Move on to next BD in work group */
		CurBd = XDmaV3_mSgBdNext(InstancePtr, CurBd);
	}

	/* Subtract off any partial packet BDs found */
	BdCount -= BdPartialCount;

	/* If BdCount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return success
	 */
	if (BdCount) {
		*BdSetPtr = Ring->HwHead;
		Ring->HwCnt -= BdCount;
		Ring->PostCnt += BdCount;
		XDMAV3_RING_SEEKAHEAD(Ring, Ring->HwHead, BdCount);
		return (BdCount);
	}
	else {
		*BdSetPtr = NULL;
		return (0);
	}
}


/******************************************************************************/
/**
 * Frees a set of BDs that had been previously retrieved with XDmaV3_SgBdFromHw().
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param NumBd is the number of BDs to free.
 * @param BdSetPtr is the head of a list of BDs returned by XDmaV3_SgBdFromHw().
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was freed.
 *   - XST_DMA_SG_LIST_ERROR if this function was called out of sequence with
 *     XDmaV3_SgBdFromHw().
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 ******************************************************************************/
int XDmaV3_SgBdFree(XDmaV3 * InstancePtr, unsigned NumBd, XDmaBdV3 * BdSetPtr)
{
	XDmaV3_BdRing *Ring = &InstancePtr->BdRing;

	/* Make sure we are in sync with XDmaV3_SgBdFromHw() */
	if ((Ring->PostCnt < NumBd) || (Ring->PostHead != BdSetPtr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Update pointers and counters */
	Ring->FreeCnt += NumBd;
	Ring->PostCnt -= NumBd;
	XDMAV3_RING_SEEKAHEAD(Ring, Ring->PostHead, NumBd);
	return (XST_SUCCESS);
}


/******************************************************************************/
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
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return
 *   - XST_SUCCESS if the set of BDs was freed.
 *   - XST_DMA_SG_NO_LIST if the list has not been created.
 *   - XST_IS_STARTED if the channel is not stopped.
 *   - XST_DMA_SG_LIST_ERROR if a problem is found with the internal data
 *     structures. If this value is returned, the channel should be reset to
 *     avoid data corruption or system instability.
 *
 * @note This function should not be preempted by another XDmaV3 function call
 *       that modifies the BD space. It is the caller's responsibility to
 *       provide a mutual exclusion mechanism.
 *
 ******************************************************************************/
int XDmaV3_SgCheck(XDmaV3 * InstancePtr)
{
	XDmaV3_BdRing *RingPtr = &InstancePtr->BdRing;
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
	if ((AddrV < RingPtr->BaseAddr) || (AddrV > RingPtr->HighAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PreHead;
	if ((AddrV < RingPtr->BaseAddr) || (AddrV > RingPtr->HighAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwHead;
	if ((AddrV < RingPtr->BaseAddr) || (AddrV > RingPtr->HighAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->HwTail;
	if ((AddrV < RingPtr->BaseAddr) || (AddrV > RingPtr->HighAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	AddrV = (u32) RingPtr->PostHead;
	if ((AddrV < RingPtr->BaseAddr) || (AddrV > RingPtr->HighAddr)) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify internal counters add up */
	if ((RingPtr->HwCnt + RingPtr->PreCnt + RingPtr->FreeCnt +
	     RingPtr->PostCnt) != RingPtr->AllCnt) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* Verify BDs are linked correctly */
	AddrV = RingPtr->BaseAddr;
	AddrP = RingPtr->PhysBaseAddr + RingPtr->Separation;
	for (i = 1; i < RingPtr->AllCnt; i++) {
		/* Check BDA for this BD. It should point to next physical addr */
		if (XDmaV3_mReadBd(AddrV, XDMAV3_BD_BDA_OFFSET) != AddrP) {
			return (XST_DMA_SG_LIST_ERROR);
		}

		/* Move on to next BD */
		AddrV += RingPtr->Separation;
		AddrP += RingPtr->Separation;
	}

	/* Last BD should point back to the beginning of ring */
	if (XDmaV3_mReadBd(AddrV, XDMAV3_BD_BDA_OFFSET) !=
	    RingPtr->PhysBaseAddr) {
		return (XST_DMA_SG_LIST_ERROR);
	}

	/* No problems found */
	return (XST_SUCCESS);
}


/******************************************************************************
 * Verify given channel is of the SGDMA variety.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 *
 * @return
 *   - 1 if channel is of type SGDMA
 *   - 0 if channel is not of type SGDMA
 ******************************************************************************/
static int IsSgDmaChannel(XDmaV3 * InstancePtr)
{
	u32 Dmasr;

	Dmasr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (Dmasr & (XDMAV3_DMASR_DMACNFG_SGDMARX_MASK |
		     XDMAV3_DMASR_DMACNFG_SGDMATX_MASK |
		     XDMAV3_DMASR_DMACNFG_SSGDMA_MASK)) {
		return (1);
	}
	else {
		return (0);
	}
}
