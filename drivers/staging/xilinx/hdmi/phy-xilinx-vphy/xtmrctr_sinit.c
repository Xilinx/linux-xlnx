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
* @file xtmrctr_sinit.c
* @addtogroup tmrctr_v4_0
* @{
*
* This file contains static initialization methods for the XTmrCtr driver.
*
* @note	None.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 4.0   als  09/30/15 Creation of this file. Moved LookupConfig from xtmrctr.c.
* </pre>
*
******************************************************************************/

/******************************* Include Files *******************************/


#include "xtmrctr.h"

/************************** Constant Definitions *****************************/

#ifndef XPAR_XTMRCTR_NUM_INSTANCES
#define XPAR_XTMRCTR_NUM_INSTANCES	0
#endif

/*************************** Variable Declarations ***************************/

/* A table of configuration structures containing the configuration information
 * for each timer core in the system. */
extern XTmrCtr_Config XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES];

/**************************** Function Definitions ***************************/

/*****************************************************************************/
/**
* Looks up the device configuration based on the unique device ID. The table
* TmrCtr_ConfigTable contains the configuration info for each device in the
* system.
*
* @param	DeviceId is the unique device ID to search for in the config
*		table.
*
* @return	A pointer to the configuration that matches the given device
* 		ID, or NULL if no match is found.
*
* @note		None.
*
******************************************************************************/
XTmrCtr_Config *XTmrCtr_LookupConfig(u16 DeviceId)
{
	XTmrCtr_Config *CfgPtr = NULL;
	int Index;

	for (Index = 0; Index < XPAR_XTMRCTR_NUM_INSTANCES; Index++) {
		if (XTmrCtr_ConfigTable[Index].DeviceId == DeviceId) {
			CfgPtr = &XTmrCtr_ConfigTable[Index];
			break;
		}
	}

	return CfgPtr;
}

/** @} */
