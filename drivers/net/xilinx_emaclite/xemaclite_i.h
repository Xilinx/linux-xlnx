/* $Id: xemaclite_i.h,v 1.1.2.1 2007/03/13 17:26:08 akondratenko Exp $: */
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
*       (c) Copyright 2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/******************************************************************************/
/**
* @file xemaclite_i.h
*
* This header file contains internal identifiers, which are those shared
* between the files of the driver. It is intended for internal use only.
*
* NOTES:
*
* None.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.01a ecm  05/21/04 First release
* </pre>
******************************************************************************/

#ifndef XEMACLITE_I_H		/* prevent circular inclusions */
#define XEMACLITE_I_H		/* by using protection macros */

/***************************** Include Files *********************************/

#include "xemaclite.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/****************************************************************************/
/**
*
* Get the TX active location to check status. This is used to check if
* the TX buffer is currently active. There isn't any way in the hardware
* to implement this but the register is fully populated so the driver can
* set the bit in the send routine and the ISR can clear the bit when
* the handler is complete. This mimics the correct operation of the hardware
* if it was possible to do this in hardware.
*
* @param    BaseAddress is the base address of the device
*
* @return   Contents of active bit in register.
*
* @note
* u32 XEmacLite_mGetTxActive(u32 BaseAddress)
*
*****************************************************************************/
#define XEmacLite_mGetTxActive(BaseAddress)                                  \
         (XIo_In32((BaseAddress) + XEL_TSR_OFFSET))

/****************************************************************************/
/**
*
* Set the TX active location to update status. This is used to set the bit
* indicating which TX buffer is currently active. There isn't any way in the
* hardware to implement this but the register is fully populated so the driver
* can set the bit in the send routine and the ISR can clear the bit when
* the handler is complete. This mimics the correct operation of the hardware
* if it was possible to do this in hardware.
*
* @param    BaseAddress is the base address of the device
* @param    Mask is the data to be written
*
* @return   None
*
* @note
* void XEmacLite_mSetTxActive(u32 BaseAddress, u32 Mask)
*
*****************************************************************************/
#define XEmacLite_mSetTxActive(BaseAddress, Mask)                            \
         (XIo_Out32((BaseAddress) + XEL_TSR_OFFSET, (Mask)))

/************************** Variable Definitions ****************************/

/************************** Function Prototypes ******************************/

void XEmacLite_AlignedWrite(void *SrcPtr, u32 *DestPtr, unsigned ByteCount);
void XEmacLite_AlignedRead(u32 *SrcPtr, void *DestPtr, unsigned ByteCount);

void StubHandler(void *CallBackRef);


#endif /* end of protection macro */
