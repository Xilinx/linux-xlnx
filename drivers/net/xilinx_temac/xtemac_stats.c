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
* @file xtemac_stats.c
*
* Functions in this file implement statistics related functionality.
* See xtemac.h for a detailed description of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  06/01/05 First release
* 2.00a rmm  11/21/05 Changed copyright
* </pre>
******************************************************************************/

/***************************** Include Files *********************************/

#include <linux/string.h>

#include "xtemac.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
* Get a current copy of the software maintained statistics. See
* XTemac_SoftStats structure for information on what counters are maintained.
*
* @param InstancePtr is a pointer to the instance to be worked on.
* @param StatsPtr is an output parameter, and is a pointer to a stats buffer
*        into which the current statistics will be copied.
*
******************************************************************************/
void XTemac_GetSoftStats(XTemac *InstancePtr, XTemac_SoftStats *StatsPtr)
{
	XASSERT_VOID(InstancePtr != NULL);
	XASSERT_VOID(StatsPtr != NULL);

	memcpy(StatsPtr, &InstancePtr->Stats, sizeof(XTemac_SoftStats));
}


/*****************************************************************************/
/**
* Zero out the software maintained statistics counters.
*
* @param InstancePtr is a pointer to the instance to be worked on.
*
******************************************************************************/
void XTemac_ClearSoftStats(XTemac *InstancePtr)
{
	XASSERT_VOID(InstancePtr != NULL);

	memset(&InstancePtr->Stats, 0, sizeof(XTemac_SoftStats));
}
