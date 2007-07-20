/* $Id: xspi_options.c,v 1.1 2007/04/04 18:31:38 wre Exp $ */
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
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xspi_options.c
*
* Contains functions for the configuration of the XSpi driver component.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00b jhl  2/27/02  First release
* 1.00b rpm  04/25/02 Collapsed IPIF and reg base addresses into one
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xspi.h"
#include "xspi_i.h"
#include "xio.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/

/*
 * Create the table of options which are processed to get/set the device
 * options. These options are table driven to allow easy maintenance and
 * expansion of the options.
 */
typedef struct {
	u32 Option;
	u16 Mask;
} OptionsMap;

static OptionsMap OptionsTable[] = {
	{XSP_LOOPBACK_OPTION, XSP_CR_LOOPBACK_MASK},
	{XSP_CLK_ACTIVE_LOW_OPTION, XSP_CR_CLK_POLARITY_MASK},
	{XSP_CLK_PHASE_1_OPTION, XSP_CR_CLK_PHASE_MASK},
	{XSP_MASTER_OPTION, XSP_CR_MASTER_MODE_MASK},
	{XSP_MANUAL_SSELECT_OPTION, XSP_CR_MANUAL_SS_MASK}
};

#define XSP_NUM_OPTIONS      (sizeof(OptionsTable) / sizeof(OptionsMap))

/*****************************************************************************/
/**
*
* This function sets the options for the SPI device driver. The options control
* how the device behaves relative to the SPI bus. The device must be idle
* rather than busy transferring data before setting these device options.
*
* @param    InstancePtr is a pointer to the XSpi instance to be worked on.
* @param    Options contains the specified options to be set. This is a bit
*           mask where a 1 means to turn the option on, and a 0 means to turn
*           the option off. One or more bit values may be contained in the mask.
*           See the bit definitions named XSP_*_OPTIONS in the file xspi.h.
*
* @return
*
* XST_SUCCESS if options are successfully set.  Otherwise, returns:
* - XST_DEVICE_BUSY if the device is currently transferring data. The transfer
*   must complete or be aborted before setting options.
* - XST_SPI_SLAVE_ONLY if the caller attempted to configure a slave-only
*   device as a master.
*
* @note
*
* This function makes use of internal resources that are shared between the
* XSpi_Stop() and XSpi_SetOptions() functions. So if one task might be setting
* device options options while another is trying to stop the device, the user
* is required to provide protection of this shared data (typically using a
* semaphore).
*
******************************************************************************/
int XSpi_SetOptions(XSpi * InstancePtr, u32 Options)
{
	u16 ControlReg;
	u32 Index;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Do not allow the slave select to change while a transfer is in progress.
	 * No need to worry about a critical section here since even if the Isr
	 * changes the busy flag just after we read it, the function will return
	 * busy and the caller can retry when notified that their current transfer
	 * is done.
	 */
	if (InstancePtr->IsBusy) {
		return XST_DEVICE_BUSY;
	}
	/*
	 * Do not allow master option to be set if the device is slave only
	 */
	if ((Options & XSP_MASTER_OPTION) && (InstancePtr->SlaveOnly)) {
		return XST_SPI_SLAVE_ONLY;
	}

	ControlReg = XIo_In16(InstancePtr->BaseAddr + XSP_CR_OFFSET);

	/*
	 * Loop through the options table, turning the option on or off
	 * depending on whether the bit is set in the incoming options flag.
	 */
	for (Index = 0; Index < XSP_NUM_OPTIONS; Index++) {
		if (Options & OptionsTable[Index].Option) {
			ControlReg |= OptionsTable[Index].Mask;	/* turn it on */
		}
		else {
			ControlReg &= ~OptionsTable[Index].Mask;	/* turn it off */
		}
	}

	/*
	 * Now write the control register. Leave it to the upper layers
	 * to restart the device.
	 */
	XIo_Out16(InstancePtr->BaseAddr + XSP_CR_OFFSET, ControlReg);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function gets the options for the SPI device. The options control how
* the device behaves relative to the SPI bus.
*
* @param    InstancePtr is a pointer to the XSpi instance to be worked on.
*
* @return
*
* Options contains the specified options to be set. This is a bit mask where a
* 1 means to turn the option on, and a 0 means to turn the option off. One or
* more bit values may be contained in the mask. See the bit definitions named
* XSP_*_OPTIONS in the file xspi.h.
*
* @note
*
* None.
*
******************************************************************************/
u32 XSpi_GetOptions(XSpi * InstancePtr)
{
	u32 OptionsFlag = 0;
	u16 ControlReg;
	u32 Index;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/*
	 * Get the control register to determine which options are currently set.
	 */
	ControlReg = XIo_In16(InstancePtr->BaseAddr + XSP_CR_OFFSET);

	/*
	 * Loop through the options table to determine which options are set
	 */
	for (Index = 0; Index < XSP_NUM_OPTIONS; Index++) {
		if (ControlReg & OptionsTable[Index].Mask) {
			OptionsFlag |= OptionsTable[Index].Option;
		}
	}

	return OptionsFlag;
}
