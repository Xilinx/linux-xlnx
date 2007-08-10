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
*       (c) Copyright 2006 Xilinx Inc.
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
* @file xdmav3.c
*
* This file implements initialization and control related functions. For more
* information on this driver, see xdmav3.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 3.00a rmm  03/11/05 First release
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>
#include <asm/delay.h>

#include "xdmav3.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
* This function initializes a DMA channel.  This function must be called
* prior to using a DMA channel. Initialization of a channel includes setting
* up the register base address, setting up the instance data, and ensuring the
* HW is in a quiescent state.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param BaseAddress is where the registers for this channel can be found.
*        If address translation is being used, then this parameter must
*        reflect the virtual base address.
*
* @return
* - XST_SUCCESS if initialization was successful
*
******************************************************************************/
int XDmaV3_Initialize(XDmaV3 * InstancePtr, u32 BaseAddress)
{
	u32 Dmasr;

	/* Setup the instance */
	memset(InstancePtr, 0, sizeof(XDmaV3));
	InstancePtr->RegBase = BaseAddress;
	InstancePtr->IsReady = XCOMPONENT_IS_READY;
	InstancePtr->BdRing.RunState = XST_DMA_SG_IS_STOPPED;

	/* If this is SGDMA channel, then make sure it is stopped */
	Dmasr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (Dmasr & (XDMAV3_DMASR_DMACNFG_SGDMARX_MASK |
		     XDMAV3_DMASR_DMACNFG_SGDMATX_MASK |
		     XDMAV3_DMASR_DMACNFG_SSGDMA_MASK)) {
		XDmaV3_SgStop(InstancePtr);
	}

	return (XST_SUCCESS);
}
