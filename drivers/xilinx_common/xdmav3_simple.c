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
* @file xdmav3_simple.c
*
* This file implements Simple DMA related functions. For more
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
* Initiate a simple DMA transfer. The BD argument sets the parameters of the
* transfer. Since the BD is also used for SG DMA transfers, some fields of the
* BD will be ignored. The following BD macros will have no effect on the
* transfer:
*
* - XDmaBdV3_mSetLast()
* - XDmaBdV3_mClearLast()
* - XDmaBdV3_mSetBdPage()
*
* To determine when the transfer has completed, the user can poll the device
* with XDmaV3_mGetStatus() and test the XDMAV3_DMASR_DMABSY_MASK bit, or wait for
* an interrupt. When the DMA operation has completed, the outcome of the
* transfer can be retrieved by calling XDmaV3_mGetStatus() and testing for DMA
* bus errors bits.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param BdPtr sets the parameters of the transfer.
*
* @return
* - XST_SUCCESS if the transfer was initated
* - XST_DEVICE_BUSY if a transfer is already in progress
*
******************************************************************************/
int XDmaV3_SimpleTransfer(XDmaV3 * InstancePtr, XDmaBdV3 * BdPtr)
{
	u32 Dmasr;

	/* Is the channel busy */
	Dmasr = XDmaV3_mReadReg(InstancePtr->RegBase, XDMAV3_DMASR_OFFSET);
	if (Dmasr & (XDMAV3_DMASR_DMABSY_MASK | XDMAV3_DMASR_SGBSY_MASK)) {
		return (XST_DEVICE_BUSY);
	}

	/* Copy BdPtr fields into the appropriate HW registers */

	/* DMACR: SGS bit is set always. This is done in case the transfer
	 * occurs on a SGDMA channel and will prevent the HW from fetching the
	 * next BD.
	 */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_DMACR_OFFSET,
			 XDmaV3_mReadBd(BdPtr, XDMAV3_BD_DMACR_OFFSET)
			 | XDMAV3_DMACR_SGS_MASK);

	/* MSBA */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_MSBA_OFFSET,
			 XDmaV3_mReadBd(BdPtr, XDMAV3_BD_MSBA_OFFSET));

	/* LSBA */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_LSBA_OFFSET,
			 XDmaV3_mReadBd(BdPtr, XDMAV3_BD_LSBA_OFFSET));

	/* LENGTH: Writing this register starts HW */
	XDmaV3_mWriteReg(InstancePtr->RegBase, XDMAV3_LENGTH_OFFSET,
			 XDmaV3_mReadBd(BdPtr, XDMAV3_BD_LENGTH_OFFSET));

	return (XST_SUCCESS);
}
