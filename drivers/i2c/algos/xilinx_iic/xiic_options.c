/* $Id: xiic_options.c,v 1.1 2007/12/03 15:44:58 meinelte Exp $ */
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
* @file xiic_options.c
*
* Contains options functions for the XIic component. This file is not required
* unless the functions in this file are called.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- --- ------- -----------------------------------------------
* 1.01b jhl 3/26/02 repartioned the driver
* 1.01c ecm 12/05/02 new rev
* 1.13a wgr  03/22/07 Converted to new coding style.
* </pre>
*
****************************************************************************/

/***************************** Include Files *******************************/

#include "xiic.h"
#include "xiic_i.h"
#include "xio.h"

/************************** Constant Definitions ***************************/


/**************************** Type Definitions *****************************/


/***************** Macros (Inline Functions) Definitions *******************/


/************************** Function Prototypes ****************************/


/************************** Variable Definitions **************************/


/*****************************************************************************/
/**
*
* This function sets the options for the IIC device driver. The options control
* how the device behaves relative to the IIC bus. If an option applies to
* how messages are sent or received on the IIC bus, it must be set prior to
* calling functions which send or receive data.
*
* To set multiple options, the values must be ORed together. To not change
* existing options, read/modify/write with the current options using
* XIic_GetOptions().
*
* <b>USAGE EXAMPLE:</b>
*
* Read/modify/write to enable repeated start:
* <pre>
*   u8 Options;
*   Options = XIic_GetOptions(&Iic);
*   XIic_SetOptions(&Iic, Options | XII_REPEATED_START_OPTION);
* </pre>
*
* Disabling General Call:
* <pre>
*   Options = XIic_GetOptions(&Iic);
*   XIic_SetOptions(&Iic, Options &= ~XII_GENERAL_CALL_OPTION);
* </pre>
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @param    NewOptions are the options to be set.  See xiic.h for a list of
*           the available options.
*
* @return
*
* None.
*
* @note
*
* Sending or receiving messages with repeated start enabled, and then
* disabling repeated start, will not take effect until another master
* transaction is completed. i.e. After using repeated start, the bus will
* continue to be throttled after repeated start is disabled until a master
* transaction occurs allowing the IIC to release the bus.
* <br><br>
* Options enabled will have a 1 in its appropriate bit position.
*
****************************************************************************/
void XIic_SetOptions(XIic * InstancePtr, u32 NewOptions)
{
	u8 CntlReg;

	XASSERT_VOID(InstancePtr != NULL);

	XIic_mEnterCriticalRegion(InstancePtr->BaseAddress);

	/* Update the options in the instance and get the contents of the control
	 * register such that the general call option can be modified
	 */
	InstancePtr->Options = NewOptions;
	CntlReg = XIo_In8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET);

	/* The general call option is the only option that maps directly to
	 * a hardware register feature
	 */
	if (NewOptions & XII_GENERAL_CALL_OPTION) {
		CntlReg |= XIIC_CR_GENERAL_CALL_MASK;
	}
	else {
		CntlReg &= ~XIIC_CR_GENERAL_CALL_MASK;
	}

	/* Write the new control register value to the register */

	XIo_Out8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET, CntlReg);

	XIic_mExitCriticalRegion(InstancePtr->BaseAddress);
}

/*****************************************************************************/
/**
*
* This function gets the current options for the IIC device. Options control
* the how the device behaves on the IIC bus. See SetOptions for more information
* on options.
*
* @param    InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return
*
* The options of the IIC device. See xiic.h for a list of available options.
*
* @note
*
* Options enabled will have a 1 in its appropriate bit position.
*
****************************************************************************/
u32 XIic_GetOptions(XIic * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);

	return InstancePtr->Options;
}
