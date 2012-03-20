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
*       (c) Copyright 2005-2006 Xilinx Inc.
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
* @file xtemac_selftest.c
*
* Self-test and diagnostic functions of the XTemac driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Switched to local link DMA driver
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtemac.h"
#include "xtemac_i.h"

/************************** Constant Definitions *****************************/
#define XTE_IPIF_IP_INTR_COUNT 13

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
* Performs a self-test on the Ethernet device.  The test includes:
*   - Run self-test on DMA channel, FIFO, and IPIF components
*
* This self-test is destructive. On successful completion, the device is reset
* and returned to its default configuration. The caller is responsible for
* re-configuring the device after the self-test is run, and starting it when
* ready to send and receive frames.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
* @return
*
*  - XST_SUCCESS  Self-test was successful
*  - XST_FAILURE  Self-test failed
*
* @note
* There is the possibility that this function will not return if the hardware is
* broken (i.e., it never sets the status bit indicating that transmission is
* done). If this is of concern to the user, the user should provide protection
* from this problem - perhaps by using a different timer thread to monitor the
* self-test thread.
*
******************************************************************************/
int XTemac_SelfTest(XTemac *InstancePtr)
{
	int Result;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* Run self-test on the DMA (if configured) */
	if (XTemac_mIsSgDma(InstancePtr)) {
		Result = XDmaV3_SelfTest(&InstancePtr->RecvDma);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}

		Result = XDmaV3_SelfTest(&InstancePtr->SendDma);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}
	}

	/* Run self-test on packet fifos */
	if (XTemac_mIsFifo(InstancePtr)) {
		Result = XPacketFifoV200a_SelfTest(&InstancePtr->RecvFifo.Fifo,
						   XPF_V200A_READ_FIFO_TYPE);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}

		Result = XPacketFifoV200a_SelfTest(&InstancePtr->SendFifo.Fifo,
						   XPF_V200A_WRITE_FIFO_TYPE);
		if (Result != XST_SUCCESS) {
			return (XST_FAILURE);
		}
	}

	/* Run the IPIF self-test */
	Result = XIpIfV123b_SelfTest(InstancePtr->BaseAddress,
				     XTE_IPIF_IP_INTR_COUNT);
	if (Result != XST_SUCCESS) {
		return (XST_FAILURE);
	}

	/* Reset the Ethernet MAC to leave it in a known good state */
	XTemac_Reset(InstancePtr, XTE_NORESET_HARD);

	return (XST_SUCCESS);
}
