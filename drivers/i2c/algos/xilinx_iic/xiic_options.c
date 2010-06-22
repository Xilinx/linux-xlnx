/* $Id: xiic_options.c,v 1.1.2.1 2010/04/12 12:13:14 svemula Exp $ */
/******************************************************************************
*
* (c) Copyright 2002-2009 Xilinx, Inc. All rights reserved.
*
* This file contains confidential and proprietary information of Xilinx, Inc.
* and is protected under U.S. and international copyright and other
* intellectual property laws.
*
* DISCLAIMER
* This disclaimer is not a license and does not grant any rights to the
* materials distributed herewith. Except as otherwise provided in a valid
* license issued to you by Xilinx, and to the maximum extent permitted by
* applicable law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND WITH ALL
* FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES AND CONDITIONS, EXPRESS,
* IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF
* MERCHANTABILITY, NON-INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE;
* and (2) Xilinx shall not be liable (whether in contract or tort, including
* negligence, or under any other theory of liability) for any loss or damage
* of any kind or nature related to, arising under or in connection with these
* materials, including for any direct, or any indirect, special, incidental,
* or consequential loss or damage (including loss of data, profits, goodwill,
* or any type of loss or damage suffered as a result of any action brought by
* a third party) even if such damage or loss was reasonably foreseeable or
* Xilinx had been advised of the possibility of the same.
*
* CRITICAL APPLICATIONS
* Xilinx products are not designed or intended to be fail-safe, or for use in
* any application requiring fail-safe performance, such as life-support or
* safety devices or systems, Class III medical devices, nuclear facilities,
* applications related to the deployment of airbags, or any other applications
* that could lead to death, personal injury, or severe property or
* environmental damage (individually and collectively, "Critical
* Applications"). Customer assumes the sole risk and liability of any use of
* Xilinx products in Critical Applications, subject only to applicable laws
* and regulations governing limitations on product liability.
*
* THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS PART OF THIS FILE
* AT ALL TIMES.
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
* 1.13a wgr 03/22/07 Converted to new coding style.
* 2.00a ktn 10/22/09 Converted all register accesses to 32 bit access.
*		     Updated to use the HAL APIs/macros.
* </pre>
*
****************************************************************************/

/***************************** Include Files *******************************/

#include "xiic.h"
#include "xiic_i.h"

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
* @param	InstancePtr is a pointer to the XIic instance to be worked on.
* @param	NewOptions are the options to be set.  See xiic.h for a list of
*		the available options.
*
* @return	None.
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
void XIic_SetOptions(XIic *InstancePtr, u32 NewOptions)
{
	u32 CntlReg;

	Xil_AssertVoid(InstancePtr != NULL);

	XIic_IntrGlobalDisable(InstancePtr->BaseAddress);

	/*
	 * Update the options in the instance and get the contents of the
	 * control register such that the general call option can be modified.
	 */
	InstancePtr->Options = NewOptions;
	CntlReg = XIic_ReadReg(InstancePtr->BaseAddress, XIIC_CR_REG_OFFSET);

	/*
	 * The general call option is the only option that maps directly to
	 * a hardware register feature.
	 */
	if (NewOptions & XII_GENERAL_CALL_OPTION) {
		CntlReg |= XIIC_CR_GENERAL_CALL_MASK;
	} else {
		CntlReg &= ~XIIC_CR_GENERAL_CALL_MASK;
	}

	/*
	 * Write the new control register value to the register.
	 */
	XIic_WriteReg(InstancePtr->BaseAddress, XIIC_CR_REG_OFFSET, CntlReg);

	XIic_IntrGlobalEnable(InstancePtr->BaseAddress);
}

/*****************************************************************************/
/**
*
* This function gets the current options for the IIC device. Options control
* the how the device behaves on the IIC bus. See SetOptions for more information
* on options.
*
* @param	InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return	The options of the IIC device. See xiic.h for a list of
*		available options.
*
* @note
*
* Options enabled will have a 1 in its appropriate bit position.
*
****************************************************************************/
u32 XIic_GetOptions(XIic *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	return InstancePtr->Options;
}
