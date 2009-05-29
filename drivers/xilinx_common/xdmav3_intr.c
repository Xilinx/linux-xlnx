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
* @file xdmav3_intr.c
*
* This file implements interrupt control related functions. For more
* information on this driver, see xdmav3.h.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 3.00a rmm  03/11/06 First release
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include "xdmav3.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
* Set the interrupt status register for this channel. Use this function
* to ack pending interrupts.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param Mask is a logical OR of XDMAV3_IPXR_*_MASK constants found in
*        xdmav3_l.h.
*
******************************************************************************/
void XDmaV3_SetInterruptStatus(XDmaV3 * InstancePtr, u32 Mask)
{
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_ISR_OFFSET, Mask);
}


/*****************************************************************************/
/**
* Retrieve the interrupt status for this channel. OR the results of this
* function with results from XDmaV3_GetInterruptEnable() to determine which
* interrupts are currently pending to the processor.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
* @return Mask of interrupt bits made up of XDMAV3_IPXR_*_MASK constants found
*         in xdmav3_l.h.
*
******************************************************************************/
u32 XDmaV3_GetInterruptStatus(XDmaV3 * InstancePtr)
{
	return (XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_ISR_OFFSET));
}


/*****************************************************************************/
/**
* Enable specific DMA interrupts.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param Mask is a logical OR of of XDMAV3_IPXR_*_MASK constants found in
*        xdmav3_l.h.
*
******************************************************************************/
void XDmaV3_SetInterruptEnable(XDmaV3 * InstancePtr, u32 Mask)
{
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_IER_OFFSET, Mask);
}


/*****************************************************************************/
/**
* Retrieve the interrupt enable register for this channel. Use this function to
* determine which interrupts are currently enabled to the processor.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
* @return Mask of interrupt bits made up of XDMAV3_IPXR_*_MASK constants found in
*         xdmav3_l.h.
*
******************************************************************************/
u32 XDmaV3_GetInterruptEnable(XDmaV3 * InstancePtr)
{
	return (XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_IER_OFFSET));
}
