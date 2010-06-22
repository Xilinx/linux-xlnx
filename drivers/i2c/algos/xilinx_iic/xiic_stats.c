/* $Id: xiic_stats.c,v 1.1.2.1 2010/04/12 12:13:14 svemula Exp $ */
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
* @file xiic_stats.c
*
* Contains statistics functions for the XIic component.
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
*		     XIic_ClearStats function is updated as the
*		     macro XIIC_CLEAR_STATS has been removed.
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
* Gets a copy of the statistics for an IIC device.
*
* @param	InstancePtr is a pointer to the XIic instance to be worked on.
* @param	StatsPtr is a pointer to a XIicStats structure which will get a
*		copy of current statistics.
*
* @return	None.
*
* @note		None.
*
****************************************************************************/
void XIic_GetStats(XIic *InstancePtr, XIicStats * StatsPtr)
{
	u8 NumBytes;
	u8 *SrcPtr;
	u8 *DestPtr;

	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(StatsPtr != NULL);

	/*
	 * Setup pointers to copy the stats structure
	 */
	SrcPtr = (u8 *) &InstancePtr->Stats;
	DestPtr = (u8 *) StatsPtr;

	/*
	 * Copy the current statistics to the structure passed in
	 */
	for (NumBytes = 0; NumBytes < sizeof(XIicStats); NumBytes++) {
		*DestPtr++ = *SrcPtr++;
	}
}

/*****************************************************************************/
/**
*
* Clears the statistics for the IIC device by zeroing all counts.
*
* @param	InstancePtr is a pointer to the XIic instance to be worked on.
*
* @return	None.
*
* @note		None.
*
****************************************************************************/
void XIic_ClearStats(XIic *InstancePtr)
{
	u8 NumBytes;
	u8 *DestPtr;

	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(InstancePtr != NULL);

	DestPtr = (u8 *)&InstancePtr->Stats;
	for (NumBytes = 0; NumBytes < sizeof(XIicStats); NumBytes++) {
		*DestPtr++ = 0;
	}

}
