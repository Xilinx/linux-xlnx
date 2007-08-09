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
* @file xlldma.c
*
* This file implements initialization and control related functions. For more
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

#include "xlldma.h"
#include "xenv.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
 * This function initializes a DMA engine.  This function must be called
 * prior to using a DMA engine. Initialization of a engine includes setting
 * up the register base address, setting up the instance data, and ensuring the
 * hardware is in a quiescent state.
 *
 * @param  InstancePtr is a pointer to the DMA engine instance to be worked on.
 * @param  BaseAddress is where the registers for this engine can be found.
 *         If address translation is being used, then this parameter must
 *         reflect the virtual base address.
 * @return None.
 *
 *****************************************************************************/
void XLlDma_Initialize(XLlDma * InstancePtr, u32 BaseAddress)
{
	/* Setup the instance */
	memset(InstancePtr, 0, sizeof(XLlDma));
	InstancePtr->RegBase = BaseAddress;

	/* Initialize the ring structures */
	InstancePtr->TxBdRing.RunState = XST_DMA_SG_IS_STOPPED;
	InstancePtr->TxBdRing.ChanBase = BaseAddress + XLLDMA_TX_OFFSET;
	InstancePtr->TxBdRing.IsRxChannel = 0;
	InstancePtr->RxBdRing.RunState = XST_DMA_SG_IS_STOPPED;
	InstancePtr->RxBdRing.ChanBase = BaseAddress + XLLDMA_RX_OFFSET;
	InstancePtr->RxBdRing.IsRxChannel = 1;

	/* Reset the device and return */
	XLlDma_Reset(InstancePtr);
}

/*****************************************************************************/
/**
* Reset both TX and RX channels of a DMA engine.
*
* Any DMA transaction in progress aborts immediately. The DMA engine is in
* stop state after the reset.
*
* @param  InstancePtr is a pointer to the DMA engine instance to be worked on.
*
* @return None.
*
* @note
*         - If the hardware is not working properly, this function will enter
*           infinite loop and never return.
*         - After the reset, the Normal mode is enabled, and the overflow error
*           for both TX/RX channels are disabled.
*         - After the reset, the DMA engine is no longer in pausing state, if
*           the DMA engine is paused before the reset operation.
*         - After the reset, the coalescing count value and the delay timeout
*           value are both set to 1 for TX and RX channels.
*         - After the reset, all interrupts are disabled.
*
******************************************************************************/
void XLlDma_Reset(XLlDma * InstancePtr)
{
	u32 IrqStatus;
	XLlDma_BdRing *TxRingPtr, *RxRingPtr;

	TxRingPtr = &XLlDma_mGetTxRing(InstancePtr);
	RxRingPtr = &XLlDma_mGetRxRing(InstancePtr);

	/* Save the locations of current BDs both rings are working on
	 * before the reset so later we can resume the rings smoothly.
	 */
	XLlDma_mBdRingSnapShotCurrBd(TxRingPtr);
	XLlDma_mBdRingSnapShotCurrBd(RxRingPtr);

	/* Start reset process then wait for completion */
	XLlDma_mSetCr(InstancePtr, XLLDMA_DMACR_SW_RESET_MASK);

	/* Loop until the reset is done */
	while ((XLlDma_mGetCr(InstancePtr) & XLLDMA_DMACR_SW_RESET_MASK)) {
	}

	/* Disable all interrupts after issue software reset */
	XLlDma_mBdRingIntDisable(TxRingPtr, XLLDMA_CR_IRQ_ALL_EN_MASK);
	XLlDma_mBdRingIntDisable(RxRingPtr, XLLDMA_CR_IRQ_ALL_EN_MASK);

	/* Clear Interrupt registers of both channels, as the software reset
	 * does not clear any register values. Not doing so will cause
	 * interrupts asserted after the software reset if there is any
	 * interrupt left over before.
	 */
	IrqStatus = XLlDma_mBdRingGetIrq(TxRingPtr);
	XLlDma_mBdRingAckIrq(TxRingPtr, IrqStatus);
	IrqStatus = XLlDma_mBdRingGetIrq(RxRingPtr);
	XLlDma_mBdRingAckIrq(RxRingPtr, IrqStatus);

	/* Enable Normal mode, and disable overflow errors for both channels */
	XLlDma_mSetCr(InstancePtr, XLLDMA_DMACR_TAIL_PTR_EN_MASK |
		      XLLDMA_DMACR_RX_OVERFLOW_ERR_DIS_MASK |
		      XLLDMA_DMACR_TX_OVERFLOW_ERR_DIS_MASK);

	/* Set TX/RX Channel coalescing setting */
	XLlDma_BdRingSetCoalesce(TxRingPtr, 1, 1);
	XLlDma_BdRingSetCoalesce(RxRingPtr, 1, 1);

	TxRingPtr->RunState = XST_DMA_SG_IS_STOPPED;
	RxRingPtr->RunState = XST_DMA_SG_IS_STOPPED;
}

/*****************************************************************************/
/**
* Pause DMA transactions on both channels. The DMA enters the pausing state
* immediately. So if a DMA transaction is in progress, it will be left
* unfinished and will be continued once the DMA engine is resumed
* (see XLlDma_Resume()).
*
* @param  InstancePtr is a pointer to the DMA engine instance to be worked on.
*
* @return None.
*
* @note
*       - If the hardware is not working properly, this function will enter
*         infinite loop and never return.
*       - After the DMA is paused, DMA channels still could accept more BDs
*         from software (see XLlDma_BdRingToHw()), but new BDs will not be
*         processed until the DMA is resumed (see XLlDma_Resume()).
*
*****************************************************************************/
void XLlDma_Pause(XLlDma * InstancePtr)
{
	u32 RegValue;
	XLlDma_BdRing *TxRingPtr, *RxRingPtr;

	TxRingPtr = &XLlDma_mGetTxRing(InstancePtr);
	RxRingPtr = &XLlDma_mGetRxRing(InstancePtr);

	/* Do nothing if both channels already stopped */
	if ((TxRingPtr->RunState == XST_DMA_SG_IS_STOPPED) &&
	    (RxRingPtr->RunState == XST_DMA_SG_IS_STOPPED)) {
		return;
	}

	/* Enable pause bits for both TX/ RX channels */
	RegValue = XLlDma_mGetCr(InstancePtr);
	XLlDma_mSetCr(InstancePtr, RegValue | XLLDMA_DMACR_TX_PAUSE_MASK |
		      XLLDMA_DMACR_RX_PAUSE_MASK);

	/* Loop until Write Command Queue of RX channel is empty, which
	 * indicates that all the write data associated with the pending
	 * commands has been flushed.*/
	while (!(XLlDma_mBdRingGetIrq(RxRingPtr) | XLLDMA_IRQ_WRQ_EMPTY_MASK));

	TxRingPtr->RunState = XST_DMA_SG_IS_STOPPED;
	RxRingPtr->RunState = XST_DMA_SG_IS_STOPPED;
}

/*****************************************************************************/
/**
* Resume DMA transactions on both channels. Any interrupted DMA transaction
* caused by DMA pause operation (see XLlDma_Pause()) and all committed
* transactions after DMA is paused will be continued upon the return of this
* function.
*
* @param  InstancePtr is a pointer to the DMA engine instance to be worked on.
*
* @return None.
*
*****************************************************************************/
void XLlDma_Resume(XLlDma * InstancePtr)
{
	u32 RegValue;
	XLlDma_BdRing *TxRingPtr, *RxRingPtr;

	TxRingPtr = &XLlDma_mGetTxRing(InstancePtr);
	RxRingPtr = &XLlDma_mGetRxRing(InstancePtr);

	/* Do nothing if both channels already started */
	if ((TxRingPtr->RunState == XST_DMA_SG_IS_STARTED) &&
	    (RxRingPtr->RunState == XST_DMA_SG_IS_STARTED)) {
		return;
	}

	/* Clear pause bits for both TX/ RX channels */
	RegValue = XLlDma_mGetCr(InstancePtr);
	XLlDma_mSetCr(InstancePtr, RegValue & ~(XLLDMA_DMACR_TX_PAUSE_MASK |
						XLLDMA_DMACR_RX_PAUSE_MASK));

	TxRingPtr->RunState = XST_DMA_SG_IS_STARTED;
	RxRingPtr->RunState = XST_DMA_SG_IS_STARTED;
}
