/* $Id: xsysace_jtagcfg.c,v 1.1 2006/02/17 21:52:36 moleres Exp $ */
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
* @file xsysace_jtagcfg.c
*
* Contains functions to control the configuration of the target FPGA chain on
* the System ACE via the JTAG configuration port.
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


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
*
* Reset the JTAG configuration controller. This comprises a reset of the JTAG
* configuration controller and the CompactFlash controller (if it is currently
* being accessed by the configuration controller). Note that the MPU controller
* is not reset, meaning the MPU registers remain unchanged. The configuration
* controller is reset then released from reset in this function.
*
* The CFGDONE status (and therefore interrupt) is cleared when the configuration
* controller is reset. If interrupts have been enabled, we go ahead and enable
* the CFGDONE interrupt here. This means that if and when a configuration
* process starts as a result of this reset, an interrupt will be received when
* it is complete.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* None.
*
* @note
*
* This function is not thread-safe.
*
******************************************************************************/
void XSysAce_ResetCfg(XSysAce * InstancePtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Reset the configuration controller */
	XSysAce_mOrControlReg(InstancePtr->BaseAddress, XSA_CR_CFGRESET_MASK);

	/*
	 * If in interrupt mode, enable the CFGDONE and error interrupts.
	 * A reset clears the CFGDONE and error statuses, so we're going to
	 * re-enable the interrupts here so any new errors or CFGDONEs will be
	 * caught.
	 */
	if (XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		XSysAce_mOrControlReg(InstancePtr->BaseAddress,
				      XSA_CR_CFGDONEIRQ_MASK |
				      XSA_CR_ERRORIRQ_MASK);
	}

	/* Release the reset of the configuration controller */
	XSysAce_mAndControlReg(InstancePtr->BaseAddress, ~XSA_CR_CFGRESET_MASK);
}

/*****************************************************************************/
/**
*
* Select the configuration address (or file) from the CompactFlash to be used
* for configuration of the target FPGA chain.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param Address is the address or file number to be used as the bitstream to
*        configure the target FPGA devices. There are 8 possible files, so
*        the value of this parameter can range from 0 to 7.
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
void XSysAce_SetCfgAddr(XSysAce * InstancePtr, unsigned int Address)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(Address < 8);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Set the control register with the address and the bit that forces
	 * the use of the control register address bits instead of the address
	 * pins on the device.
	 */
	XSysAce_mSetCfgAddr(InstancePtr->BaseAddress, Address);
}

/*****************************************************************************/
/**
*
* Set the start mode for configuration of the target FPGA chain from
* CompactFlash. The configuration process only starts after a reset. The
* user can indicate that the configuration should start immediately after a
* reset, or the configuration process can be delayed until the user commands
* it to start (using this function). The configuration controller can be
* reset using XSysAce_ResetCfg().
*
* The user can select which configuration file on the CompactFlash to use using
* the XSysAce_SetCfgAddr() function. If the user intends to configure the target
* FPGA chain directly from the MPU port, this function is not needed. Instead,
* the user would simply call XSysAce_ProgramChain().
*
* The user can use XSysAce_IsCfgDone() when in polled mode to determine if
* the configuration is complete. If in interrupt mode, the event
* XSA_EVENT_CFG_DONE will be returned asynchronously to the user when the
* configuration is complete. The user must call XSysAce_EnableInterrupt() to put
* the device/driver into interrupt mode.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param ImmedOnReset can be set to TRUE to indicate the configuration process
*        will start immediately after a reset of the ACE configuration
*        controller, or it can be set to FALSE to indicate the configuration
*        process is delayed after a reset until the user starts it (using this
*        function).
* @param StartCfg is a boolean indicating whether to start the configuration
*        process or not. When ImmedOnReset is set to TRUE, this value is
*        ignored. When ImmedOnReset is set to FALSE, then this value controls
*        when the configuration process is started. When set to TRUE the
*        configuration process starts (assuming a reset of the device has
*        occurred), and when set to FALSE the configuration process does not
*        start.
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
void XSysAce_SetStartMode(XSysAce * InstancePtr, u32 ImmedOnReset, u32 StartCfg)
{
	u32 Control;

	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Get the current contents of the control register */
	Control = XSysAce_mGetControlReg(InstancePtr->BaseAddress);

	/*
	 * Since the user has called this function, we want to tell the ACE
	 * controller to look at the CFGMODE bit of the control register rather
	 * than the CFGMODE pin of the device to determine when to start a
	 * configuration process.
	 */
	Control |= XSA_CR_FORCECFGMODE_MASK;

	/* Set or clear the CFGMODE bit of the control register */
	if (ImmedOnReset) {
		Control |= XSA_CR_CFGMODE_MASK;	/* immediate on reset */
	}
	else {
		Control &= ~XSA_CR_CFGMODE_MASK;	/* wait for start bit */
	}


	/* Set or clear the CFGSTART bit of the control register */
	if (StartCfg) {
		Control |= XSA_CR_CFGSTART_MASK;
	}
	else {
		Control &= ~XSA_CR_CFGSTART_MASK;
	}

	XSysAce_mSetControlReg(InstancePtr->BaseAddress, Control);
}


/*****************************************************************************/
/**
*
* Program the target FPGA chain through the configuration JTAG port. This
* allows the user to program the devices on the target FPGA chain from the MPU
* port instead of from CompactFlash. The user specifies a buffer and the number
* of bytes to write. The buffer should be equivalent to an ACE (.ace) file.
*
* Note that when loading the ACE file via the MPU port, the first sector of the
* ACE file is discarded. The CF filesystem controller in the System ACE device
* knows to skip the first sector when the ACE file comes from the CF, but the
* CF filesystem controller is bypassed when the ACE file comes from the MPU
* port. For this reason, this function skips the first sector of the buffer
* passed in.
*
* In polled mode, the write is blocking. In interrupt mode, the write is
* non-blocking and an event, XSA_EVENT_CFG_DONE, is returned to the user in
* the asynchronous event handler when the configuration is complete.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param BufferPtr is a pointer to a buffer that will be used to program
*        the configuration JTAG devices.
* @param NumBytes is the number of bytes in the buffer. We assume that there
*        is at least one sector of data in the .ace file, which is the
*        information sector.
*
* @return
*
* - XST_SUCCESS if the write was successful. In interrupt mode, this does not
*   mean the write is complete, only that it has begun. An event is returned
*   to the user when the write is complete.
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_FAILURE if an error occurred during the write. The user should call
*   XSysAce_GetErrors() to determine the cause of the error.
*
* @note
*
* None.
*
* @internal
*
* The System ACE controller has a 32-byte buffer which holds data. The entire
* buffer must be written to ensure that it gets sent to the configuration
* JTAG port. If the number of bytes specified by the user is not a multiple
* of 32, the driver will pad the remaining bytes of the System ACE buffer with
* zeroes in order to write the entire buffer.
*
******************************************************************************/
int XSysAce_ProgramChain(XSysAce * InstancePtr, u8 *BufferPtr, int NumBytes)
{
	u32 ControlMask;
	int BytesToSend;
	int NumSent;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(NumBytes > XSA_CF_SECTOR_SIZE);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/*
	 * Set up the configuration controller to use the MPU port as the
	 * source of configuration data (instead of the CF). The following
	 * control flow comes directly from the System ACE specification, except
	 * the reset was moved to after the other control register bits are set.
	 * Putting it into reset before the bits are set seemed to produce
	 * configuration errors occasionally.
	 */
	ControlMask = XSysAce_mGetControlReg(InstancePtr->BaseAddress);

	/* Select MPU as the source */
	ControlMask |= XSA_CR_CFGSEL_MASK;
	XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

	/* Tell controller to wait for start bit from MPU */
	ControlMask |= XSA_CR_FORCECFGMODE_MASK;
	ControlMask &= ~XSA_CR_CFGMODE_MASK;
	XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

	/* Set the start bit */
	ControlMask |= XSA_CR_CFGSTART_MASK;
	XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

	/* Put the configuration controller into a reset condition */
	ControlMask |= XSA_CR_CFGRESET_MASK;
	XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

	/* Clear the reset condition, which starts the process */
	ControlMask &= ~XSA_CR_CFGRESET_MASK;
	XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

	/*
	 * Set up number of bytes to send. Default to the entire buffer, which
	 * will be true in polled mode. In interrupt mode, modify this value to
	 * send only one data buffer of data.  Always skip the first sector per
	 * the comment above.
	 */
	BytesToSend = NumBytes - XSA_CF_SECTOR_SIZE;

	/*
	 * The number of bytes to write depends on interrupt or polled mode
	 */
	if (XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		/*
		 * In interrupt mode, so enable the data-buffer-ready and
		 * configuration-done interrupts. Also, set up the state variables for
		 * the interrupt handler to transfer the remaining data after the
		 * initial write below. We need to write one data buffer here in this
		 * function in order to cause the data-buffer-ready interrupt to occur
		 * for subsequent writes.
		 */
		ControlMask |= XSA_CR_DATARDYIRQ_MASK | XSA_CR_CFGDONEIRQ_MASK;
		XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);

		/* Send only one data buffer to begin with (if there is that much) */
		if (BytesToSend > XSA_DATA_BUFFER_SIZE) {
			BytesToSend = XSA_DATA_BUFFER_SIZE;
		}

		/*
		 * Setup state variables for the interrupt handler. Skip the first
		 * sector per the comment above, and also skip the first data buffer
		 * since it is written below.
		 */
		InstancePtr->NumRequested = NumBytes - XSA_CF_SECTOR_SIZE;
		InstancePtr->BufferPtr =
			BufferPtr + XSA_CF_SECTOR_SIZE + BytesToSend;
		InstancePtr->NumRemaining =
			NumBytes - XSA_CF_SECTOR_SIZE - BytesToSend;
	}

	NumSent = XSysAce_WriteDataBuffer(InstancePtr->BaseAddress,
					  BufferPtr + XSA_CF_SECTOR_SIZE,
					  BytesToSend);
	if (NumSent != BytesToSend) {
		/* an error occurred, report this to the user */
		return XST_FAILURE;
	}

	/*
	 * If in polled mode, restore the control register to the way it was
	 */
	if (!XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		/*
		 * Unselect MPU as the source, tell controller to use CFGMODE pin,
		 * and clear the start bit.
		 */
		ControlMask &= ~(XSA_CR_CFGSEL_MASK | XSA_CR_FORCECFGMODE_MASK |
				 XSA_CR_CFGSTART_MASK);
		XSysAce_mSetControlReg(InstancePtr->BaseAddress, ControlMask);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Check to see if the configuration of the target FPGA chain is complete. This
* function is typically only used in polled mode. In interrupt mode, an event
* (XSA_EVENT_CFG_DONE) is returned to the user in the asynchronous event
* handler.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* TRUE if the configuration is complete. FALSE otherwise.
*
* @note
*
* None.
*
******************************************************************************/
u32 XSysAce_IsCfgDone(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Use the layer 0 macro by the same name */

	return XSysAce_mIsCfgDone(InstancePtr->BaseAddress);
}


/*****************************************************************************/
/**
*
* Get the sector ID of the CompactFlash sector being used for configuration of
* the target FPGA chain. This sector ID (or logical block address) only affects
* transfers between the ACE configuration logic and the CompactFlash card.
* This function is typically used for debug purposes to determine which sector
* was being accessed when an error occurred.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* The sector ID (logical block address) being used for data transfers between
* the ACE configuration logic and the CompactFlash. Sector IDs range from 0
* to 0x10000000.
*
* @note
*
* None.
*
******************************************************************************/
u32 XSysAce_GetCfgSector(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return XSysAce_RegRead32(InstancePtr->BaseAddress + XSA_CLR_OFFSET);
}
