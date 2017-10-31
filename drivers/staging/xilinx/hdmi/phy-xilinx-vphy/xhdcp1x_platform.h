/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xhdcp1x_platform.h
* @addtogroup hdcp1x_v4_0
* @{
*
* This file provides the interface for the hdcp platform integration module
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  fidus  07/16/15 Initial release.
* </pre>
*
******************************************************************************/

#ifndef XHDCP1X_PLATFORM_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP1X_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xhdcp1x.h"
#include "xil_types.h"

/************************** Constant Definitions *****************************/

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

int XHdcp1x_PlatformIsKsvRevoked(const XHdcp1x *InstancePtr, u64 Ksv);
int XHdcp1x_PlatformTimerStart(XHdcp1x *InstancePtr, u16 TimeoutInMs);
int XHdcp1x_PlatformTimerStop(XHdcp1x *InstancePtr);
int XHdcp1x_PlatformTimerBusy(XHdcp1x *InstancePtr, u16 DelayInMs);

#ifdef __cplusplus
}
#endif

#endif  /* XHDCP1X_PLATFORM_H */
/** @} */
