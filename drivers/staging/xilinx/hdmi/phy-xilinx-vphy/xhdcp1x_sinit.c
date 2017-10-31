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
* @file xhdcp1x_sinit.c
* @addtogroup hdcp1x_v4_0
* @{
*
* This file contains static initialization method for Xilinx HDCP driver
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

/***************************** Include Files *********************************/

#include "xhdcp1x.h"
#include "xhdcp1x_cipher.h"


/************************** Constant Definitions *****************************/

#ifndef XPAR_XHDCP_NUM_INSTANCES
#define XPAR_XHDCP_NUM_INSTANCES 0	/**< Number of HDCP Instances */
#endif

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

extern XHdcp1x_Config XHdcp1x_ConfigTable[];	/**< Instance of Lookup table
						  *  of HDCP instance(s) in
						  *  the design */

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
* This function returns a reference to an XHdcp1x_Config structure based on
* specified device ID.
*
* @param	DeviceId is the unique core ID of the HDCP interface.
*
* @return	A reference to the config record in the configuration table (in
*		xhdcp_g.c) corresponding the specified DeviceId. NULL if no
*		match is found.
*
* @note		None.
*
******************************************************************************/
XHdcp1x_Config *XHdcp1x_LookupConfig(u16 DeviceId)
{
	XHdcp1x_Config *CfgPtr = NULL;
	u32 Index;

	for (Index = 0; Index < XPAR_XHDCP_NUM_INSTANCES; Index++) {
		if (XHdcp1x_ConfigTable[Index].DeviceId == DeviceId) {
			CfgPtr = &XHdcp1x_ConfigTable[Index];
			break;
		}
	}

	return (CfgPtr);
}
/** @} */
