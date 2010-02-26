/* $Id: xsysace_intr.c,v 1.1 2006/02/17 21:52:36 moleres Exp $ */
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
*       (c) Copyright 2002 Xilinx Inc.
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
* @file xsysace_intr.c
*
* Contains functions related to System ACE interrupt mode. The driver's
* interrupt handler, XSysAce_InterruptHandler(), must be connected by the
* user to the interrupt controller.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  06/17/02 work in progress
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xsysace.h"
#include "xsysace_l.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Variable Definitions *****************************/


/************************** Function Prototypes ******************************/

static void HandleDataBuffer(XSysAce * InstancePtr, u32 StatusReg);
static void DataComplete(XSysAce * InstancePtr);


/*****************************************************************************/
/**
*
* Enable System ACE interrupts. There are three interrupts that can be enabled.
* The error interrupt enable serves as the driver's means to determine whether
* interrupts have been enabled or not. The configuration-done interrupt is not
* enabled here, instead it is enabled during a reset - which can cause a
* configuration process to start. The data-buffer-ready interrupt is not enabled
* here either. It is enabled when a read or write operation is started. The
* reason for not enabling the latter two interrupts are because the status bits
* may be set as a leftover of an earlier occurrence of the interrupt.
*
* @param InstancePtr is a pointer to the XSysAce instance to work on.
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
void XSysAce_EnableInterrupt(XSysAce * InstancePtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* By default, enable only the error interrupt */
	XSysAce_mOrControlReg(InstancePtr->BaseAddress, XSA_CR_ERRORIRQ_MASK);

	/* Clear the reset on the interrupt line if it was in reset */
	XSysAce_mAndControlReg(InstancePtr->BaseAddress, ~XSA_CR_RESETIRQ_MASK);
}


/*****************************************************************************/
/**
*
* Disable all System ACE interrupts and hold the interrupt request line of
* the device in reset.
*
* @param InstancePtr is a pointer to the XSysAce instance that just interrupted.
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
void XSysAce_DisableInterrupt(XSysAce * InstancePtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Put the interrupt request line in reset */
	XSysAce_mOrControlReg(InstancePtr->BaseAddress, XSA_CR_RESETIRQ_MASK);

	/* Clear the interrupt enable bits */
	XSysAce_mAndControlReg(InstancePtr->BaseAddress,
			       ~(XSA_CR_DATARDYIRQ_MASK | XSA_CR_ERRORIRQ_MASK |
				 XSA_CR_CFGDONEIRQ_MASK));
}


/*****************************************************************************/
/**
*
* The interrupt handler for the System ACE driver. This handler must be
* connected by the user to an interrupt controller or source. This function
* does not save or restore context.
*
* This function continues reading or writing to the compact flash if such an
* operation is in progress, and notifies the upper layer software through
* the event handler once the operation is complete or an error occurs. On an
* error, any command currently in progress is aborted.
*
* @param InstancePtr is a pointer to the XSysAce instance that just interrupted.
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
void XSysAce_InterruptHandler(void *InstancePtr)
{
	u32 StatusReg;
	XSysAce *AcePtr = (XSysAce *) InstancePtr;

	XASSERT_VOID(InstancePtr != NULL);

	/*
	 * Get the status in order to process each interrupt that has occurred
	 */
	StatusReg = XSysAce_mGetStatusReg(AcePtr->BaseAddress);

	/*
	 * Reset the interrupt line to effectively clear the interrupt conditions.
	 * We need to set the bit to clear the interrupts, then clear the bit so
	 * that new interrupts can be generated.
	 */
	XSysAce_mOrControlReg(AcePtr->BaseAddress, XSA_CR_RESETIRQ_MASK);
	XSysAce_mAndControlReg(AcePtr->BaseAddress, ~XSA_CR_RESETIRQ_MASK);

	/*
	 * Check for data buffer ready, which means an operation (either read or
	 * write) is in progress.
	 */
	if (StatusReg & XSA_SR_DATABUFRDY_MASK) {
		/*
		 * Handles the data buffer, and invokes the callback to the user for
		 * data transfer completion.
		 */
		HandleDataBuffer(AcePtr, StatusReg);
	}

	/*
	 * Check for completion of JTAG configuration and report the event up.
	 * We only do this if the CFGDONE interrupt is enabled since the CFGDONE
	 * status only gets cleared when the confguration controller is reset,
	 * which we do not do unless requested by the user because it may cause
	 * a configuration process to start. We could have gotten into this
	 * interrupt handler by another interrupt, yet have a leftover CFGDONE
	 * status from an earlier configuration process.
	 */
	if ((StatusReg & XSA_SR_CFGDONE_MASK) &&
	    (XSysAce_mGetControlReg(AcePtr->BaseAddress) &
	     XSA_CR_CFGDONEIRQ_MASK)) {
		/*
		 * Clear the bit indicating MPU is the source of configuration data
		 * since we're done configuring from the MPU for now. Also clear the
		 * force CFGMODE bit and the CFGSTART bit, basically undoing what was
		 * done in XSysAce_ProgramChain(). Disable the interrupts since the
		 * CFGDONE status does not get cleared unless a reset occurs - and in
		 * the meantime we may get into this interrupt handler again.
		 */
		XSysAce_mAndControlReg(AcePtr->BaseAddress,
				       ~(XSA_CR_CFGSEL_MASK |
					 XSA_CR_CFGSTART_MASK |
					 XSA_CR_CFGDONEIRQ_MASK |
					 XSA_CR_DATARDYIRQ_MASK |
					 XSA_CR_FORCECFGMODE_MASK));

		AcePtr->EventHandler(AcePtr->EventRef, XSA_EVENT_CFG_DONE);
	}

	/*
	 * Check for errors and report the event (the user is responsible for
	 * retrieving and interpreting the errors). We only do this if the error
	 * interrupt is enabled since the error status only gets cleared when the
	 * CompactFlash or confguration controller is reset, which we do not do
	 * because it may cause a configuration process to start. We could have
	 * entered this interrupt handler by another interrupt and have a leftover
	 * error status from a previous error.
	 */
	if ((StatusReg & (XSA_SR_CFGERROR_MASK | XSA_SR_CFCERROR_MASK)) &&
	    (XSysAce_mGetControlReg(AcePtr->BaseAddress) &
	     XSA_CR_ERRORIRQ_MASK)) {
		/* Clear the transfer state to effectively abort the operation */
		AcePtr->NumRequested = 0;
		AcePtr->NumRemaining = 0;
		AcePtr->BufferPtr = NULL;

		/*
		 * Disable the error interrupt since the only way to clear the
		 * error status is to reset the CF or the configuration controller,
		 * neither of which we want to do here since the consequences may
		 * be undesirable (i.e., may cause a reconfiguration). The user
		 * will need to perform the reset based on the error event.
		 */
		XSysAce_mAndControlReg(AcePtr->BaseAddress,
				       ~XSA_CR_ERRORIRQ_MASK);

		AcePtr->EventHandler(AcePtr->EventRef, XSA_EVENT_ERROR);
	}
}

/*****************************************************************************/
/**
*
* Set the callback function for handling events. The upper layer software
* should call this function during initialization. The events are passed
* asynchronously to the upper layer software. The events are described in
* xsysace.h and are named XSA_EVENT_*.
*
* Note that the callback is invoked by the driver within interrupt context, so
* it needs to do its job quickly. If there are potentially slow operations
* within the callback, these should be done at task-level.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param FuncPtr is the pointer to the callback function.
* @param CallBackRef is a reference pointer to be passed back to the upper
*        layer.
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
void XSysAce_SetEventHandler(XSysAce * InstancePtr,
			     XSysAce_EventHandler FuncPtr, void *CallBackRef)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(FuncPtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	InstancePtr->EventHandler = FuncPtr;
	InstancePtr->EventRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Handle a data-buffer-ready interrupt. If we get the interrupt when reading,
* it means there is still data to read since the interrupt does not occur after
* reading the last data buffer. If we get the interrupt when writing, there
* may or may not be data left to write since the interrupt does occur after the
* last data buffer is written.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param StatusReg is the contents of the status register, read at the start
*        of the interrupt service routine.
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
static void HandleDataBuffer(XSysAce * InstancePtr, u32 StatusReg)
{
	/* By default, transfer a whole data buffer */
	int BytesToTransfer = XSA_DATA_BUFFER_SIZE;

	/*
	 * Check to see if number of bytes remaining is less than the data buffer
	 * size. If it is, we need to adjust the remaining bytes to transfer.
	 */
	if (InstancePtr->NumRemaining < XSA_DATA_BUFFER_SIZE) {
		BytesToTransfer = InstancePtr->NumRemaining;
	}

	/*
	 * Transfer only one data buffer at a time, which is 32 bytes. Note that
	 * errors will be handled by an error interrupt occurring, so no need to
	 * check for them here.
	 */
	if (StatusReg & XSA_SR_DATABUFMODE_MASK) {
		/*
		 * A write operation in progress, so if there is data remaining then
		 * write the buffer. If no data is remaining, clean up.
		 */
		if (InstancePtr->NumRemaining > 0) {
			(void) XSysAce_WriteDataBuffer(InstancePtr->BaseAddress,
						       InstancePtr->BufferPtr,
						       BytesToTransfer);

			/*
			 * Decrement the number of bytes remaining to be transferred and
			 * adjust the buffer pointer appropriately.
			 */
			InstancePtr->NumRemaining -= BytesToTransfer;
			InstancePtr->BufferPtr += BytesToTransfer;
		}
		else {
			/* Done writing data, so clean up */
			DataComplete(InstancePtr);
		}
	}
	else {
		/* A read operation in progress, so read the buffer */
		(void) XSysAce_ReadDataBuffer(InstancePtr->BaseAddress,
					      InstancePtr->BufferPtr,
					      BytesToTransfer);

		/*
		 * Decrement the number of bytes remaining to be transferred and
		 * adjust the buffer pointer appropriately. If it was the last buffer,
		 * we're done and we can cleanup.
		 */
		InstancePtr->NumRemaining -= BytesToTransfer;
		InstancePtr->BufferPtr += BytesToTransfer;

		if (InstancePtr->NumRemaining == 0) {
			/* Done reading data, so clean up */
			DataComplete(InstancePtr);
		}
	}
}

/*****************************************************************************/
/**
*
* Handle cleanup when a data transfer is complete. This means intializing the
* state variables, disabling the data-buffer-ready interrupt, and sending the
* event to the user.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
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
static void DataComplete(XSysAce * InstancePtr)
{
	InstancePtr->NumRequested = 0;
	InstancePtr->NumRemaining = 0;
	InstancePtr->BufferPtr = NULL;

	/*
	 * Disable the data-buffer-ready interrupt. This isn't necessary when
	 * reading since the DATABUFRDY status bit is cleared by the ACE after
	 * the last data buffer is read.  However, the ACE isn't currently
	 * smart enough to clear the DATABUFRDY status bit after the last data
	 * buffer is written during a write operation.  So, we need to use the
	 * enable/disable interrupt bit to control its usefulness.
	 */
	XSysAce_mAndControlReg(InstancePtr->BaseAddress,
			       ~XSA_CR_DATARDYIRQ_MASK);

	/*
	 * The same code is executed for JTAG configuration as well as CompactFlash
	 * transfers, so we need to distinguish between JTAG config done and CF
	 * data transfer done. We look at the CFGSEL value in the control register
	 * to determine if an MPU JTAG config process has just completed. The
	 * CFG_DONE event is passed up later by the main interrupt handler.
	 */
	if ((XSysAce_mGetControlReg(InstancePtr->BaseAddress)
	     & XSA_CR_CFGSEL_MASK) == 0) {
		/* no JTAG configuration in progress */
		InstancePtr->EventHandler(InstancePtr->EventRef,
					  XSA_EVENT_DATA_DONE);
	}
}
