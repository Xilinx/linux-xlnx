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
* @file xhdcp22_cipher_sinit.c
* @addtogroup hdcp22_cipher_v1_1
* @{
* @details
*
* This file contains the static initialization methods for the Xilinx
* HDCP 2.2 Cipher core.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     10/01/15 Initial release.
* </pre>
*
******************************************************************************/


/***************************** Include Files *********************************/
#include "xhdcp22_cipher.h"


/************************** Constant Definitions *****************************/
#ifndef XPAR_XHDCP22_CIPHER_NUM_INSTANCES
#define XPAR_XHDCP22_CIPHER_NUM_INSTANCES 0
#endif

/***************** Macros (Inline Functions) Definitions *********************/

/**************************** Type Definitions *******************************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/
extern XHdcp22_Cipher_Config XHdcp22_Cipher_ConfigTable[];

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
*
* This function returns a reference to an XHdcp22_Cipher_Config structure based
* on the core id, <i>DeviceId</i>. The return value will refer to an entry in
* the device configuration table defined in the xhdcp22_cipher_g.c file.
*
* @param  DeviceId is the unique core ID of the XHDCP22 Cipher core for the
*         lookup operation.
*
* @return XHdcp22Cipher_LookupConfig returns a reference to a config record
*         in the configuration table (in xhdcp22_cipher_g.c) corresponding
*         to <i>DeviceId</i>, or NULL if no match is found.
*
* @note   None.
*
******************************************************************************/
XHdcp22_Cipher_Config *XHdcp22Cipher_LookupConfig(u16 DeviceId)
{

	XHdcp22_Cipher_Config *CfgPtr = NULL;
	u32 Index;

	/* Checking for device id for which instance it is matching */
	for (Index = (u32)0x0; Index < (u32)(XPAR_XHDCP22_CIPHER_NUM_INSTANCES);
		Index++) {

		/* Assigning address of config table if both device ids
		 * are matched
		 */
		if (XHdcp22_Cipher_ConfigTable[Index].DeviceId == DeviceId) {
			CfgPtr = &XHdcp22_Cipher_ConfigTable[Index];
			break;
		}
	}

	return (XHdcp22_Cipher_Config *)CfgPtr;
}

/** @} */
