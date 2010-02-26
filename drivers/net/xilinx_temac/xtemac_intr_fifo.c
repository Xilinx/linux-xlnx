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
* @file xtemac_intr_fifo.c
*
* Functions in this file implement interrupt related operations for
* FIFO direct frame transfer mode. See xtemac.h for a detailed description of
* the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Removed simple DMA, added auto-negotiate handling,
*                     removed XST_SEND_ERROR reporting
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac.h"
#include "xtemac_i.h"
#include "xio.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/* shortcut macros */
#define ERR_HANDLER(Class, Word1, Word2)  \
    InstancePtr->ErrorHandler(InstancePtr->ErrorRef, Class, Word1, Word2)

#define FIFOSEND_HANDLER(Cnt)  \
    InstancePtr->FifoSendHandler(InstancePtr->FifoSendRef, Cnt)

#define FIFORECV_HANDLER()  \
    InstancePtr->FifoRecvHandler(InstancePtr->FifoRecvRef)

#define ANEG_HANDLER()  \
    InstancePtr->AnegHandler(InstancePtr->AnegRef)


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
 *
 * Enable FIFO related interrupts for FIFO direct frame transfer mode. Dma
 * interrupts are not affected.
 *
 * Do not use this function when using SG DMA frame transfer mode.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction specifies whether the transmit related (XTE_SEND) or
 *        receive related (XTE_RECV) interrupts should be affected, or
 *        both (XTE_SEND | XTE_RECV).
 *
 * @return None
 *
 * @note The state of the transmitter and receiver are not modified by this
 *       function.
 *
 * @note If the device is configured for SGDMA, then this function has no
 *       effect. Use XTemac_IntrSgDmaDisable() instead.
 *
 ******************************************************************************/
void XTemac_IntrFifoEnable(XTemac *InstancePtr, u32 Direction)
{
	u32 RegIPIER;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(!(Direction & ~(XTE_SEND | XTE_RECV)));

	/* Don't allow if device is configured for SGDMA */
	if (XTemac_mIsSgDma(InstancePtr)) {
		return;
	}

	/* Get contents of IPIER register */
	RegIPIER = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);

	/* Handle send direction */
	if (Direction & XTE_SEND) {
		RegIPIER |=
			(XTE_IPXR_XMIT_ERROR_MASK | XTE_IPXR_XMIT_DONE_MASK);
		InstancePtr->Flags |= XTE_FLAGS_SEND_FIFO_INT_ENABLE;

		/* Don't allow Tx status overrun interrupt if option is cleared */
		if (!
		    (InstancePtr->
		     Options & XTE_REPORT_TXSTATUS_OVERRUN_OPTION)) {
			RegIPIER &= ~XTE_IPXR_XMIT_SFIFO_OVER_MASK;;
		}
	}

	/* Handle receive direction */
	if (Direction & XTE_RECV) {
		RegIPIER |=
			(XTE_IPXR_RECV_ERROR_MASK | XTE_IPXR_RECV_DONE_MASK);
		InstancePtr->Flags |= XTE_FLAGS_RECV_FIFO_INT_ENABLE;

		/* Don't enable recv reject errors if option is cleared */
		if (!(InstancePtr->Options & XTE_REPORT_RXERR_OPTION)) {
			RegIPIER &= ~XTE_IPXR_RECV_DROPPED_MASK;
		}
	}

	/* Update IPIER with new setting */
	XTemac_mSetIpifReg(XTE_IPIER_OFFSET, RegIPIER);
}


/*****************************************************************************/
/**
 *
 * Disable FIFO related interrupts for FIFO direct frame transfer mode. Dma
 * interrupts are not affected.
 *
 * Do not use this function when using SG DMA frame transfer mode.
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Direction specifies whether the transmit related (XTE_SEND) or
 *        receive related (XTE_RECV) interrupts should be affected, or
 *        both (XTE_SEND | XTE_RECV).
 *
 * @return None
 *
 * @note The state of the transmitter and receiver are not modified by this
 *       function.
 *
 * @note If the device is configured for SGDMA, then this function has no
 *       effect. Use XTemac_IntrSgDmaDisable() instead.
 *
 ******************************************************************************/
void XTemac_IntrFifoDisable(XTemac *InstancePtr, u32 Direction)
{
	u32 RegIPIER;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID(!(Direction & ~(XTE_SEND | XTE_RECV)));

	/* Don't allow if device is configured for SGDMA */
	if (XTemac_mIsSgDma(InstancePtr)) {
		return;
	}

	/* Get contents of IPIER register */
	RegIPIER = XTemac_mGetIpifReg(XTE_IPIER_OFFSET);

	/* Handle send direction */
	if (Direction & XTE_SEND) {
		RegIPIER &=
			~(XTE_IPXR_XMIT_ERROR_MASK | XTE_IPXR_XMIT_DONE_MASK);
		InstancePtr->Flags &= ~XTE_FLAGS_SEND_FIFO_INT_ENABLE;
	}

	/* Handle receive direction */
	if (Direction & XTE_RECV) {
		RegIPIER &=
			~(XTE_IPXR_RECV_ERROR_MASK | XTE_IPXR_RECV_DONE_MASK);
		InstancePtr->Flags &= ~XTE_FLAGS_RECV_FIFO_INT_ENABLE;
	}

	/* Update IPIER with new setting */
	XTemac_mSetIpifReg(XTE_IPIER_OFFSET, RegIPIER);
}



/*****************************************************************************/
/**
 *
 * Master interrupt handler for FIFO direct frame transfer mode. This routine
 * will query the status of the device, bump statistics, and invoke user
 * callbacks in the following priority:
 *
 * This routine must be connected to an interrupt controller using OS/BSP
 * specific methods.
 *
 * @param InstancePtr is a pointer to the TEMAC instance that has caused the
 *        interrupt.
 *
 * @return None
 *
 ******************************************************************************/
void XTemac_IntrFifoHandler(void *TemacPtr)
{
	u32 RegDISR;
	u32 CorePending;
	u32 RegMisc;
	unsigned Cnt;
	XTemac *InstancePtr = (XTemac *) TemacPtr;

	XASSERT_VOID(InstancePtr != NULL);

	/* This ISR will try to handle as many interrupts as it can in a single
	 * call. However, in most of the places where the user's error handler is
	 * called, this ISR exits because it is expected that the user will reset
	 * the device most of the time.
	 */

	/* Log interrupt */
	XTemac_mBumpStats(Interrupts, 1);

	/* Get top level interrupt status. The status is self clearing when the
	 * interrupt source is cleared
	 */
	RegDISR = XTemac_mGetIpifReg(XTE_DISR_OFFSET);

	/* Handle IPIF and packet FIFO errors */
	if (RegDISR & (XTE_DXR_DPTO_MASK | XTE_DXR_TERR_MASK |
		       XTE_DXR_RECV_FIFO_MASK | XTE_DXR_SEND_FIFO_MASK)) {
		/* IPIF transaction or data phase error */
		if (RegDISR & (XTE_DXR_DPTO_MASK | XTE_DXR_TERR_MASK)) {
			XTemac_mBumpStats(IpifErrors, 1);
			ERR_HANDLER(XST_IPIF_ERROR, RegDISR, 0);
			return;
		}

		/* Receive packet FIFO is deadlocked */
		if (RegDISR & XTE_DXR_RECV_FIFO_MASK) {
			XTemac_mBumpStats(RxPktFifoErrors, 1);
			ERR_HANDLER(XST_PFIFO_DEADLOCK, XTE_RECV, 0);
			return;
		}

		/* Transmit packet FIFO is deadlocked */
		if (RegDISR & XTE_DXR_SEND_FIFO_MASK) {
			XTemac_mBumpStats(TxPktFifoErrors, 1);
			ERR_HANDLER(XST_PFIFO_DEADLOCK, XTE_SEND, 0);
			return;
		}
	}

	/* Handle core interrupts */
	if (RegDISR & XTE_DXR_CORE_MASK) {
		/* Calculate which enabled interrupts have been asserted */
		CorePending = XTemac_mGetIpifReg(XTE_IPIER_OFFSET) &
			XTemac_mGetIpifReg(XTE_IPISR_OFFSET);

		/* Check for fatal status/length FIFO errors. These errors can't be
		 * cleared
		 */
		if (CorePending & XTE_IPXR_FIFO_FATAL_ERROR_MASK) {
			XTemac_mBumpStats(FifoErrors, 1);
			ERR_HANDLER(XST_FIFO_ERROR, CorePending &
				    XTE_IPXR_FIFO_FATAL_ERROR_MASK, 0);
			return;
		}

		/* A receive packet has arrived. Call the receive handler.
		 *
		 * Acking this interrupt is not done here. The handler has a choice:
		 * 1) Call XTemac_FifoRecv() which will ack this interrupt source, or
		 * 2) Call XTemac_IntrFifoDisable() and defer XTEmac_FifoRecv() to a
		 * later time. Failure to do one of these actions will leave this
		 * interupt still pending resulting in an exception loop.
		 */
		if (CorePending & XTE_IPXR_RECV_DONE_MASK) {
			FIFORECV_HANDLER();
		}

		/* A transmit has completed. Pull off all statuses that are available.
		 * For each status that contains a non-fatal error, the error handler
		 * is invoked. For fatal errors, the error handler is invoked once and
		 * assumes the callback will reset the device.
		 *
		 * Unless there was a fatal error, then call the send handler since
		 * resources in the packet FIFO, transmit length FIFO, and transmit
		 * status FIFO have been freed up. This gives the handler a chance
		 * to enqueue new frame(s).
		 */
		if (CorePending & XTE_IPXR_XMIT_DONE_MASK) {
			Cnt = 0;

			/* While XMIT_DONE persists */
			do {
				/* Get TSR, try to clear XMIT_DONE */
				RegMisc = XTemac_mGetIpifReg(XTE_TSR_OFFSET);
				XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
						   XTE_IPXR_XMIT_DONE_MASK);

				Cnt++;

				/* Read IPISR and test XMIT_DONE again */
				RegMisc = XTemac_mGetIpifReg(XTE_IPISR_OFFSET);
			} while (RegMisc & XTE_IPXR_XMIT_DONE_MASK);

			FIFOSEND_HANDLER(Cnt);
		}

		/* Auto negotiation interrupt */
		if (CorePending & XTE_IPXR_AUTO_NEG_MASK) {
			ANEG_HANDLER();
		}

		/* Check for dropped receive frame. Ack the interupt then call the
		 * error handler
		 */
		if (CorePending & XTE_IPXR_RECV_DROPPED_MASK) {
			XTemac_mSetIpifReg(XTE_IPISR_OFFSET,
					   CorePending &
					   XTE_IPXR_RECV_DROPPED_MASK);

			XTemac_mBumpStats(RxRejectErrors, 1);
			ERR_HANDLER(XST_RECV_ERROR,
				    CorePending & XTE_IPXR_RECV_DROPPED_MASK,
				    0);

			/* no return here, nonfatal error */
		}
	}
}
