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
* @file xhdcp1x_debug.h
* @addtogroup hdcp1x_v4_0
* @{
*
* This file provides the interface of the HDCP debug commands
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

#ifndef XHDCP1X_DEBUG_H
/**< Prevent circular inclusions by using protection macros */
#define XHDCP1X_DEBUG_H

#include "xdebug.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xhdcp1x.h"

/************************** Constant Definitions *****************************/

/***************** Macros (Inline Functions) Definitions *********************/

#define XHDCP1X_DEBUG_LOGMSG if (XHdcp1xDebugLogMsg != NULL) XHdcp1xDebugLogMsg
 /**< Instance of the function interface used for debug log messages. */

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

/************************* External Declarations *****************************/

extern XHdcp1x_LogMsg XHdcp1xDebugLogMsg;	/**< Instance of function
						  *  interface used for debug
						  *  log message statement */

#ifdef __cplusplus
}
#endif

#endif  /* XHDCP1X_DEBUG_H */
/** @} */
