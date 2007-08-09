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
* @file xlldma_bdring.h
*
* This file contains DMA channel related structure and constant definition
* as well as function prototypes. Each DMA channel is managed by a Buffer
* Descriptor ring, and so XLlDma_BdRing is chosen as the symbol prefix used in
* this file. See xlldma.h for more information.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a xd   12/21/06 First release
* </pre>
*
******************************************************************************/

#ifndef XLLDMA_BDRING_H		/* prevent circular inclusions */
#define XLLDMA_BDRING_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

#include "xbasic_types.h"
#include "xstatus.h"
#include "xlldma_hw.h"
#include "xlldma_bd.h"

/** Container structure for descriptor storage control. If address translation
 * is enabled, then all addresses and pointers excluding FirstBdPhysAddr are
 * expressed in terms of the virtual address.
 */
typedef struct {
	u32 ChanBase;	       /**< Virtual base address of channel registers
                                       */
	u32 IsRxChannel;       /**< Is this a receive channel ? */
	u32 FirstBdPhysAddr;   /**< Physical address of 1st BD in list */
	u32 FirstBdAddr;       /**< Virtual address of 1st BD in list */
	u32 LastBdAddr;	       /**< Virtual address of last BD in the list */
	u32 Length;	       /**< Total size of ring in bytes */
	u32 RunState;	       /**< Flag to indicate channel is started */
	u32 Separation;	       /**< Number of bytes between the starting
	                            address of adjacent BDs */
	XLlDma_Bd *FreeHead;   /**< First BD in the free group */
	XLlDma_Bd *PreHead;    /**< First BD in the pre-work group */
	XLlDma_Bd *HwHead;     /**< First BD in the work group */
	XLlDma_Bd *HwTail;     /**< Last BD in the work group */
	XLlDma_Bd *PostHead;   /**< First BD in the post-work group */
	XLlDma_Bd *BdaRestart; /**< BD to load when channel is started */
	u32 FreeCnt;	       /**< Number of allocatable BDs in free group */
	u32 PreCnt;	       /**< Number of BDs in pre-work group */
	u32 HwCnt;	       /**< Number of BDs in work group */
	u32 PostCnt;	       /**< Number of BDs in post-work group */
	u32 AllCnt;	       /**< Total Number of BDs for channel */
} XLlDma_BdRing;

/*****************************************************************************/
/**
* Use this macro at initialization time to determine how many BDs will fit
* within the given memory constraints.
*
* The results of this macro can be provided to XLlDma_BdRingCreate().
*
* @param Alignment specifies what byte alignment the BDs must fall on and
*        must be a power of 2 to get an accurate calculation (32, 64, 126,...)
* @param Bytes is the number of bytes to be used to store BDs.
*
* @return Number of BDs that can fit in the given memory area
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingCntCalc(u32 Alignment, u32 Bytes)
*
******************************************************************************/
#define XLlDma_mBdRingCntCalc(Alignment, Bytes)                           \
	(u32)((Bytes)/((sizeof(XLlDma_Bd)+((Alignment)-1))&~((Alignment)-1)))


/*****************************************************************************/
/**
* Use this macro at initialization time to determine how many bytes of memory
* are required to contain a given number of BDs at a given alignment.
*
* @param Alignment specifies what byte alignment the BDs must fall on. This
*        parameter must be a power of 2 to get an accurate calculation (32, 64,
*        128,...)
* @param NumBd is the number of BDs to calculate memory size requirements for
*
* @return The number of bytes of memory required to create a BD list with the
*         given memory constraints.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingMemCalc(u32 Alignment, u32 NumBd)
*
******************************************************************************/
#define XLlDma_mBdRingMemCalc(Alignment, NumBd)			\
	(u32)((sizeof(XLlDma_Bd)+((Alignment)-1))&~((Alignment)-1))*(NumBd)


/****************************************************************************/
/**
* Return the total number of BDs allocated by this channel with
* XLlDma_BdRingCreate().
*
* @param  RingPtr is the BD ring to operate on.
*
* @return The total number of BDs allocated for this channel.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingGetCnt(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingGetCnt(RingPtr) ((RingPtr)->AllCnt)


/****************************************************************************/
/**
* Return the number of BDs allocatable with XLlDma_BdRingAlloc() for pre-
* processing.
*
* @param  RingPtr is the BD ring to operate on.
*
* @return The number of BDs currently allocatable.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingGetFreeCnt(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingGetFreeCnt(RingPtr)  ((RingPtr)->FreeCnt)


/****************************************************************************/
/**
* Snap shot the latest BD a BD ring is processing.
*
* @param  RingPtr is the BD ring to operate on.
*
* @return None
*
* @note
* C-style signature:
*    void XLlDma_mBdRingSnapShotCurrBd(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingSnapShotCurrBd(RingPtr)				  \
	{								  \
		(RingPtr)->BdaRestart = 				  \
			(XLlDma_Bd *)XLlDma_mReadReg((RingPtr)->ChanBase, \
					XLLDMA_CDESC_OFFSET);		  \
	}


/****************************************************************************/
/**
* Return the next BD in the ring.
*
* @param  RingPtr is the BD ring to operate on.
* @param  BdPtr is the current BD.
*
* @return The next BD in the ring relative to the BdPtr parameter.
*
* @note
* C-style signature:
*    XLlDma_Bd *XLlDma_mBdRingNext(XLlDma_BdRing* RingPtr, XLlDma_Bd *BdPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingNext(RingPtr, BdPtr)			\
		(((u32)(BdPtr) >= (RingPtr)->LastBdAddr) ?	\
			(XLlDma_Bd*)(RingPtr)->FirstBdAddr :	\
			(XLlDma_Bd*)((u32)(BdPtr) + (RingPtr)->Separation))


/****************************************************************************/
/**
* Return the previous BD in the ring.
*
* @param  InstancePtr is the DMA channel to operate on.
* @param  BdPtr is the current BD.
*
* @return The previous BD in the ring relative to the BdPtr parameter.
*
* @note
* C-style signature:
*    XLlDma_Bd *XLlDma_mBdRingPrev(XLlDma_BdRing* RingPtr, XLlDma_Bd *BdPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingPrev(RingPtr, BdPtr)				\
		(((u32)(BdPtr) <= (RingPtr)->FirstBdAddr) ?		\
			(XLlDma_Bd*)(RingPtr)->LastBdAddr :		\
			(XLlDma_Bd*)((u32)(BdPtr) - (RingPtr)->Separation))

/****************************************************************************/
/**
* Retrieve the contents of the channel status register XLLDMA_SR_OFFSET
*
* @param  RingPtr is the channel instance to operate on.
*
* @return Current contents of SR_OFFSET
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingGetSr(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingGetSr(RingPtr)				\
		XLlDma_mReadReg((RingPtr)->ChanBase, XLLDMA_SR_OFFSET)


/****************************************************************************/
/**
* Retrieve the contents of the channel control register XLLDMA_CR_OFFSET
*
* @param  RingPtr is the channel instance to operate on.
*
* @return Current contents of CR_OFFSET
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingGetCr(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingGetCr(RingPtr)				\
		XLlDma_mReadReg((RingPtr)->ChanBase, XLLDMA_CR_OFFSET)


/****************************************************************************/
/**
* Set the contents of the channel control register XLLDMA_CR_OFFSET. This
* register does not affect the other DMA channel.
*
* @param  RingPtr is the channel instance to operate on.
* @param  Data is the data to write to CR_OFFSET
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingSetCr(XLlDma_BdRing* RingPtr, u32 Data)
*
*****************************************************************************/
#define XLlDma_mBdRingSetCr(RingPtr, Data)				\
		XLlDma_mWriteReg((RingPtr)->ChanBase, XLLDMA_CR_OFFSET, (Data))


/****************************************************************************/
/**
* Check if the current DMA channel is busy with a DMA operation.
*
* @param  RingPtr is the channel instance to operate on.
*
* @return TRUE if the DMA is busy. FALSE otherwise
*
* @note
* C-style signature:
*    XBoolean XLlDma_mBdRingBusy(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingBusy(RingPtr)					 \
		((XLlDma_mReadReg((RingPtr)->ChanBase, XLLDMA_SR_OFFSET) \
			& XLLDMA_SR_ENGINE_BUSY_MASK) ? TRUE : FALSE)


/****************************************************************************/
/**
* Set interrupt enable bits for a channel. This operation will modify the
* XLLDMA_CR_OFFSET register.
*
* @param  RingPtr is the channel instance to operate on.
* @param  Mask consists of the interrupt signals to enable. They are formed by
*         OR'ing one or more of the following bitmasks together:
*         XLLDMA_CR_IRQ_EN_MASK, XLLDMA_CR_IRQ_ERROR_EN_MASK,
*         XLLDMA_CR_IRQ_DELAY_EN_MASK, XLLDMA_CR_IRQ_COALESCE_EN_MASK and
*         XLLDMA_CR_IRQ_ALL_EN_MASK. Bits not specified in the mask are not
*         affected.
*
* @note
* C-style signature:
*    void XLlDma_mBdRingIntEnable(XLlDma_BdRing* RingPtr, u32 Mask)
*
*****************************************************************************/
#define XLlDma_mBdRingIntEnable(RingPtr, Mask)			\
	{							\
		u32 Reg = XLlDma_mReadReg((RingPtr)->ChanBase,	\
				XLLDMA_CR_OFFSET);		\
		Reg |= ((Mask) & XLLDMA_CR_IRQ_ALL_EN_MASK);	\
		XLlDma_mWriteReg((RingPtr)->ChanBase, XLLDMA_CR_OFFSET, Reg);\
	}


/****************************************************************************/
/**
* Clear interrupt enable bits for a channel. This operation will modify the
* XLLDMA_CR_OFFSET register.
*
* @param  RingPtr is the channel instance to operate on.
* @param  Mask consists of the interrupt signals to disable. They are formed
*         by OR'ing one or more of the following bitmasks together:
*         XLLDMA_CR_IRQ_EN_MASK, XLLDMA_CR_IRQ_ERROR_EN_MASK,
*         XLLDMA_CR_IRQ_DELAY_EN_MASK, XLLDMA_CR_IRQ_COALESCE_EN_MASK and
*         XLLDMA_CR_IRQ_ALL_EN_MASK. Bits not specified in the mask are not
*         affected.
*
* @note
* C-style signature:
*    void XLlDma_mBdRingIntDisable(XLlDma_BdRing* RingPtr, u32 Mask)
*
*****************************************************************************/
#define XLlDma_mBdRingIntDisable(RingPtr, Mask)				\
	{								\
		u32 Reg = XLlDma_mReadReg((RingPtr)->ChanBase,		\
				XLLDMA_CR_OFFSET);			\
		Reg &= ~((Mask) & XLLDMA_CR_IRQ_ALL_EN_MASK);		\
		XLlDma_mWriteReg((RingPtr)->ChanBase, XLLDMA_CR_OFFSET, Reg);\
	}


/****************************************************************************/
/**
* Get enabled interrupts of a channel.
*
* @param  RingPtr is the channel instance to operate on.
* @return Enabled interrupts of a channel. Use XLLDMA_CR_IRQ_* defined in
*         xlldma_hw.h to interpret this returned value.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingIntGetEnabled(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingIntGetEnabled(RingPtr)				\
		(XLlDma_mReadReg((RingPtr)->ChanBase, XLLDMA_CR_OFFSET) \
			& XLLDMA_CR_IRQ_ALL_EN_MASK)


/****************************************************************************/
/**
* Retrieve the contents of the channel's IRQ register XDMACR_IRQ_OFFSET. This
* operation can be used to see which interrupts are pending.
*
* @param  RingPtr is the channel instance to operate on.
*
* @return Current contents of the IRQ_OFFSET register. Use XLLDMA_IRQ_***
*         values defined in xlldma_hw.h to interpret the returned value.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingGetIrq(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingGetIrq(RingPtr)				\
		XLlDma_mReadReg((RingPtr)->ChanBase, XLLDMA_IRQ_OFFSET)


/****************************************************************************/
/**
* Acknowledge asserted interrupts.
*
* @param  RingPtr is the channel instance to operate on.
* @param  Mask are the interrupt signals to acknowledge and are made by Or'ing
*         one or more of the following bits: XLLDMA_IRQ_ERROR_MASK,
*         XLLDMA_IRQ_DELAY_MASK, XLLDMA_IRQ_COALESCE_MASK, XLLDMA_IRQ_ALL_MASK.
*         Any mask bit set for an unasserted interrupt has no effect.
*
* @note
* C-style signature:
*    u32 XLlDma_mBdRingAckIrq(XLlDma_BdRing* RingPtr)
*
*****************************************************************************/
#define XLlDma_mBdRingAckIrq(RingPtr, Mask)				\
		XLlDma_mWriteReg((RingPtr)->ChanBase, XLLDMA_IRQ_OFFSET,\
			(Mask) & XLLDMA_IRQ_ALL_MASK)

/************************* Function Prototypes ******************************/

/*
 * Descriptor ring functions xlldma_bdring.c
 */
int XLlDma_BdRingCreate(XLlDma_BdRing * RingPtr, u32 PhysAddr,
			u32 VirtAddr, u32 Alignment, unsigned BdCount);
int XLlDma_BdRingCheck(XLlDma_BdRing * RingPtr);
int XLlDma_BdRingClone(XLlDma_BdRing * RingPtr, XLlDma_Bd * SrcBdPtr);
int XLlDma_BdRingAlloc(XLlDma_BdRing * RingPtr, unsigned NumBd,
		       XLlDma_Bd ** BdSetPtr);
int XLlDma_BdRingUnAlloc(XLlDma_BdRing * RingPtr, unsigned NumBd,
			 XLlDma_Bd * BdSetPtr);
int XLlDma_BdRingToHw(XLlDma_BdRing * RingPtr, unsigned NumBd,
		      XLlDma_Bd * BdSetPtr);
unsigned XLlDma_BdRingFromHw(XLlDma_BdRing * RingPtr, unsigned BdLimit,
			     XLlDma_Bd ** BdSetPtr);
int XLlDma_BdRingFree(XLlDma_BdRing * RingPtr, unsigned NumBd,
		      XLlDma_Bd * BdSetPtr);
int XLlDma_BdRingStart(XLlDma_BdRing * RingPtr);
int XLlDma_BdRingSetCoalesce(XLlDma_BdRing * RingPtr, u32 Counter, u32 Timer);
void XLlDma_BdRingGetCoalesce(XLlDma_BdRing * RingPtr,
			      u32 *CounterPtr, u32 *TimerPtr);
#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
