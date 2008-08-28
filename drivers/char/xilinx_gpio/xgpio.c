/* $Id: xgpio.c,v 1.1.2.1 2007/02/16 10:03:28 imanuilov Exp $ */
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
*       (c) Copyright 2002-2008 Xilinx Inc.
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
/**
* @file xgpio.c
*
* The implementation of the XGpio component's basic functionality. See xgpio.h
* for more information about the component.
*
* @note
*
* None
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rmm  02/04/02 First release
* 2.00a jhl  12/16/02 Update for dual channel and interrupt support
* 2.01a jvb  12/13/05 I changed Initialize() into CfgInitialize(), and made
*                     CfgInitialize() take a pointer to a config structure
*                     instead of a device id. I moved Initialize() into
*                     xgpio_sinit.c, and had Initialize() call CfgInitialize()
*                     after it retrieved the config structure using the device
*                     id. I removed include of xparameters.h along with any
*                     dependencies on xparameters.h and the _g.c config table.
*
* </pre>
*
*****************************************************************************/

/***************************** Include Files ********************************/

#include "xgpio.h"
#include "xstatus.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Variable Definitions ****************************/


/************************** Function Prototypes *****************************/


/****************************************************************************/
/**
* Initialize the XGpio instance provided by the caller based on the
* given configuration data.
*
* Nothing is done except to initialize the InstancePtr.
*
* @param InstancePtr is a pointer to an XGpio instance. The memory the pointer
*        references must be pre-allocated by the caller. Further calls to
*        manipulate the component through the XGpio API must be made with this
*        pointer.
*
* @param Config is a reference to a structure containing information about
*        a specific GPIO device. This function initializes an InstancePtr object
*        for a specific device specified by the contents of Config. This
*        function can initialize multiple instance objects with the use of
*        multiple calls giving different Config information on each call.
*
* @param EffectiveAddr is the device base address in the virtual memory address
*        space. The caller is responsible for keeping the address mapping
*        from EffectiveAddr to the device physical base address unchanged
*        once this function is invoked. Unexpected errors may occur if the
*        address mapping changes after this function is called. If address
*        translation is not used, use Config->BaseAddress for this parameters,
*        passing the physical address instead.
*
* @return
*
* - XST_SUCCESS           Initialization was successfull.
*
* @note
*
* None.
*
*****************************************************************************/
int XGpio_CfgInitialize(XGpio *InstancePtr, XGpio_Config *Config,
			u32 EffectiveAddr)
{
	/*
	 * Assert arguments
	 */
	XASSERT_NONVOID(InstancePtr != NULL);

	/*
	 * Set some default values.
	 */
	InstancePtr->BaseAddress = EffectiveAddr;
	InstancePtr->InterruptPresent = Config->InterruptPresent;
	InstancePtr->IsDual = Config->IsDual;

	/*
	 * Indicate the instance is now ready to use, initialized without error
	 */
	InstancePtr->IsReady = XCOMPONENT_IS_READY;
	return (XST_SUCCESS);
}


/****************************************************************************/
/**
* Set the input/output direction of all discrete signals for the specified
* GPIO channel.
*
* @param InstancePtr is a pointer to an XGpio instance to be worked on.
* @param Channel contains the channel of the GPIO (1 or 2) to operate on.
* @param DirectionMask is a bitmask specifying which discretes are input and
*        which are output. Bits set to 0 are output and bits set to 1 are input.
*
* @return
*
* None.
*
* @note
*
* The hardware must be built for dual channels if this function is used
* with any channel other than 1.  If it is not, this function will assert.
*
*****************************************************************************/
void XGpio_SetDataDirection(XGpio *InstancePtr, unsigned Channel,
			    u32 DirectionMask)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID((Channel == 1) ||
		     ((Channel == 2) && (InstancePtr->IsDual == TRUE)));

	XGpio_mWriteReg(InstancePtr->BaseAddress,
			((Channel - 1) * XGPIO_CHAN_OFFSET) + XGPIO_TRI_OFFSET,
			DirectionMask);
}


/****************************************************************************/
/**
* Read state of discretes for the specified GPIO channnel.
*
* @param InstancePtr is a pointer to an XGpio instance to be worked on.
* @param Channel contains the channel of the GPIO (1 or 2) to operate on.
*
* @return Current copy of the discretes register.
*
* @note
*
* The hardware must be built for dual channels if this function is used
* with any channel other than 1.  If it is not, this function will assert.
*
*****************************************************************************/
u32 XGpio_DiscreteRead(XGpio *InstancePtr, unsigned Channel)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_NONVOID((Channel == 1) ||
			((Channel == 2) && (InstancePtr->IsDual == TRUE)));

	return XGpio_mReadReg(InstancePtr->BaseAddress,
			      ((Channel - 1) * XGPIO_CHAN_OFFSET) +
			      XGPIO_DATA_OFFSET);
}

/****************************************************************************/
/**
* Write to discretes register for the specified GPIO channel.
*
* @param InstancePtr is a pointer to an XGpio instance to be worked on.
* @param Channel contains the channel of the GPIO (1 or 2) to operate on.
* @param Data is the value to be written to the discretes register.
*
* @return
*
* None.
*
* @note
*
* The hardware must be built for dual channels if this function is used
* with any channel other than 1.  If it is not, this function will assert.
* See also XGpio_DiscreteSet() and XGpio_DiscreteClear().
*
*****************************************************************************/
void XGpio_DiscreteWrite(XGpio *InstancePtr, unsigned Channel, u32 Data)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);
	XASSERT_VOID((Channel == 1) ||
		     ((Channel == 2) && (InstancePtr->IsDual == TRUE)));

	XGpio_mWriteReg(InstancePtr->BaseAddress,
			((Channel - 1) * XGPIO_CHAN_OFFSET) + XGPIO_DATA_OFFSET,
			Data);
}
