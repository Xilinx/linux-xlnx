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
* @file xtemac_i.h
*
* This header file contains internal identifiers, which are those shared
* between XTemac components. The identifiers in this file are not intended for
* use external to the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Removed simple dma
* </pre>
*
******************************************************************************/

#ifndef XTEMAC_I_H		/* prevent circular inclusions */
#define XTEMAC_I_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xtemac.h"

/************************** Constant Definitions *****************************/


/* Internal flags kept in Instance's Flags attribute */
#define XTE_FLAGS_RECV_SGDMA_INT_ENABLE   0x0020
#define XTE_FLAGS_SEND_SGDMA_INT_ENABLE   0x0010
#define XTE_FLAGS_RECV_FIFO_INT_ENABLE    0x0002
#define XTE_FLAGS_SEND_FIFO_INT_ENABLE    0x0001

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************
* Statistics increment macros
* The referenced InstancePtr is an implicitly assumed parameter.
******************************************************************************/
#define XTemac_mBumpStats(Counter, Value) \
    InstancePtr->Stats.Counter += (Value);


/*****************************************************************************
* Register accessors.
*
* The goal of these four functions is to make the code look cleaner. These
* simply wrap to the level 0 macros defined in xtemac_l.h.
*
* The referenced InstancePtr is an implicitly assumed parameter.
*
******************************************************************************/
#define XTemac_mGetHostReg(RegOffset) \
    XTemac_mReadHostReg(InstancePtr->BaseAddress, RegOffset)

#define XTemac_mSetHostReg(RegOffset, Data) \
    XTemac_mWriteHostReg(InstancePtr->BaseAddress, RegOffset, Data)

#define XTemac_mGetIpifReg(RegOffset) \
    XTemac_mReadReg(InstancePtr->BaseAddress, RegOffset)

#define XTemac_mSetIpifReg(RegOffset, Data) \
    XTemac_mWriteReg(InstancePtr->BaseAddress, RegOffset, Data)



/************************** Function Prototypes ******************************/

int XTemac_ConfigureFifoAccess(XTemac *InstancePtr);

/************************** Variable Definitions *****************************/


#ifdef __cplusplus
}
#endif


#endif /* end of protection macro */
